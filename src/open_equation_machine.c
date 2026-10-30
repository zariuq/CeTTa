#include "binding/frame_identity_quiet.h"
#include "atom.h"
#include "space.h"
#include "open_equation_machine.h"
#include "owned_execution.h"
#include "prepared_pure_machine.h"

#include "binding/frame_identity.h"
#include "grounded.h"
#include "match.h"
#include "native_query.h"
#include "petta_contract_core.h"
#include "petta_program.h"
#include "petta_semantics.h"
#include "petta_specializer.h"
#include "search_machine.h"
#include "shared_transition.h"
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
    uint8_t representation;
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
 * hole, which an enclosing let or constructor may already have shaped.  A
 * list built by `cons` that names a let pattern's computed head meets its
 * place in the output after the let's body (OemNode.late_output). */
typedef enum {
    OEM_S_RET = 0,   /* the activation's output is complete                */
    OEM_S_FAIL,
    OEM_S_CALL,      /* call `relation` with destination `pattern`         */
    OEM_S_TAIL,      /* call `relation` in place of this equation          */
    OEM_S_ENTRY,     /* validate the call plan before its argument goals;
                       a changed authority runs the original goal and
                       resumes at `target`                                */
    OEM_S_BUILTIN_ENTRY, /* a changed program runs this source occurrence
                            through the host before any argument goal */
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
    /* Entry checks whose next instruction needs no separate dispatch.
     * The original entry operands and source-exit target stay in place;
     * the following instruction retains its ordinary operands and index. */
    OEM_S_BUILTIN_PRIM,
    OEM_S_BUILTIN_TEST,
    OEM_S_BUILTIN_FAIL,
    OEM_S_BUILTIN_HOST,
    OEM_S_BUILTIN_BIND,
    OEM_S_BUILTIN_CALL,
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
    OEM_HOST_TYPE_ACCEPT,
    OEM_HOST_TYPE_MATCH,
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
    /* PeTTa's named state, `get-state` and `change-state!`, over values. */
    OEM_HOST_STATE,
    /* Structural predicates and boolean unification over evaluated values. */
    OEM_HOST_OBSERVE,
    /* Ground is-member whose matching multiplicity is zero or one. */
    OEM_HOST_MEMBER,
    /* Prepared structural projections; dispatch and argument preparation
     * remain those of the containing HOST or APPLY occurrence. */
    OEM_HOST_SEQUENCE_HEAD,
    OEM_HOST_SEQUENCE_TAIL,
    /* PeTTa's non-strict variable-privacy form: freshen its raw goal in the
     * region, then suspend only that computation to the host. */
    OEM_HOST_SEALED,
    /* A generated strict native over completed operand values. */
    OEM_HOST_NATIVE,
};

enum {
    OEM_STORE_FIRST = 1u,
    OEM_STORE_IF_EMPTY,
};

enum {
    OEM_ARM_TESTS = 1u,
    OEM_ARM_LATER_TEST = 2u,
};

/* The spelling of an existence observer (petta_semantics_existence_view):
 * the reification on the comparison's second side, under size-atom, of
 * once, and compared by `!=`. */
enum {
    OEM_EXISTS_SIDE2 = 1u,
    OEM_EXISTS_COUNTED = 2u,
    OEM_EXISTS_ONCE = 4u,
    OEM_EXISTS_NEGATED = 8u,
};

typedef struct {
    uint8_t kind;
    bool match_row; /* an already-decomposed conjunction leg */
    /* The step's `pattern` is a slot at its first occurrence on every path
     * through the body: the step stores its result there rather than
     * unifying (a PRIM, a BIND, or a HOST step the region may decide).
     * IF_EMPTY retains a cell acquired on an earlier fallback path. */
    uint8_t store;
    /* A CALL, or a HOST step's application entering a relation, that PeTTa
     * dispatches at run time: its body runs under the implicit handler, so
     * an error it raises fails that path alone (DispatchErrorScope). */
    uint8_t recovers;
    uint8_t checked_body;
    /* A literal `superpose` in a tail position: its alternatives return to
     * the equation's caller, as a tail call does. */
    uint8_t tail;
    /* Consecutive source entries before the first producer, validated at
     * one entry boundary. This occupies the step's alignment padding. */
    uint16_t ready;
    /* A type guard whose requirement variables no later guard of its typed
     * protocol reads (OemNode.type_dead). */
    uint8_t type_dead;
    /* A case arm's first tests, as its case's emission decided them: its
     * root tests the key (OEM_ARM_TESTS), and so does every later arm's
     * (OEM_ARM_LATER_TEST), so a key may leave it the case's last. */
    uint8_t arm;
    /* An OEM_HOST_EXISTS step's spelling: OEM_EXISTS_* bits. */
    uint8_t existence;
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
    /* The body begins by destructuring register `entry_guard_reg` with
     * PeTTa's decons-atom (oem_note_entry_guard): a call whose value there
     * is closed and not a non-empty list cannot answer from this equation. */
    bool has_entry_guard;
    uint32_t entry_guard_reg;
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

typedef enum {
    OEM_ALGEBRAIC_FOLD_NONE = 0,
    OEM_ALGEBRAIC_FOLD_LENGTH,
    OEM_ALGEBRAIC_FOLD_APPEND,
    OEM_ALGEBRAIC_FOLD_PROJECT_MAP,
    OEM_ALGEBRAIC_FOLD_CONTAINS,
    OEM_ALGEBRAIC_FOLD_DROP,
    OEM_ALGEBRAIC_FOLD_SCALAR_SUM,
} OemAlgebraicFold;

enum { OEM_PURE_RELATION_INLINE_ARGS = 16u };

typedef struct OemScalarProgram OemScalarProgram;
static void oem_scalar_free(OemScalarProgram *);

typedef struct {
    SymbolId head;
    uint32_t arity;
    bool body_is_data;
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
    /* Immutable source eligibility for prepared entry. The callee owns
     * current admission and stable refusal; this program does not cache
     * observations about a particular input or caller. */
    bool pure_entry_candidate;
    /* An exact source-recognized algebraic fold.  These constructor ids are
     * only an execution certificate for the relation whose complete source
     * recurrence was checked; they do not give similarly named functions
     * this meaning. */
    uint8_t algebraic_fold;
    OemScalarProgram *scalar_sum;
    SymbolId algebraic_nil;
    SymbolId algebraic_cons;
    SymbolId algebraic_item;
    uint32_t algebraic_item_arity;
    uint32_t algebraic_projection;
    SymbolId algebraic_true;
    SymbolId algebraic_false;
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
    /* A typed relation with a trusted version names it in `trusted` and
     * keeps the entry facts a checked call must meet to enter it.  The
     * version (`trusted_version`) holds the same equations, its calls to
     * itself compiled without their call protocol (petta_contract_core.h). */
    bool has_trusted;
    bool trusted_version;
    uint32_t trusted;
    PettaContractVersion version;
    /* The names of the spaces the version matches, as atoms of the
     * program, which its entry resolves. */
    Atom *version_spaces[PETTA_CONTRACT_MAX_SPACES];
} OemRelation;

/* One equation of a local relation: parameters, body and the body's plan. */
typedef struct {
    Atom *lhs;
    Atom *rhs;
    const PettaPlanNode *plan;
    PettaTypeCall contract;
    bool typed;
    bool hold_body;
    bool overload;
    /* The sole signature changes argument demand but installs no guard. */
    bool identity_typed;
} OemLocalSource;

typedef enum {
    OEM_READ_RELATION, OEM_READ_PROTOCOL, OEM_READ_BODY, OEM_READ_HEAD,
    OEM_READ_BUILTIN, OEM_READ_BUILTIN_EQUATIONS, OEM_READ_DELAY,
    OEM_READ_IDENTITY,
} OemHostReadKind;

typedef struct {
    OemHostReadKind kind;
    SymbolId head;
    uint32_t arity;
    Atom *expression;
    bool answer;
} OemHostRead;

typedef struct {
    _Atomic uint32_t refs;
    Arena atoms;
} OemSource;

static void oem_source_retain(void *owner) {
    OemSource *source = owner;
    atomic_fetch_add_explicit(&source->refs, 1u, memory_order_relaxed);
}

static void oem_source_release(void *owner) {
    OemSource *source = owner;
    if (source && atomic_fetch_sub_explicit(&source->refs, 1u,
                                            memory_order_acq_rel) == 1u) {
        arena_free(&source->atoms);
        free(source);
    }
}

struct CettaOpenEquationProgram {
    /* The compiler's reference and one per open cursor.  Cursors open and
     * close on worker threads (hyperpose), so the count is atomic: the last
     * release, which frees the program, follows every use before it. */
    _Atomic uint32_t refs;
    /* Charged to the resource ledger's live-program gauge when compiled. */
    bool ledger_counted;
    /* False denotes a stable, dependency-stamped compilation refusal. */
    bool admitted;
    CettaOpenEquationEntryKind entry_kind;
    Space *space;
    SpaceProgramToken token;
    SpaceReadToken source_rows;
    CettaCount source_row_count;
    /* All named syntax/contract leaves consulted by this compilation,
     * including heads that had no equation. Sorted and unique when complete.
     * This conservative footprint licenses only disjoint named appends. */
    SymbolId *read_heads;
    uint32_t read_head_len, read_head_cap;
    bool read_heads_complete;
    /* The space's petta_contract_callee_source_arity, once asked.  When a
     * call through a variable was compiled to apply its callee to evaluated
     * arguments, `assumes_value_callees` keeps any later declaration from
     * extending this program. */
    uint32_t callee_source_arity;
    bool callee_source_arity_known;
    bool assumes_value_callees;
    OemHostRead *host_reads;
    uint32_t host_read_len, host_read_cap;
    uint32_t host_read_mask;
    bool host_reads_complete;
    /* Read before compilation: equality certifies that no Space changed
     * while this immutable program was built or subsequently entered. */
    uint64_t compilation_mutation_epoch;
    /* The spelling of region and answer variables. */
    SymbolId spelling;
    /* While the equations of a trusted version compile: that version, whose
     * calls to itself skip their protocol. */
    bool compiling_trusted;
    uint32_t trusted_relation;
    /* The equations of local relations, and the heads made for them. */
    OemLocalSource *sources;
    uint32_t source_len, source_cap;
    OemControl *controls;
    uint32_t control_len, control_cap;
    Arena atoms;
    bool atoms_ready;
    /* A private entry's immutable source. Exported literals lease the
     * program just as literals of stored equations lease their owner. */
    OemSource *lexical_source;
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
    /* Under `evaluates_instances`, a match leg's own source and its exposed
     * output: a row that could still reduce sends that source to the host,
     * whose answers fill the output (a MATCH step's `relation`, plus one). */
    struct {
        uint32_t source;
        uint32_t exposed;
        /* This leg and the conjunction's legs after it. */
        uint32_t legs;
        bool tail;
    } *match_fallbacks;
    uint32_t match_fallback_len, match_fallback_cap;
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

/* Give back a vector's capacity beyond twice its length when that frees at
 * least OEM_SHRINK_MIN_BYTES: a later push grows it geometrically again, so
 * a shrink is paid for by the pushes that make the next one possible.  A
 * failed reallocation keeps the vector as it is. */
enum { OEM_SHRINK_MIN_BYTES = 1024u };
static void oem_shrink(void **items, uint32_t *cap, uint32_t len,
                       size_t size) {
    uint32_t keep = len ? len : 1u;
    if (!*items || *cap / 2u <= keep ||
        (size_t)(*cap - keep) * size < OEM_SHRINK_MIN_BYTES)
        return;
    void *shrunk = realloc(*items, (size_t)keep * size);
    if (!shrunk)
        return;
    *items = shrunk;
    *cap = keep;
}

/* A cursor's arenas start with a small block and grow geometrically: most
 * cursors hold little, and a suspended one keeps about what it holds. */
enum { OEM_FIRST_BLOCK_BYTES = 256u, OEM_FIRST_FRAMES = 4u };

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
    if (program->ledger_counted)
        cetta_runtime_stats_gauge_add(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PROGRAMS_LIVE,
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PROGRAMS_LIVE_PEAK, -1);
    free(program->host_reads);
    free(program->read_heads);
    for (uint32_t index = 0; index < program->relation_len; index++)
        oem_scalar_free(program->relations[index].scalar_sum);
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
    free(program->match_fallbacks);
    free(program->slot_stores);
    free(program->cell_slots);
    free(program->sources);
    free(program->controls);
    if (program->atoms_ready)
        arena_free(&program->atoms);
    oem_source_release(program->lexical_source);
    free(program);
}

bool cetta_open_equation_program_is_current(
    const CettaOpenEquationProgram *program) {
    return program && space_program_token_is_current(program->token);
}

CettaOpenEquationRead cetta_open_equation_program_read(
    const CettaOpenEquationProgram *program) {
    return program ? (CettaOpenEquationRead){
        .program = program, .program_token = program->token,
        .rows = program->source_rows, .row_count = program->source_row_count,
    } : (CettaOpenEquationRead){0};
}

static int oem_read_head_compare(const void *left, const void *right) {
    SymbolId a = *(const SymbolId *)left, b = *(const SymbolId *)right;
    return (a > b) - (a < b);
}

static void oem_read_heads_finish(CettaOpenEquationProgram *program) {
    if (!program || !program->read_heads_complete || !program->read_head_len)
        return;
    qsort(program->read_heads, program->read_head_len,
          sizeof(*program->read_heads), oem_read_head_compare);
    uint32_t unique = 1u;
    for (uint32_t index = 1u; index < program->read_head_len; index++) {
        if (program->read_heads[index] != program->read_heads[unique - 1u])
            program->read_heads[unique++] = program->read_heads[index];
    }
    program->read_head_len = unique;
}

static bool oem_reads_head(const CettaOpenEquationProgram *program,
                           SymbolId head) {
    if (!program || program->read_head_len == 0u || !program->read_heads)
        return false;
    return bsearch(&head, program->read_heads, program->read_head_len,
                    sizeof(*program->read_heads), oem_read_head_compare) != NULL;
}

bool cetta_open_equation_program_refresh(
    const CettaOpenEquationProgram *program, Space *space,
    CettaOpenEquationRead *read) {
    if (!program || !space || !read || read->program != program ||
        program->space != space || read->program_token.space != space ||
        read->program_token.instance_id != space_instance_id(space))
        return false;
    SpaceProgramToken now = space_program_token(space);
    SpaceReadToken rows = space_read_token(space);
    CettaCount count = space_length64(space);
    if (!space_program_token_eq(read->program_token, now)) {
        if (!program->read_heads_complete || space->overlay_base ||
            space_has_dependencies(space) ||
            space->match_backend.kind != SPACE_ENGINE_NATIVE ||
            !space_read_token_prefix_intact(read->rows) ||
            read->row_count > count)
            return false;
        for (CettaIndex index = read->row_count; index < count; index++) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PROGRAM_APPEND_ROW);
            Atom *row = space_get_at64(space, index);
            if (!row)
                return false;
            if (row->kind != ATOM_EXPR || row->expr.len != 3u ||
                row->expr.elems[0]->kind != ATOM_SYMBOL)
                continue;
            SymbolId form = row->expr.elems[0]->sym_id;
            Atom *subject = row->expr.elems[1];
            if (form == g_builtin_syms.subtype)
                return false;
            if (form == g_builtin_syms.equals ||
                form == g_builtin_syms.equals_percent) {
                if (subject->kind != ATOM_EXPR || subject->expr.len == 0u)
                    return false;
                subject = subject->expr.elems[0];
            } else if (form != g_builtin_syms.colon) {
                continue;
            } else if (subject->kind == ATOM_EXPR) {
                /* Compilation asks for types of named heads. A compound
                 * subject cannot unify with a symbol, even with open fields. */
                continue;
            } else if (program->assumes_value_callees) {
                /* Any callee may be this name, now declared. */
                return false;
            }
            if (subject->kind != ATOM_SYMBOL ||
                oem_reads_head(program, subject->sym_id))
                return false;
        }
    }
    if (!space_read_token_matches_live_space(rows, space) ||
        !space_program_token_matches_live_space(now, space))
        return false;
    *read = (CettaOpenEquationRead){
        .program = program, .program_token = now, .rows = rows, .row_count = count,
    };
    return true;
}


/* Compilation is an adaptive query over host authorities. Record the actual
 * Boolean observations, including negative lookups. A foreign revision need
 * not rebuild code when every observation still agrees. */
static uint32_t oem_host_read_mask(const CettaOpenEquationHost *host) {
    if (!host)
        return 0u;
    return (!!host->relation_admitted << OEM_READ_RELATION) |
        (!!host->call_protocol_admitted << OEM_READ_PROTOCOL) |
        (!!host->body_admitted << OEM_READ_BODY) |
        (!!host->head_callable << OEM_READ_HEAD) |
        (!!host->builtin_allowed << OEM_READ_BUILTIN) |
        (!!host->builtin_equations << OEM_READ_BUILTIN_EQUATIONS) |
        (!!host->may_delay << OEM_READ_DELAY) |
        (!!host->retains_variable_identity << OEM_READ_IDENTITY);
}

static bool oem_host_query(const CettaOpenEquationHost *host, Space *space,
                           const OemHostRead *read) {
    switch (read->kind) {
    case OEM_READ_RELATION:
        return host->relation_admitted(host->context, space, read->head, read->arity);
    case OEM_READ_PROTOCOL:
        return host->call_protocol_admitted(host->context, space, read->head, read->arity);
    case OEM_READ_BODY:
        return host->body_admitted(host->context, space, read->head, read->arity);
    case OEM_READ_HEAD:
        return host->head_callable(host->context, space, read->expression);
    case OEM_READ_BUILTIN:
        return host->builtin_allowed(host->context, read->head);
    case OEM_READ_BUILTIN_EQUATIONS:
        return host->builtin_equations(host->context, space, read->head, read->arity);
    case OEM_READ_DELAY:
        return host->may_delay(host->context, read->head, read->arity);
    case OEM_READ_IDENTITY:
        return host->retains_variable_identity(host->context, read->head);
    }
    return false;
}

bool cetta_open_equation_program_host_reads_current(
    const CettaOpenEquationProgram *program, const CettaOpenEquationHost *host) {
    if (!program || !program->host_reads_complete ||
        oem_host_read_mask(host) != program->host_read_mask)
        return false;
    for (uint32_t index = 0u; index < program->host_read_len; index++) {
        const OemHostRead *read = &program->host_reads[index];
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PROGRAM_HOST_READ);
        if (oem_host_query(host, program->space, read) != read->answer)
            return false;
    }
    return true;
}

typedef struct {
    CettaOpenEquationProgram *program;
    const CettaOpenEquationHost *host;
    AtomDeepCopySession *copies;
} OemHostRecorder;

static bool oem_host_record(OemHostRecorder *recorder, Space *space,
                            OemHostRead read) {
    CettaOpenEquationProgram *program = recorder->program;
    bool answer = oem_host_query(recorder->host, space, &read);
    if (space != program->space)
        program->host_reads_complete = false;
    if (!program->host_reads_complete)
        return answer;
    read.answer = answer;
    if (!oem_reserve((void **)&program->host_reads, &program->host_read_cap,
                     program->host_read_len + 1u, sizeof(*program->host_reads))) {
        program->host_reads_complete = false;
        return answer;
    }
    if (read.expression) {
        if (!program->atoms_ready) {
            arena_init_detached(&program->atoms);
            program->atoms_ready = true;
        }
        if (!recorder->copies)
            recorder->copies = atom_deep_copy_session_new(&program->atoms);
        read.expression = recorder->copies
            ? atom_deep_copy_session_copy(recorder->copies, read.expression) : NULL;
        if (!read.expression) {
            program->host_reads_complete = false;
            return answer;
        }
    }
    program->host_reads[program->host_read_len++] = read;
    return answer;
}

#define OEM_RECORD_NAMED(NAME, KIND)                                       \
static bool NAME(void *context, Space *space, SymbolId head, uint32_t arity) { \
    return oem_host_record(context, space, (OemHostRead){                  \
        .kind = KIND, .head = head, .arity = arity,                        \
    });                                                                  \
}
OEM_RECORD_NAMED(oem_record_relation, OEM_READ_RELATION)
OEM_RECORD_NAMED(oem_record_protocol, OEM_READ_PROTOCOL)
OEM_RECORD_NAMED(oem_record_body, OEM_READ_BODY)
OEM_RECORD_NAMED(oem_record_builtin_equations, OEM_READ_BUILTIN_EQUATIONS)
#undef OEM_RECORD_NAMED
static bool oem_record_head(void *context, Space *space, Atom *expression) {
    return oem_host_record(context, space, (OemHostRead){
        .kind = OEM_READ_HEAD, .expression = expression,
    });
}
static bool oem_record_builtin(void *context, SymbolId head) {
    OemHostRecorder *recorder = context;
    return oem_host_record(recorder, recorder->program->space, (OemHostRead){
        .kind = OEM_READ_BUILTIN, .head = head,
    });
}
static bool oem_record_delay(void *context, SymbolId head, uint32_t arity) {
    OemHostRecorder *recorder = context;
    return oem_host_record(recorder, recorder->program->space, (OemHostRead){
        .kind = OEM_READ_DELAY, .head = head, .arity = arity,
    });
}
static bool oem_record_identity(void *context, SymbolId head) {
    OemHostRecorder *recorder = context;
    return oem_host_record(recorder, recorder->program->space, (OemHostRead){
        .kind = OEM_READ_IDENTITY, .head = head,
    });
}

/* Sort the finite trace once: no growing linear membership scan during
 * compilation, and repeated expression reads share their copied syntax. */
static int oem_host_read_compare(const void *left, const void *right) {
    const OemHostRead *a = left, *b = right;
    if (a->kind != b->kind)
        return (a->kind > b->kind) - (a->kind < b->kind);
    if (a->head != b->head)
        return (a->head > b->head) - (a->head < b->head);
    if (a->arity != b->arity)
        return (a->arity > b->arity) - (a->arity < b->arity);
    uintptr_t x = (uintptr_t)a->expression, y = (uintptr_t)b->expression;
    return (x > y) - (x < y);
}

static void oem_host_reads_finish(OemHostRecorder *recorder) {
    CettaOpenEquationProgram *program = recorder->program;
    atom_deep_copy_session_free(recorder->copies);
    recorder->copies = NULL;
    if (!program->host_reads_complete || !program->host_read_len)
        return;
    qsort(program->host_reads, program->host_read_len,
          sizeof(*program->host_reads), oem_host_read_compare);
    uint32_t unique = 1u;
    for (uint32_t index = 1u; index < program->host_read_len; index++) {
        OemHostRead *previous = &program->host_reads[unique - 1u];
        OemHostRead *next = &program->host_reads[index];
        if (oem_host_read_compare(previous, next))
            program->host_reads[unique++] = *next;
        else if (previous->answer != next->answer)
            program->host_reads_complete = false;
    }
    program->host_read_len = unique;
}

static CettaOpenEquationHost oem_recording_host(OemHostRecorder *recorder) {
    CettaOpenEquationHost host = *recorder->host;
    host.context = recorder;
    if (host.relation_admitted)
        host.relation_admitted = oem_record_relation;
    if (host.call_protocol_admitted)
        host.call_protocol_admitted = oem_record_protocol;
    if (host.body_admitted)
        host.body_admitted = oem_record_body;
    if (host.head_callable)
        host.head_callable = oem_record_head;
    if (host.builtin_allowed)
        host.builtin_allowed = oem_record_builtin;
    if (host.builtin_equations)
        host.builtin_equations = oem_record_builtin_equations;
    if (host.may_delay)
        host.may_delay = oem_record_delay;
    if (host.retains_variable_identity)
        host.retains_variable_identity = oem_record_identity;
    return host;
}

static bool oem_record_read_names(CettaOpenEquationProgram *program, Atom *root);

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
    OEM_N_BUILTIN_ENTRY, /* native body while its source authority is current */
    OEM_N_CALL,        /* a relation call; its output is the hole `exposed` */
    OEM_N_PRIM,
    OEM_N_LET,
    OEM_N_SEQUENCE,    /* ordered child goals; `exposed` selects one output */
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
    /* A HOST node's fast path (OEM_HOST_*), and an existence observer's
     * spelling (OEM_EXISTS_* bits). */
    uint8_t host_fast;
    uint8_t existence;
    /* A call or an application PeTTa dispatches at run time (OemStep). */
    bool recovers;
    bool checked_body;
    bool check_entry;
    bool identity_typed;
    /* A trusted version's call to itself: no protocol ran, so a changed
     * program sends the call to the host's complete protocol. */
    bool protocol_skipped;
    /* A typed protocol's guard whose requirement variables no later guard
     * mentions: only acceptance of its value is observable. */
    bool type_dead;
    /* A let whose body's output names its pattern's computed head only in
     * lists the body builds by `cons`, as
     * `(let ($h $t) (decons-atom $xs) (cons-atom $h $t))` does.  PeTTa's
     * translation computes such a list by a call after the pattern is read,
     * so the let exposes its body's output with each of those lists a fresh
     * hole, and meets the holes with the lists after the body: the
     * destination cannot bind the head before the pattern is read.  The
     * pairs (hole, list) are compile->late_binds[late_first, +late_count). */
    bool late_output;
    uint32_t late_first;
    uint32_t late_count;
    uint32_t call_plan;
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

/* Lexically disjoint siblings excluded by a one-shot native test. */
typedef struct OemInactiveBranch {
    const Atom *source;
    const struct OemInactiveBranch *previous;
} OemInactiveBranch;

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
    /* A let has a late output (OemNode.late_output), and the pairs
     * (hole, list) that meet after their lets' bodies. */
    bool late_outputs;
    uint32_t *late_binds;
    uint32_t late_bind_len, late_bind_cap;
    /* While a let's pattern is lowered: the let (for a let*, the whole),
     * and the bindings a let* evaluates before this one. */
    const Atom *let_scope;
    Atom *const *let_before;
    uint32_t let_before_len;
    const OemInactiveBranch *inactive;
    const char *reason;
} OemCompile;

static bool oem_reject(OemCompile *compile, const char *reason) {
    if (!compile->reason)
        compile->reason = reason;
    return false;
}

static bool oem_slot_of(OemCompile *compile, VarId id, uint32_t *slot_out) {
    for (uint32_t index = 0u; index < compile->var_len; index++) {
        if (var_id_eq(compile->vars[index], id)) {
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
            if (var_id_eq(compile->vars[index], atom->var_id))
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

/* Whether the compiler made `atom` in the program's own storage, which goes
 * with the program. */
static inline bool oem_program_made(const OemCompile *compile,
                                    const Atom *atom) {
    return compile->program->atoms_ready &&
        atom->arena_id == compile->program->atoms.identity;
}

/* A leaf the program's code shares into the terms it builds or binds.
 * Those terms may outlive the program, so a symbol or `()` the compiler
 * made is the canonical atom; the equations' own atoms are the space's and
 * stay as they are.  NULL for a leaf the compiler made with no canonical
 * form. */
static Atom *oem_shared_leaf(const OemCompile *compile, Atom *atom) {
    if (!oem_program_made(compile, atom))
        return atom;
    if (atom->kind == ATOM_SYMBOL)
        return atom_symbol_canonical(atom->sym_id);
    if (atom->kind == ATOM_EXPR && atom->expr.len == 0u)
        return atom_unit_canonical();
    return NULL;
}

static bool oem_template_build(OemCompile *compile, const uint32_t *children,
                               uint32_t count, uint32_t *out);

static bool oem_template_literal(OemCompile *compile, Atom *atom,
                                 uint32_t *out) {
    /* A ground expression the compiler made is built from its parts at each
     * run rather than shared, for the reason above. */
    if (atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        oem_program_made(compile, atom)) {
        uint32_t inline_children[8];
        uint32_t *children = atom->expr.len <= 8u
            ? inline_children
            : malloc(sizeof(*children) * atom->expr.len);
        if (!children)
            return oem_reject(compile, "out of memory");
        bool ok = true;
        for (CettaExprIndex index = 0u; ok && index < atom->expr.len; index++)
            ok = oem_template_literal(compile, atom->expr.elems[index],
                                      &children[index]);
        ok = ok && oem_template_build(compile, children,
                                      (uint32_t)atom->expr.len, out);
        if (children != inline_children)
            free(children);
        return ok;
    }
    Atom *shared = oem_shared_leaf(compile, atom);
    if (!shared)
        return oem_reject(compile, "a leaf made by the compiler has no "
                                   "canonical atom");
    OemTemplate item = {.kind = OEM_T_LITERAL, .literal = shared};
    return OEM_PUSH(compile->program, template, item, out) ||
        oem_reject(compile, "out of memory");
}

/* The head of operation `op` as a literal template, made once here for the
 * grounded dispatcher rather than at each run; none without an operation.
 * The dispatcher may build it into a value that outlives the program, so it
 * is the symbol's canonical atom, not the program's. */
static bool oem_op_literal(OemCompile *compile, SymbolId op, uint32_t *out) {
    *out = 0u;
    if (op == SYMBOL_ID_NONE)
        return true;
    Atom *head = atom_symbol_canonical(op);
    return head ? oem_template_literal(compile, head, out)
                : oem_reject(compile, "out of memory");
}

static bool oem_template_slot(OemCompile *compile, uint32_t slot,
                              uint32_t *out) {
    OemTemplate item = {.kind = OEM_T_SLOT, .slot = slot};
    return OEM_PUSH(compile->program, template, item, out) ||
        oem_reject(compile, "out of memory");
}

/* Whether a pattern's root tests what it meets, as a literal or an
 * expression does; a slot's, a list's or a variable's binds. */
static inline bool oem_template_tests(const CettaOpenEquationProgram *program,
                                      uint32_t template_index) {
    uint8_t kind = program->templates[template_index].kind;
    return kind == OEM_T_LITERAL || kind == OEM_T_BUILD;
}

enum { OEM_REPRESENTATION_LIST = 0x80u };

static inline __attribute__((always_inline)) uint8_t
oem_value_representation(const Atom *atom) {
    /* The constructor's negative fact covers both private role tags and
     * typed lists. Ordinary terms need no inspection of their children. */
    if (!atom_structural_may_have_internal_tag(atom))
        return PETTA_VALUE_ORDINARY;
    return (uint8_t)atom_petta_value_representation(atom) |
        (atom_is_list(atom) ? OEM_REPRESENTATION_LIST : 0u);
}

static inline __attribute__((always_inline)) bool
oem_decomposition_compatible(const Atom *left, const Atom *right) {
    return oem_value_representation(left) == oem_value_representation(right);
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
    const OemTemplate *head = count
        ? &program->templates[children[0]] : NULL;
    Atom *head_value = head && head->kind == OEM_T_LITERAL ? head->literal : NULL;
    if (atom_is_internal_tag(head_value, CETTA_INTERNAL_TAG_LIST))
        item.representation = OEM_REPRESENTATION_LIST;
    if ((count == 2u && atom_is_internal_tag(head_value,
                CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND)) ||
        (count == 3u && atom_is_internal_tag(head_value,
                CETTA_INTERNAL_TAG_PETTA_PARTIAL))) {
        item.representation = PETTA_VALUE_COMPOUND;
    } else if (count == 3u) {
        const OemTemplate *domain = &program->templates[children[1]];
        Atom *domain_head = NULL;
        if (domain->kind == OEM_T_LITERAL && domain->literal->kind == ATOM_EXPR &&
            domain->literal->expr.len == 2u)
            domain_head = domain->literal->expr.elems[0];
        else if (domain->kind == OEM_T_BUILD && domain->count == 2u) {
            const OemTemplate *tag = &program->templates[
                program->template_children[domain->first]];
            domain_head = tag->kind == OEM_T_LITERAL ? tag->literal : NULL;
        }
        if (atom_is_internal_tag(head_value, CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE) ||
            atom_is_internal_tag(domain_head, CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY))
            item.representation = PETTA_VALUE_REGISTERED_CALLABLE;
    }
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
                               bool body_is_data,
                               uint32_t *index_out) {
    for (uint32_t index = 0u; index < program->relation_len; index++) {
        if (!(program->entry_kind == CETTA_OPEN_ENTRY_READY_CALL && index == 0u) &&
            !program->relations[index].trusted_version &&
            program->relations[index].head == head &&
            program->relations[index].arity == arity &&
            program->relations[index].body_is_data == body_is_data) {
            *index_out = index;
            return true;
        }
    }
    if (program->relation_len >= OEM_MAX_RELATIONS)
        return false;
    OemRelation relation = {
        .head = head, .arity = arity, .body_is_data = body_is_data,
    };
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
    if (atom_is_list_rest(param))
        return oem_reject(compile, "typed rest pattern");
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
        /* An open argument is bound to the literal itself. */
        Atom *literal = oem_shared_leaf(compile, param);
        if (!literal)
            return oem_reject(compile, "a leaf made by the compiler has no "
                                       "canonical atom");
        OemMatchOp op = {.kind = OEM_M_ATOM, .reg = reg, .literal = literal};
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
        .literal = param,
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
    if (!plan || expr->kind != ATOM_EXPR || expr->expr.len == 0u ||
        expr->expr.elems[0]->kind != ATOM_SYMBOL ||
        plan->role != PETTA_PLAN_STATIC_CALL || !plan->relation_head_admitted)
        return false;
    SymbolId head = expr->expr.elems[0]->sym_id;
    /* Binding forms are syntax only at the arities read by their translator.
     * Their already callable occurrences at other arities use the ordinary
     * equation protocol, despite the form mark on the source plan. */
    return (plan->execution == PETTA_PLAN_EXEC_RELATION_SLOTS &&
            petta_plan_computation(plan).control.operation == CETTA_CONTROL_INVOKE &&
            petta_semantics_form(head) == PETTA_FORM_NONE) ||
        (petta_semantics_runtime_head(head) == PETTA_RUNTIME_HEAD_DATA &&
         !petta_semantics_special_form_reads(head, expr->expr.len - 1u));
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
 * occurs in no parameter, and in the active body only within the let, not
 * in the bindings a let* evaluates before this one. Only siblings excluded
 * by an already-value native test are inactive; its condition still counts.
 * Nothing then binds the head before the pattern. A bound head, as in `(let $h f (let ($h 2) 3 ...))`, makes a
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
    /* Each fence is a sibling of an ancestor on this path. Subtract its
     * occurrences, rather than skipping by pointer: immutable source nodes
     * may be shared by both branches. Conditions remain in the active body. */
    for (const OemInactiveBranch *branch = compile->inactive; branch;
         branch = branch->previous) {
        uint64_t excluded = oem_var_occurrences(branch->source, id, 0u);
        if (excluded >= deep || excluded > in_body)
            return false;
        in_body -= excluded;
    }
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
    if (atom_is_list_rest(pattern))
        return oem_reject(compile, "typed rest pattern");
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

/* Exposing a result can bind its slots before its body starts. A source
 * variable used as a computed pattern head is fresh only if that earlier
 * result unification cannot reach it. */
static bool oem_template_mentions_slot(const CettaOpenEquationProgram *program,
        uint32_t index, uint32_t slot, uint32_t depth) {
    if (index >= program->template_len || depth > OEM_MAX_DEPTH)
        return true;
    const OemTemplate *item = &program->templates[index];
    if (item->kind == OEM_T_SLOT)
        return item->slot == slot;
    if (item->kind != OEM_T_BUILD && !oem_template_is_list(item))
        return false;
    for (uint32_t child = 0u; child < item->count; child++) {
        if (oem_template_mentions_slot(program,
                program->template_children[item->first + child], slot,
                depth + 1u))
            return true;
    }
    return false;
}

/* Whether a template names `slot` outside the lists it builds by `cons`. */
static bool oem_template_mentions_slot_directly(
        const CettaOpenEquationProgram *program, uint32_t index,
        uint32_t slot, uint32_t depth) {
    if (index >= program->template_len || depth > OEM_MAX_DEPTH)
        return true;
    const OemTemplate *item = &program->templates[index];
    if (item->kind == OEM_T_SLOT)
        return item->slot == slot;
    if (item->kind != OEM_T_BUILD && item->kind != OEM_T_PATTERN_LIST)
        return false;
    for (uint32_t child = 0u; child < item->count; child++) {
        if (oem_template_mentions_slot_directly(program,
                program->template_children[item->first + child], slot,
                depth + 1u))
            return true;
    }
    return false;
}

/* A let's output may name a computed head of its pattern, which the
 * destination would bind if the output met it before the pattern is read.
 * Named in data, it is: PeTTa's translation exposes data in the clause's
 * head, and the pattern then reads as a call, outside the fragment.  Named
 * only in lists the body builds by `cons`, which that translation computes
 * by a call after the pattern, the let gets a late output (*late). */
static bool oem_pattern_output_independent(OemCompile *compile, Atom *pattern,
        const PettaPlanNode *plan, uint32_t output, uint32_t depth,
        bool *late) {
    if (!pattern || depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "pattern proof is too deep");
    if (pattern->kind != ATOM_EXPR)
        return true;
    if (pattern->expr.len && plan && plan->role != PETTA_PLAN_DATA &&
        pattern->expr.elems[0]->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        if (!oem_slot_of(compile, pattern->expr.elems[0]->var_id, &slot))
            return false;
        if (oem_template_mentions_slot_directly(compile->program, output,
                                                slot, 0u))
            return oem_reject(compile,
                "pattern head can be bound by the demanded result");
        if (oem_template_mentions_slot(compile->program, output, slot, 0u))
            *late = true;
    }
    for (CettaExprIndex child = 0u; child < pattern->expr.len; child++) {
        if (!oem_pattern_output_independent(compile,
                pattern->expr.elems[child],
                plan ? petta_plan_child(plan, child) : NULL,
                output, depth + 1u, late))
            return false;
    }
    return true;
}

/* Whether a construct's goals can all run before a goal that follows them:
 * nothing in it ends the activation, as a tail call, a branch or a failure
 * does. */
static bool oem_value_emittable(const OemCompile *compile, uint32_t index,
                                uint32_t depth) {
    if (depth > OEM_MAX_DEPTH)
        return false;
    const OemNode *node = &compile->nodes[index];
    switch ((OemNodeKind)node->kind) {
    case OEM_N_LET:
        return oem_value_emittable(compile, node->value, depth + 1u) &&
            oem_value_emittable(compile, node->body, depth + 1u);
    case OEM_N_BUILTIN_ENTRY:
        return oem_value_emittable(compile, node->body, depth + 1u);
    case OEM_N_CALL:
    case OEM_N_SEQUENCE:
    case OEM_N_ALTERNATIVES:
        if (node->tail)
            return false;
        /* fallthrough */
    case OEM_N_VALUE:
    case OEM_N_PRIM:
    case OEM_N_APPLY:
    case OEM_N_COLLECT:
    case OEM_N_ONCE:
        for (uint32_t child = 0u; child < node->count; child++) {
            if (!oem_value_emittable(compile,
                    compile->node_children[node->first + child], depth + 1u))
                return false;
        }
        return true;
    case OEM_N_HOST:
        return true;
    default:
        return false;
    }
}

static bool oem_hole(OemCompile *compile, uint32_t *out);

/* `index` with each list it builds by `cons` a fresh hole, recording the
 * pairs (hole, list) in compile->late_binds. */
static bool oem_template_late_lists(OemCompile *compile, uint32_t index,
                                    uint32_t depth, uint32_t *out) {
    if (depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "pattern proof is too deep");
    const OemTemplate item = compile->program->templates[index];
    *out = index;
    if (item.kind == OEM_T_LIST) {
        if (!oem_reserve((void **)&compile->late_binds,
                         &compile->late_bind_cap,
                         compile->late_bind_len + 2u,
                         sizeof(*compile->late_binds)))
            return oem_reject(compile, "out of memory");
        if (!oem_hole(compile, out))
            return false;
        compile->late_binds[compile->late_bind_len++] = *out;
        compile->late_binds[compile->late_bind_len++] = index;
        return true;
    }
    if (item.kind != OEM_T_BUILD || item.count == 0u)
        return true;
    uint32_t *children = malloc(sizeof(*children) * item.count);
    if (!children)
        return oem_reject(compile, "out of memory");
    bool ok = true;
    bool changed = false;
    for (uint32_t child = 0u; ok && child < item.count; child++) {
        uint32_t source =
            compile->program->template_children[item.first + child];
        ok = oem_template_late_lists(compile, source, depth + 1u,
                                     &children[child]);
        changed = changed || children[child] != source;
    }
    ok = ok && (!changed ||
                oem_template_build(compile, children, item.count, out));
    free(children);
    return ok;
}

/* Gives a let a late output (OemNode.late_output). */
static bool oem_late_output(OemCompile *compile, OemNode *node) {
    if (!oem_value_emittable(compile, node->body, 0u))
        return oem_reject(compile,
            "pattern head can be bound by the demanded result");
    uint32_t first = compile->late_bind_len / 2u;
    if (!oem_template_late_lists(compile, compile->nodes[node->body].exposed,
                                 0u, &node->exposed))
        return false;
    node->late_output = true;
    node->late_first = first;
    node->late_count = compile->late_bind_len / 2u - first;
    compile->late_outputs = true;
    return true;
}

/* A late output's lists meet their holes after the let's body. */
static bool oem_emit_late_binds(OemCompile *compile, const OemNode *node) {
    for (uint32_t pair = node->late_first;
         pair < node->late_first + node->late_count; pair++) {
        if (!oem_emit(compile, (OemStep){
                          .kind = OEM_S_BIND,
                          .pattern = compile->late_binds[2u * pair],
                          .value = compile->late_binds[2u * pair + 1u],
                      }, NULL))
            return false;
    }
    return true;
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
        petta_plan_computation(application).control.operation != CETTA_CONTROL_INVOKE)
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
    /* The relation's ground parts reach the values it builds and binds (a
     * call with an unbound list binds it to `()`), which may outlive the
     * program: they are canonical atoms, not the program's. */
    Arena *atoms = &program->atoms;
    Atom *head = atom_symbol_canonical(program->spelling);
    Atom *fold = atom_symbol_canonical(foldl);
    Atom *step_atom = atom_symbol_canonical(step);
    Atom *cons = atom_symbol_canonical(symbol_intern_cstr(g_symbols, "cons"));
    Atom *x = atom_var_with_id(atoms, "x", fresh_var_id());
    Atom *xs = atom_var_with_id(atoms, "xs", fresh_var_id());
    Atom *acc = atom_var_with_id(atoms, "acc", fresh_var_id());
    Atom *nil = atom_unit_canonical();
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
        petta_program_plan_transient(compile->petta, acc, NULL);
    const PettaPlanNode *recursion_plan = oem_fold_recursion_plan(
        program, petta_program_plan_transient(compile->petta, recursion, NULL));
    if (!acc_plan || !recursion_plan)
        return true;
    OemRelation relation = {
        .head = SYMBOL_ID_NONE, .arity = 2u, .local = true,
        .fold_step = step, .first_source = program->source_len,
        .source_count = 2u,
    };
    uint32_t unused = 0u;
    if (!OEM_PUSH(program, source,
                  ((OemLocalSource){.lhs = empty_lhs, .rhs = acc,
                                     .plan = acc_plan}), &unused) ||
        !OEM_PUSH(program, source,
                  ((OemLocalSource){.lhs = cons_lhs, .rhs = recursion,
                                     .plan = recursion_plan}),
                  &unused) ||
        !OEM_PUSH(program, relation, relation, out))
        return oem_reject(compile, "out of memory");
    return true;
}

static bool oem_call_relation(OemCompile *compile, Atom *call,
                              uint32_t *relation_out) {
    return oem_relation_index(compile->program,
                              call->expr.elems[0]->sym_id,
                              call->expr.len - 1u, false, relation_out) ||
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
                !cetta_control_operation_is_control_form(plan->computation.control) &&
                petta_semantics_form(head->sym_id) == PETTA_FORM_NONE;
        if (plan->role != PETTA_PLAN_DYNAMIC_CALL ||
            cetta_control_operation_is_control_form(plan->computation.control))
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
 * see, and a goal that may leave goals delayed on the equation's variables
 * would outlive the goal in variables the region moves; such an expression
 * stays outside the tier. */
static bool oem_host_goal_admitted(const OemCompile *compile, Atom *expr,
                                   uint32_t depth) {
    if (depth > OEM_MAX_DEPTH)
        return false;
    if (expr->kind != ATOM_EXPR || expr->expr.len == 0u)
        return true;
    Atom *head = expr->expr.elems[0];
    if (head->kind == ATOM_SYMBOL &&
        (petta_semantics_form(head->sym_id) == PETTA_FORM_CUT ||
         head->sym_id == g_builtin_syms.return_text))
        return false;
    /* A test whether a match has a row compares a collection with () or
     * its count with 0, which `!=` decides at once: no binding can make a
     * list of answers meet (), or change a count. */
    bool existence = petta_semantics_existence_view(
        expr, compile->host ? compile->host->reify_head : SYMBOL_ID_NONE,
        NULL);
    if (head->kind == ATOM_SYMBOL && !existence && compile->host &&
        compile->host->may_delay &&
        compile->host->may_delay(compile->host->context, head->sym_id,
                                 (uint32_t)(expr->expr.len - 1u)))
        return false;
    if (head->kind == ATOM_SYMBOL && compile->host &&
        compile->host->retains_variable_identity &&
        compile->host->retains_variable_identity(
            compile->host->context, head->sym_id))
        return false;
    for (CettaExprIndex child = 0u; child < expr->expr.len; child++) {
        if (!oem_host_goal_admitted(compile, expr->expr.elems[child],
                                    depth + 1u))
            return false;
    }
    return true;
}

/* Whether the plan takes every field of `expr` from `first` on as a value:
 * a variable's value or call-free data. */
static bool oem_field_is_value(const PettaPlanNode *field) {
    return field &&
        (field->role == PETTA_PLAN_VALUE ||
         (field->role == PETTA_PLAN_DATA && !field->contains_call));
}

static bool oem_fields_are_values(const Atom *expr, const PettaPlanNode *plan,
                                  CettaExprIndex first) {
    for (CettaExprIndex index = first; index < expr->expr.len; index++) {
        if (!oem_field_is_value(petta_plan_child(plan, index)))
            return false;
    }
    return true;
}

/* A type-pure grounded operation the language offers that is no PeTTa form:
 * the region may apply it to its arguments' values. */
static bool oem_pure_operation(const OemCompile *compile, SymbolId head) {
    return (petta_semantics_grounded_type_pure(head) ||
            head == g_builtin_syms.petta_min ||
            head == g_builtin_syms.petta_max) &&
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
          cetta_sequence_projection_call(head, nargs,
              CETTA_SEQUENCE_PETTA_VALUE).projection !=
                  CETTA_SEQUENCE_PROJECTION_NONE ||
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

static bool oem_member_call(const OemCompile *compile, const Atom *expr,
                            const PettaPlanNode *plan) {
    return expr->expr.len == 3u &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) ==
            PETTA_FORM_IS_MEMBER &&
        plan->role == PETTA_PLAN_STATIC_CALL && compile->host &&
        compile->host->builtin_allowed &&
        compile->host->builtin_allowed(
            compile->host->context, expr->expr.elems[0]->sym_id);
}

static bool oem_observation_call(const OemCompile *compile, const Atom *expr,
                                  const PettaPlanNode *plan) {
    if (expr->expr.elems[0]->kind != ATOM_SYMBOL ||
        plan->role != PETTA_PLAN_STATIC_CALL || !compile->host ||
        !compile->host->builtin_allowed ||
        !compile->host->builtin_allowed(compile->host->context,
                                        expr->expr.elems[0]->sym_id))
        return false;
    SymbolId head = expr->expr.elems[0]->sym_id;
    PeTTaForm form = petta_semantics_form(head);
    /* SWI-PeTTa evaluates both operands left-to-right before its rollback-
     * safe unification trial. An APPLY node preserves that operand plan for
     * computed and open operands; once their answers arrive, the region's
     * existing observation step performs the same trial and Boolean result. */
    return (expr->expr.len == 3u && head == g_builtin_syms.equals) ||
        (expr->expr.len == 2u &&
         (form == PETTA_FORM_IS_VAR || form == PETTA_FORM_IS_EXPR ||
          form == PETTA_FORM_IS_GROUND || form == PETTA_FORM_IS_SPACE));
}

/* An atomic constant: a template the observation can ignore. */
static bool oem_existence_constant_template(Atom *template) {
    return !atom_has_vars(template) && !atom_is_error(template) &&
        (template->kind == ATOM_SYMBOL ||
         (template->kind == ATOM_GROUNDED &&
          term_universe_atom_is_stable(template)));
}

/* The template of an existence observer's match, when the observation can
 * ignore it, as the search machine requires before it answers the observer
 * by membership: an atomic constant, or a position the plan reads as a
 * value, which the step checks to be one when it runs (oem_exists_step).
 * No intrinsic of the observer (the comparison, size-atom, the
 * reification, once, match) may have acquired authored equations, which
 * the host runs. */
static bool oem_existence_admitted(const OemCompile *compile, const Atom *expr,
                                   const PettaPlanNode *plan,
                                   const PeTTaExistenceView *view) {
    const Atom *heads[5];
    const PettaPlanNode *plans[5];
    uint32_t count = 0u;
    heads[count] = expr;
    plans[count++] = plan;
    const Atom *at = expr->expr.elems[view->observed];
    const PettaPlanNode *at_plan = petta_plan_child(plan, view->observed);
    for (uint32_t level = 0u; level < (view->counted ? 3u : 2u) +
                                      (view->once ? 1u : 0u) - 1u; level++) {
        heads[count] = at;
        plans[count++] = at_plan;
        at = at->expr.elems[1];
        at_plan = petta_plan_child(at_plan, 1u);
    }
    heads[count] = at;
    plans[count++] = at_plan;
    const Atom *match = at;
    const PettaPlanNode *template_plan = petta_plan_child(at_plan, 3u);
    if (match != view->match ||
        (!oem_existence_constant_template(match->expr.elems[3]) &&
         (!template_plan || template_plan->role != PETTA_PLAN_VALUE)))
        return false;
    if (!compile->host || !compile->host->builtin_equations)
        return true;
    for (uint32_t index = 0u; index < count; index++) {
        const PettaPlanNode *head_plan = plans[index];
        if (head_plan && (head_plan->role == PETTA_PLAN_DATA ||
                          head_plan->role == PETTA_PLAN_VALUE))
            continue;
        if (compile->host->builtin_equations(
                compile->host->context, compile->program->space,
                heads[index]->expr.elems[0]->sym_id,
                heads[index]->expr.len - 1u))
            return false;
    }
    return true;
}

/* PeTTa's named state: `(get-state name)` and `(change-state! name value)`,
 * functions whose arguments PeTTa's translation evaluates before the call. */
static bool oem_state_call(const Atom *expr) {
    if (expr->kind != ATOM_EXPR || expr->expr.len == 0u ||
        expr->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    PeTTaForm form = petta_semantics_form(expr->expr.elems[0]->sym_id);
    return (expr->expr.len == 2u && form == PETTA_FORM_GET_STATE) ||
        (expr->expr.len == 3u && form == PETTA_FORM_CHANGE_STATE);
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
static bool oem_native_operation(SymbolId head, CettaExprLen arity) {
    CettaGsltPreparedPureIntrinsicInstruction instruction;
    CettaGsltPreparedPureIntrinsicOperandDiscipline discipline;
    return cetta_gslt_prepared_intrinsic_program(head, arity, &discipline,
                                                &instruction, &g_builtin_syms) &&
        discipline == CETTA_GSLT_PREPARED_PURE_INTRINSIC_OPERANDS_STRICT_ALL &&
        instruction == CETTA_GSLT_PREPARED_PURE_INTRINSIC_LIBRARY_VALUE_DISPATCH;
}

static uint32_t oem_host_fast_path(const OemCompile *compile, Atom *expr,
                                   const PettaPlanNode *plan,
                                   SymbolId *op_out) {
    *op_out = SYMBOL_ID_NONE;
    if (expr->kind != ATOM_EXPR || expr->expr.len == 0u)
        return OEM_HOST_PLAIN;
    if (plan->role == PETTA_PLAN_STATIC_CALL &&
        expr->expr.elems[0]->kind == ATOM_SYMBOL &&
        oem_native_operation(expr->expr.elems[0]->sym_id, expr->expr.len - 1u) &&
        oem_fields_are_values(expr, plan, 1u)) {
        *op_out = expr->expr.elems[0]->sym_id;
        return OEM_HOST_NATIVE;
    }
    if (plan->role == PETTA_PLAN_DYNAMIC_CALL &&
        expr->expr.elems[0]->kind == ATOM_VAR &&
        oem_fields_are_values(expr, plan, 1u))
        return OEM_HOST_DATA_HEAD;
    /* PeTTa's translation of `add-atom` evaluates the space and passes the
     * atom untranslated: the payload is data whatever its plan holds. */
    if (expr->expr.len == 3u &&
        atom_is_symbol_id(expr->expr.elems[0], g_builtin_syms.add_atom) &&
        oem_field_is_value(petta_plan_child(plan, 1u)) &&
        compile->host && compile->host->builtin_allowed &&
        compile->host->builtin_allowed(compile->host->context,
                                       g_builtin_syms.add_atom))
        return OEM_HOST_ADMIT;
    if (oem_state_call(expr) && oem_fields_are_values(expr, plan, 1u))
        return OEM_HOST_STATE;
    if (oem_observation_call(compile, expr, plan) &&
        oem_fields_are_values(expr, plan, 1u)) {
        *op_out = expr->expr.elems[0]->sym_id;
        return OEM_HOST_OBSERVE;
    }
    if (expr->expr.elems[0]->kind == ATOM_SYMBOL && expr->expr.len == 2u &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) ==
            PETTA_FORM_MSORT &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        oem_fields_are_values(expr, plan, 1u)) {
        *op_out = expr->expr.elems[0]->sym_id;
        return OEM_HOST_SORT;
    }
    PettaSequenceCallInterpretation projection = petta_plan_sequence_call(
        plan, atom_head_symbol_id(expr), expr->expr.len - 1u,
        CETTA_SEQUENCE_PETTA_VALUE);
    if (expr->expr.elems[0]->kind == ATOM_SYMBOL &&
        oem_list_native(expr->expr.elems[0]->sym_id, expr->expr.len - 1u) &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        (projection.call.projection != CETTA_SEQUENCE_PROJECTION_NONE
            ? projection.source_arguments_are_values
            : oem_fields_are_values(expr, plan, 1u))) {
        *op_out = expr->expr.elems[0]->sym_id;
        return projection.call.projection == CETTA_SEQUENCE_PROJECTION_HEAD
            ? OEM_HOST_SEQUENCE_HEAD
            : projection.call.projection == CETTA_SEQUENCE_PROJECTION_TAIL
                ? OEM_HOST_SEQUENCE_TAIL : OEM_HOST_LIST;
    }
    if (oem_metatype_call(compile, expr, plan) &&
        oem_fields_are_values(expr, plan, 1u)) {
        *op_out = g_builtin_syms.get_metatype;
        return OEM_HOST_METATYPE;
    }
    if (oem_member_call(compile, expr, plan) &&
        oem_fields_are_values(expr, plan, 1u)) {
        *op_out = expr->expr.elems[0]->sym_id;
        return OEM_HOST_MEMBER;
    }
    if (expr->expr.len == 3u &&
        atom_is_symbol_id(expr->expr.elems[0], g_builtin_syms.sealed_text)) {
        *op_out = g_builtin_syms.sealed_text;
        return OEM_HOST_SEALED;
    }
    /* The observation whether a match has a row, which the search machine
     * answers by membership where the space proves ground matching exact:
     * the region asks the space the same question, and leaves the goal
     * whole to the host where it cannot. */
    /* The observation reads the space natively, so the reference
     * evaluation (cetta_native_query_enabled) asks the host. */
    PeTTaExistenceView existence;
    if (cetta_native_query_enabled() &&
        petta_semantics_existence_view(
            expr, compile->host ? compile->host->reify_head : SYMBOL_ID_NONE,
            &existence) &&
        oem_existence_admitted(compile, expr, plan, &existence))
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
    if (!plan || !oem_host_goal_admitted(compile, expr, 0u))
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
    PeTTaExistenceView existence;
    if (node.host_fast == OEM_HOST_EXISTS &&
        petta_semantics_existence_view(
            expr, compile->host ? compile->host->reify_head : SYMBOL_ID_NONE,
            &existence))
        node.existence = (uint8_t)(
            (existence.observed == 2u ? OEM_EXISTS_SIDE2 : 0u) |
            (existence.counted ? OEM_EXISTS_COUNTED : 0u) |
            (existence.once ? OEM_EXISTS_ONCE : 0u) |
            (existence.negated ? OEM_EXISTS_NEGATED : 0u));
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
        return var_id_eq(atom->var_id, id) ? 1u : 0u;
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return 0u;
    uint64_t total = 0u;
    const uint64_t unknown = UINT64_MAX / 4u;
    for (CettaExprIndex child = 0u; child < atom->expr.len; child++) {
        uint64_t count = oem_var_occurrences(atom->expr.elems[child], id,
                                             depth + 1u);
        if (count >= unknown - total)
            return unknown;
        total += count;
    }
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
        var_id_is_none(binder->var_id) ||
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

/* Whether a node's output is a hole of its own, made for it alone. A failed
 * producer never fills its hole, so forwarding that destination is safe too. */
static bool oem_output_is_hole(const OemNode *node) {
    return node->kind == OEM_N_CALL || node->kind == OEM_N_PRIM ||
        node->kind == OEM_N_HOST || node->kind == OEM_N_APPLY ||
        node->kind == OEM_N_COLLECT || node->kind == OEM_N_ALTERNATIVES ||
        node->kind == OEM_N_ONCE || node->kind == OEM_N_CUT ||
        node->kind == OEM_N_FAIL;
}

/* A let whose pattern is one variable meets its value's output hole before
 * the value's goals run, which only makes the two one variable; the value
 * then fills the variable's slot itself. */
static void oem_let_fills_pattern(OemCompile *compile, uint32_t pattern,
                                  uint32_t value) {
    OemNode *node = &compile->nodes[value];
    if (node->kind == OEM_N_BUILTIN_ENTRY &&
        node->exposed == compile->nodes[node->body].exposed) {
        /* The guard and its native producer share the same output hole.
         * Preserve the ordinary let/producer slot fusion on both paths. */
        oem_let_fills_pattern(compile, pattern, node->body);
        node->exposed = compile->nodes[node->body].exposed;
        return;
    }
    if (node->kind == OEM_N_BUILTIN_ENTRY &&
        compile->program->templates[pattern].kind == OEM_T_SLOT) {
        /* The guard's separate result is a fresh hole, even when its native
         * body exposes a constructor or source variable. Pass the consumer's
         * destination directly to both authority paths. The native shape is
         * still imposed only after the guard selects the native body. */
        node->exposed = pattern;
        return;
    }
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
        !cetta_control_operation_is_control_form(plan->computation.control) &&
        plan->child_count == expr->expr.len && head_plan &&
        ((head->kind == ATOM_EXPR &&
          (head_plan->role == PETTA_PLAN_STATIC_CALL ||
           head_plan->role == PETTA_PLAN_DYNAMIC_CALL ||
           (head_plan->role == PETTA_PLAN_VALUE &&
            petta_semantics_runtime_callable_value(head)))) ||
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
    for (CettaExprIndex index = 0u; index < source->child_count; index++)
        children[index].computation.control = CETTA_CONTROL_RETURN_VALUE_PLAN;
    *root = (PettaPlanNode){
        .role = PETTA_PLAN_STATIC_CALL,
        .computation.control = CETTA_CONTROL_INVOKE_PLAN,
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
        CettaSequenceProjectionCall projection = petta_plan_sequence_call(
            plan, expr->expr.elems[0]->sym_id, expr->expr.len - 1u,
            CETTA_SEQUENCE_PETTA_VALUE).call;
        node.host_fast = projection.projection == CETTA_SEQUENCE_PROJECTION_HEAD
            ? (uint8_t)OEM_HOST_SEQUENCE_HEAD
            : projection.projection == CETTA_SEQUENCE_PROJECTION_TAIL
                ? (uint8_t)OEM_HOST_SEQUENCE_TAIL : (uint8_t)OEM_HOST_LIST;
        node.op = expr->expr.elems[0]->sym_id;
    } else if (oem_metatype_call(compile, expr, plan)) {
        /* And `get-metatype`. */
        node.host_fast = (uint8_t)OEM_HOST_METATYPE;
        node.op = g_builtin_syms.get_metatype;
    } else if (oem_member_call(compile, expr, plan)) {
        node.host_fast = (uint8_t)OEM_HOST_MEMBER;
        node.op = expr->expr.elems[0]->sym_id;
    } else if (oem_state_call(expr)) {
        /* And PeTTa's named state. */
        node.host_fast = (uint8_t)OEM_HOST_STATE;
        node.op = expr->expr.elems[0]->sym_id;
    } else if (oem_observation_call(compile, expr, plan)) {
        node.host_fast = (uint8_t)OEM_HOST_OBSERVE;
        node.op = expr->expr.elems[0]->sym_id;
    } else if (expr->expr.elems[0]->kind == ATOM_SYMBOL &&
               oem_native_operation(expr->expr.elems[0]->sym_id,
                                      expr->expr.len - 1u)) {
        node.host_fast = (uint8_t)OEM_HOST_NATIVE;
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
                seen = var_id_eq(vars[index]->var_id, atom->var_id);
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
    elems[0] = atom_symbol_canonical(program->spelling);
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
                      ((OemLocalSource){.lhs = lhs, .rhs = bodies[index],
                                         .plan = plans[index]}),
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
    if (!plan || !oem_host_goal_admitted(compile, expr, 0u))
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

/* A full typed call selects a fresh signature before evaluating any
 * argument. Its local equations keep those alternatives in declaration
 * order; each is lowered to argument operations, guards and a body call.
 * A partial signature keeps the machine's partial-application protocol. */
/* A trusted version's call to itself, as an ordinary relation call: its
 * arguments are evaluated where the protocol evaluates them, and its entry
 * check runs the source occurrence on the host if the program changed. */
static bool oem_build_trusted_call(OemCompile *compile, Atom *expr,
                                   const PettaPlanNode *plan, uint32_t depth,
                                   uint32_t relation, uint32_t *out) {
    CettaOpenEquationProgram *program = compile->program;
    OemNode node = {
        .kind = OEM_N_CALL, .recovers = plan->dispatch_handler,
        .check_entry = true, .protocol_skipped = true, .relation = relation,
    };
    if (!oem_reserve((void **)&program->host_plans, &program->host_plan_cap,
                     program->host_plan_len + 1u,
                     sizeof(*program->host_plans)) ||
        !oem_lower_pattern(compile, expr, NULL, depth + 1u, false,
                           &node.value))
        return oem_reject(compile, "call plan unavailable");
    node.call_plan = program->host_plan_len;
    program->host_plans[program->host_plan_len++] = plan;
    return oem_build_elements(compile, expr, plan, depth, 1u, &node.first,
                              &node.count) &&
        oem_hole(compile, &node.exposed) &&
        oem_node(compile, node, out);
}

static void oem_attach_trusted_version(CettaOpenEquationProgram *program,
                                       PettaProgram *petta,
                                       uint32_t relation_index);

static bool oem_build_typed_protocol(OemCompile *compile,
                                     const OemLocalSource *local,
                                     uint32_t *out);

/* A single signature whose domains require only raw retention or ordinary
 * translation and whose result has no guard has no rejecting branch. Its
 * preparation remains semantically relevant; its one-alternative dispatch
 * does not. */
static bool oem_identity_typed_protocol(const OemCompile *compile,
                                        const PettaTypeCall *call,
                                        bool hold_body) {
    if (!call || hold_body)
        return false;
    bool contracts = compile->host->call_contracts;
    if (call->guard_result &&
        !(contracts && call->result_type->kind == ATOM_VAR))
        return false;
    for (CettaExprIndex index = 0u; index < call->arity; index++) {
        PettaTypeArgument requirement =
            petta_type_call_argument(call, index + 1u, false);
        if (requirement.demand == PETTA_TYPE_TRANSLATE_AND_GUARD &&
            !(contracts && requirement.formal->kind == ATOM_VAR))
            return false;
    }
    return true;
}

static bool oem_build_typed_call(OemCompile *compile, Atom *expr,
                                 const PettaPlanNode *plan, uint32_t depth,
                                 bool *taken, uint32_t *out) {
    *taken = false;
    if (!compile->petta || !compile->host ||
        !compile->host->call_protocol_admitted ||
        !compile->host->call_protocol_admitted(compile->host->context,
            compile->program->space, expr->expr.elems[0]->sym_id,
            expr->expr.len - 1u))
        return true;
    CettaOpenEquationProgram *program = compile->program;
    if (!program->atoms_ready) {
        arena_init(&program->atoms);
        arena_set_hashcons(&program->atoms, NULL);
        program->atoms_ready = true;
    }
    PettaTypeCall *calls = NULL;
    uint32_t count = 0u;
    bool hold_body = false;
    if (!petta_program_type_calls(compile->petta, program->space,
            expr->expr.elems[0], expr->expr.len - 1u, &program->atoms,
            &calls, &count, &hold_body))
        return oem_reject(compile, "type family unavailable");
    if (!count) {
        free(calls);
        return true;
    }
    *taken = true;
    bool full = true;
    for (uint32_t index = 0u; index < count; index++)
        full = full && calls[index].arity == expr->expr.len - 1u;
    if (!full) {
        free(calls);
        return oem_build_host(compile, expr, plan, depth,
                              "partial typed call", out);
    }
    Atom **bodies = malloc(sizeof(*bodies) * count);
    const PettaPlanNode **plans = malloc(sizeof(*plans) * count);
    if (!bodies || !plans) {
        free(bodies);
        free(plans);
        free(calls);
        return oem_reject(compile, "out of memory");
    }
    for (uint32_t index = 0u; index < count; index++) {
        bodies[index] = expr;
        plans[index] = plan;
    }
    /* In a trusted version, a call the analysis discharged enters its
     * callee's version: the version's facts prove the callee's guards pass
     * exactly once and the callee's version proves its result. */
    if (program->compiling_trusted && count == 1u &&
        petta_contract_version_discharges(
            &program->relations[program->trusted_relation].version, expr)) {
        uint32_t callee = 0u;
        if (oem_relation_index(program, expr->expr.elems[0]->sym_id,
                               (uint32_t)(expr->expr.len - 1u), false,
                               &callee)) {
            oem_attach_trusted_version(program, compile->petta, callee);
            if (program->relations[callee].has_trusted) {
                uint32_t target = program->relations[callee].trusted;
                free(bodies);
                free(plans);
                free(calls);
                return oem_build_trusted_call(compile, expr, plan, depth,
                                              target, out);
            }
        }
    }
    /* In a trusted version, a call to itself meets the version's entry facts
     * (petta_contract_trusted_version): it skips the protocol and enters the
     * version directly, its arguments evaluated as the protocol would. */
    if (program->compiling_trusted && count == 1u &&
        program->relations[program->trusted_relation].head ==
            expr->expr.elems[0]->sym_id &&
        program->relations[program->trusted_relation].arity ==
            expr->expr.len - 1u) {
        free(bodies);
        free(plans);
        free(calls);
        return oem_build_trusted_call(compile, expr, plan, depth,
                                      program->trusted_relation, out);
    }
    if (count == 1u &&
        oem_identity_typed_protocol(compile, &calls[0], hold_body)) {
        OemLocalSource direct = {
            .rhs = expr, .plan = plan, .contract = calls[0], .typed = true,
            .identity_typed = true,
        };
        bool ok = oem_build_typed_protocol(compile, &direct, out);
        free(bodies);
        free(plans);
        free(calls);
        return ok;
    }
    bool ok = oem_build_local_control(compile, OEM_N_ALTERNATIVES, expr,
        plan, depth, bodies, plans, count, expr, out);
    if (ok) {
        uint32_t relation = compile->nodes[*out].local;
        uint32_t source = program->relations[relation].first_source;
        for (uint32_t index = 0u; index < count; index++) {
            OemLocalSource *local = &program->sources[source + index];
            local->contract = calls[index];
            local->typed = true;
            local->hold_body = hold_body;
            local->overload = count > 1u;
        }
    }
    free(bodies);
    free(plans);
    free(calls);
    return ok;
}

/* Whether `expr` is a control the region runs: PeTTa's `collapse`,
 * `superpose` and `once`; the host's materializer; and, where the host's
 * machine runs them and the profile offers them, `collect` and `select`. */
static bool oem_control_form(const OemCompile *compile, const Atom *expr,
                             const PettaPlanNode *plan) {
    SymbolId head = expr->expr.elems[0]->sym_id;
    uint32_t len = (uint32_t)expr->expr.len;
    if (plan->computation.control.operation == CETTA_CONTROL_COLLECT ||
        plan->computation.control.operation == CETTA_CONTROL_FIRST_SUCCESS)
        return true;
    if (len == 2u &&
        (plan->computation.control.operation == CETTA_CONTROL_ORDERED_CHOICE ||
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
    bool choice = plan->computation.control.operation == CETTA_CONTROL_ORDERED_CHOICE;
    bool collection = plan->computation.control.operation == CETTA_CONTROL_COLLECT;
    bool first = plan->computation.control.operation == CETTA_CONTROL_FIRST_SUCCESS;
    uint32_t limit = 0u;
    if (collection) {
        head = g_builtin_syms.collapse;
    } else if (first) {
        head = g_builtin_syms.once;
    } else if (choice) {
        head = g_builtin_syms.superpose;
    } else if (head == g_builtin_syms.select && expr->expr.len == 3u) {
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
    } else if (!choice &&
               head != g_builtin_syms.once) {
        head = g_builtin_syms.collapse;
    }
    CettaExprIndex body_index = choice || collection || first
        ? (CettaExprIndex)plan->computation.control.source + 1u
        : (CettaExprIndex)(expr->expr.len - 1u);
    if (body_index >= expr->expr.len ||
        ((choice || collection || first) && plan->computation.control.source == CETTA_CONTROL_OPERAND_ABSENT))
        return oem_reject(compile, "control source coordinate outside its operands");
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

/* A sequence composes the children's existing occurrence plans. Its output
 * is the selected child's exposed term, unified at the same frontier as
 * the ordinary translator; unused values are never evaluated again. Only
 * the final child of progn may inherit a tail position. No delimiter or
 * synthetic binder is introduced around the children. */
static bool oem_sequence(const Atom *expr, const PettaPlanNode *plan) {
    if (!expr || expr->kind != ATOM_EXPR || expr->expr.len < 2u ||
        expr->expr.elems[0]->kind != ATOM_SYMBOL || !plan ||
        plan->output != PETTA_PLAN_OUTPUT_CHILD)
        return false;
    CettaExprIndex result = cetta_control_operation_series_result(
        plan->computation.control, expr->expr.len - 1u);
    return result != 0u && plan->output_child == result;
}

static bool oem_build_sequence(OemCompile *compile, Atom *expr,
                               const PettaPlanNode *plan, uint32_t depth,
                               bool tail, uint32_t *out) {
    OemNode node = {.kind = OEM_N_SEQUENCE,
                    .count = (uint32_t)expr->expr.len - 1u,
                    .tail = tail && plan->computation.control.operation == CETTA_CONTROL_SEQUENCE_LAST};
    uint32_t *children = malloc(sizeof(*children) * node.count);
    if (!children)
        return oem_reject(compile, "out of memory");
    bool ok = true;
    for (uint32_t index = 0u; ok && index < node.count; index++) {
        bool last = node.tail && index + 1u == node.count;
        ok = (last ? oem_build_tail : oem_build_value)(
            compile, expr->expr.elems[index + 1u],
            petta_plan_child(plan, index + 1u), depth + 1u, &children[index]);
    }
    if (ok) {
        node.exposed = compile->nodes[children[plan->output_child - 1u]].exposed;
        ok = oem_node_children(compile, children, node.count, &node.first);
    }
    free(children);
    return ok && oem_node(compile, node, out);
}

static bool oem_build_bindings(OemCompile *compile, Atom *expr,
                               const PettaPlanNode *plan, uint32_t depth,
                               bool tail, uint32_t *out);

/* A source occurrence whose intrinsic implementation may acquire authored
 * alternatives. Only the extended host supplies this authority. */
static bool oem_builtin_source(const OemCompile *compile, const Atom *expr,
                                const PettaPlanNode *plan) {
    return compile->host && compile->host->builtin_equations && plan &&
        plan->role == PETTA_PLAN_STATIC_CALL && expr &&
        expr->kind == ATOM_EXPR && expr->expr.len > 0u &&
        expr->expr.elems[0]->kind == ATOM_SYMBOL &&
        petta_program_head_is_intrinsic(expr->expr.elems[0]->sym_id);
}

static bool oem_build_value_native(OemCompile *compile, Atom *expr,
                                   const PettaPlanNode *plan, uint32_t depth,
                                   uint32_t *out);
static bool oem_build_tail_native(OemCompile *compile, Atom *expr,
                                  const PettaPlanNode *plan, uint32_t depth,
                                  uint32_t *out);

/* Preserve the source before lowering its argument demands. The guard's stale
 * branch runs that source once and skips all of this occurrence's native work.
 * A fused control retains the entire control as its fallback source. */
static bool oem_guard_native_node(OemCompile *compile, Atom *expr,
                                   const PettaPlanNode *plan, uint32_t depth,
                                   uint32_t *out) {
    CettaOpenEquationProgram *program = compile->program;
    if (!oem_reserve((void **)&program->host_plans, &program->host_plan_cap,
                     program->host_plan_len + 1u,
                     sizeof(*program->host_plans)))
        return oem_reject(compile, "out of memory");
    OemNode entry = {.kind = OEM_N_BUILTIN_ENTRY, .body = *out,
        .relation = program->host_plan_len};
    program->host_plans[program->host_plan_len++] = plan;
    /* A native result already represented by a fresh temporary is an
     * unconstrained destination on either authority path. Reuse it rather
     * than introducing a second cell and an unnecessary unification.
     * Source variables and constructor templates still need a separate
     * hole: a replacement implementation can return a different shape. */
    uint32_t native_output = compile->nodes[entry.body].exposed;
    const OemTemplate *result = &program->templates[native_output];
    bool reuse = result->kind == OEM_T_SLOT && result->slot >= compile->var_len;
    if (reuse)
        entry.exposed = native_output;
    return oem_lower_pattern(compile, expr, NULL, depth + 1u, false,
                             &entry.value) &&
        (reuse || oem_hole(compile, &entry.exposed)) &&
        oem_node(compile, entry, out);
}

static bool oem_build_builtin_entry(OemCompile *compile, Atom *expr,
                                    const PettaPlanNode *plan, uint32_t depth,
                                    bool tail, uint32_t *out) {
    if (compile->host && compile->host->declared_builtin_protocol &&
        expr && expr->kind == ATOM_EXPR && expr->expr.len > 0u &&
        expr->expr.elems[0]->kind == ATOM_SYMBOL && plan &&
        plan->role != PETTA_PLAN_VALUE && plan->role != PETTA_PLAN_DATA &&
        is_grounded_op(expr->expr.elems[0]->sym_id) &&
        petta_semantics_form(expr->expr.elems[0]->sym_id) == PETTA_FORM_NONE &&
        space_head_declares_type(
            compile->program->space, expr->expr.elems[0]->sym_id)) {
        if (!oem_build_host(compile, expr, plan, depth, "declared builtin protocol", out))
            return false;
        compile->nodes[*out].host_fast = OEM_HOST_PLAIN;
        compile->nodes[*out].op = SYMBOL_ID_NONE;
        return true;
    }
    bool guarded = oem_builtin_source(compile, expr, plan);
    if (guarded && compile->host->builtin_equations(
            compile->host->context, compile->program->space,
            expr->expr.elems[0]->sym_id, expr->expr.len - 1u)) {
        if (!oem_build_host(compile, expr, plan, depth,
                            "extended builtin equations", out))
            return false;
        /* A host leaf must enumerate both branches; its scalar shortcut owns
         * only the intrinsic branch. */
        compile->nodes[*out].host_fast = OEM_HOST_PLAIN;
        compile->nodes[*out].op = SYMBOL_ID_NONE;
        return true;
    }
    /* `(reduce (f a…))` over a written application that carries the
     * run-time dispatch's handler is that application, as the search
     * machine runs it: the call keeps the handler, which recovers an error
     * its body raises, and its arguments run outside it. */
    if (!guarded && expr && expr->kind == ATOM_EXPR &&
        expr->expr.len == 2u && plan &&
        plan->role == PETTA_PLAN_STATIC_CALL && plan->child_count == 2u &&
        plan->computation.control.operation == CETTA_CONTROL_DISPATCH) {
        Atom *call = expr->expr.elems[1];
        const PettaPlanNode *call_plan = &plan->children[1];
        if (call->kind == ATOM_EXPR && call->expr.len > 0u &&
            call->expr.len == call_plan->child_count &&
            call_plan->role == PETTA_PLAN_STATIC_CALL &&
            call_plan->dispatch_handler &&
            call->expr.elems[0]->kind == ATOM_SYMBOL &&
            petta_semantics_runtime_head(call->expr.elems[0]->sym_id) ==
                PETTA_RUNTIME_HEAD_ORDINARY)
            return oem_build_builtin_entry(compile, call, call_plan,
                                           depth + 1u, tail, out);
    }
    return (tail ? oem_build_tail_native : oem_build_value_native)(
            compile, expr, plan, depth, out) &&
        (!guarded || compile->nodes[*out].kind == OEM_N_BUILTIN_ENTRY ||
         oem_guard_native_node(compile, expr, plan, depth, out));
}

static bool oem_build_value(OemCompile *compile, Atom *expr,
                            const PettaPlanNode *plan, uint32_t depth,
                            uint32_t *out) {
    return oem_build_builtin_entry(compile, expr, plan, depth, false, out);
}

static bool oem_build_tail(OemCompile *compile, Atom *expr,
                           const PettaPlanNode *plan, uint32_t depth,
                           uint32_t *out) {
    return oem_build_builtin_entry(compile, expr, plan, depth, true, out);
}

/* Only the caller's already-value native comparison grants exclusivity.
 * Keep the fence on this compilation stack, never in deferred equations. */
static bool oem_build_selected_branch(OemCompile *compile, Atom *expr,
        const PettaPlanNode *plan, uint32_t depth, const Atom *inactive,
        uint32_t *out) {
    OemInactiveBranch fence = {.source = inactive,
        .previous = compile->inactive};
    if (inactive)
        compile->inactive = &fence;
    bool ok = oem_build_tail(compile, expr, plan, depth, out);
    compile->inactive = fence.previous;
    return ok;
}

/* A value position: a variable, a literal, a constructor over values, a
 * relation call or a primitive. */
static bool oem_build_failure(OemCompile *compile, uint32_t *out) {
    OemNode node = {.kind = OEM_N_FAIL};
    return oem_hole(compile, &node.exposed) && oem_node(compile, node, out);
}

static bool oem_build_value_native(OemCompile *compile, Atom *expr,
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
    if (plan->computation.control.operation == CETTA_CONTROL_FAILURE)
        return oem_build_failure(compile, out);
    if (oem_sequence(expr, plan))
        return oem_build_sequence(compile, expr, plan, depth, false, out);
    if ((cetta_control_operation_binding_layout(plan->computation.control, 1u) && expr->expr.len == 4u) ||
        (plan->computation.control.operation == CETTA_CONTROL_SEQUENCE_MANY && expr->expr.len == 3u))
        return oem_build_bindings(compile, expr, plan, depth, false, out);
    bool symbol_head = expr->expr.elems[0]->kind == ATOM_SYMBOL;
    if (symbol_head && expr->expr.len == 4u &&
        expr->expr.elems[0]->sym_id == g_builtin_syms.match)
        return oem_build_match(compile, expr, plan, depth, false, out);
    /* PeTTa's `(cut)` is Prolog's `!`, as its translation reads it, whatever
     * equations the program has for `cut`: in an equation's own body it
     * commits the call to that equation, and its value is the truth
     * value. */
    if (plan->computation.control.operation == CETTA_CONTROL_SCOPED_COMMIT) {
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
    if (symbol_head && oem_control_form(compile, expr, plan))
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
                cetta_control_operation_binding_layout(lowered_plan->computation.control, 1u))
                return oem_build_local_control(compile, OEM_N_ALTERNATIVES,
                                               expr, plan, depth, &lowered,
                                               &lowered_plan, 1u, expr, out);
        }
    }
    /* An `if` whose output is a value runs as a local relation whose one
     * equation is the `if` in a tail position: each of its answers
     * continues after it, as each of the host's does. */
    if (symbol_head && plan->computation.control.operation == CETTA_CONTROL_BRANCH &&
        expr->expr.len == 4u)
        return oem_build_local_control(compile, OEM_N_ALTERNATIVES, expr,
                                       plan, depth, &expr, &plan, 1u, expr,
                                       out);
    /* These intrinsics precede authored equations in the machine. Compute
     * their arguments once; integer min/max and structural observations
     * stay native, while other min/max domains use the prepared leaf. */
    if (symbol_head &&
        (oem_observation_call(compile, expr, plan) ||
         (plan->role == PETTA_PLAN_STATIC_CALL && expr->expr.len == 3u &&
          (expr->expr.elems[0]->sym_id == g_builtin_syms.petta_min ||
           expr->expr.elems[0]->sym_id == g_builtin_syms.petta_max) &&
          oem_pure_operation(compile, expr->expr.elems[0]->sym_id))))
        return oem_build_apply(compile, expr, plan, depth, out);
    /* A full-arity grounded spelling undefined by this dialect is data
     * until an authored function registers it. Partial applications keep
     * the machine's arity protocol. Its children retain their plans, and
     * registration/mutation invalidates the enclosing program. */
    bool undefined = symbol_head && expr->expr.len == 3u && compile->petta &&
        petta_semantics_grounded_undefined(expr->expr.elems[0]->sym_id) &&
        !petta_program_function_registered(
            compile->petta, compile->program->space,
            expr->expr.elems[0]->sym_id);
    if (!undefined && symbol_head && oem_relation_call(expr, plan)) {
        bool typed = false;
        if (!oem_build_typed_call(compile, expr, plan, depth, &typed, out))
            return false;
        if (typed)
            return true;
    }
    if (!undefined && symbol_head && oem_relation_call(expr, plan) &&
        compile->host && compile->host->relation_admitted &&
        !compile->host->relation_admitted(
            compile->host->context, compile->program->space,
            expr->expr.elems[0]->sym_id, expr->expr.len - 1u))
        return oem_build_host(compile, expr, plan, depth,
                              "relation owned by the host", out);
    if (!undefined && symbol_head && oem_relation_call(expr, plan)) {
        node.kind = OEM_N_CALL;
        node.recovers = plan->dispatch_handler;
        node.check_entry = true;
        CettaOpenEquationProgram *program = compile->program;
        if (!oem_reserve((void **)&program->host_plans,
                         &program->host_plan_cap,
                         program->host_plan_len + 1u,
                         sizeof(*program->host_plans)) ||
            !oem_lower_pattern(compile, expr, NULL, depth + 1u, false,
                               &node.value))
            return oem_reject(compile, "call plan unavailable");
        node.call_plan = program->host_plan_len;
        program->host_plans[program->host_plan_len++] = plan;
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
     * `get-metatype`, or the named state, over computed arguments applies to
     * their values, which the region computes, collections included; a
     * counted `length` keeps the host's counted route. */
    bool computed_call = symbol_head &&
        plan->role == PETTA_PLAN_STATIC_CALL &&
        !oem_fields_are_values(expr, plan, 1u);
    /* Keep a cardinality consumer with its producer. Splitting it into
     * eagerly computed fields materializes a collection the host can count
     * without retaining its items. Value arguments still use the native
     * observer below. */
    if (computed_call && expr->expr.len == 2u &&
        petta_semantics_count_consumer(expr->expr.elems[0]->sym_id) !=
            PETTA_COUNT_CONSUMER_NONE)
        return oem_build_host(compile, expr, plan, depth,
                              "collection cardinality observer", out);
    bool value_native = computed_call &&
        ((oem_list_native(expr->expr.elems[0]->sym_id,
                          expr->expr.len - 1u) &&
          petta_semantics_form(expr->expr.elems[0]->sym_id) !=
              PETTA_FORM_LENGTH) ||
         oem_metatype_call(compile, expr, plan) ||
         oem_member_call(compile, expr, plan) || oem_state_call(expr) ||
         oem_observation_call(compile, expr, plan));
    /* A callee reached through a variable decides its arguments' demand.
     * Where no declaration of the space can make a callee of this many
     * arguments hold one as written, or evaluate it once per declaration,
     * applying the head's value to evaluated arguments is the callee's call;
     * otherwise the host makes the call from the written arguments. */
    if (!undefined && oem_apply_call(expr, plan) &&
        expr->expr.elems[0]->kind == ATOM_VAR &&
        !oem_fields_are_values(expr, plan, 1u)) {
        CettaOpenEquationProgram *program = compile->program;
        if (!program->callee_source_arity_known) {
            program->callee_source_arity =
                petta_contract_callee_source_arity(program->space);
            program->callee_source_arity_known = true;
        }
        if (expr->expr.len - 1u <= program->callee_source_arity)
            return oem_build_host(compile, expr, plan, depth,
                                  "callee may receive its arguments as written",
                                  out);
        program->assumes_value_callees = true;
    }
    if (!undefined && (oem_apply_call(expr, plan) || value_native ||
        (symbol_head && plan->role == PETTA_PLAN_STATIC_CALL &&
         ((expr->expr.len == 2u &&
           petta_semantics_form(expr->expr.elems[0]->sym_id) ==
               PETTA_FORM_MSORT) ||
          (oem_pure_operation(compile, expr->expr.elems[0]->sym_id) &&
           !oem_fields_are_values(expr, plan, 1u) &&
           !petta_semantics_existence_view(
               expr, compile->host ? compile->host->reify_head
                                   : SYMBOL_ID_NONE, NULL))))))
        return oem_build_apply(compile, expr, plan, depth, out);
    if (!undefined && !oem_data_node(expr, plan))
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

/* The same binding construction in tail and value positions: a producer
 * meets its pattern before its goals, and the body exposes its own value.
 * Only the body's continuation differs; neither position adds a cut scope. */
static bool oem_build_bindings(OemCompile *compile, Atom *expr,
                               const PettaPlanNode *plan, uint32_t depth,
                               bool tail, uint32_t *out) {
    if (compile->host && compile->host->preserve_collection_observer &&
        compile->host->preserve_collection_observer(
            compile->host->context, expr, plan))
        return oem_build_host(compile, expr, plan, depth,
                              "collection producer/consumer observation", out);
    OemNode node = {0};
    if (cetta_control_operation_binding_layout(plan->computation.control, 1u)) {
        if (expr->expr.len != 4u)
            return oem_reject(compile, "let arity");
        CettaExprIndex producer = (CettaExprIndex)plan->computation.control.source + 1u;
        CettaExprIndex pattern = (CettaExprIndex)plan->computation.control.first + 1u;
        CettaExprIndex body = (CettaExprIndex)plan->computation.control.last + 1u;
        node.kind = OEM_N_LET;
        const PettaPlanNode *body_plan = petta_plan_child(plan, body);
        bool counted = oem_count_only_binder(
            compile, expr->expr.elems[pattern], expr->expr.elems[producer], NULL, 0u,
            expr->expr.elems[body],
            !body_plan || body_plan->contains_cardinality_call);
        compile->let_scope = expr;
        compile->let_before = NULL;
        compile->let_before_len = 0u;
        bool lowered = oem_lower_pattern(compile, expr->expr.elems[pattern],
                                         petta_plan_child(plan, pattern),
                                         depth + 1u, true, &node.pattern);
        compile->let_scope = NULL;
        if (!lowered ||
            !oem_build_producer(compile, expr->expr.elems[producer],
                                petta_plan_child(plan, producer), depth + 1u,
                                counted, &node.value) ||
            !(tail ? oem_build_tail : oem_build_value)(
                compile, expr->expr.elems[body], petta_plan_child(plan, body),
                depth + 1u, &node.body))
            return false;
        bool late = false;
        if (!oem_pattern_output_independent(compile, expr->expr.elems[pattern],
                petta_plan_child(plan, pattern),
                compile->nodes[node.body].exposed, 0u, &late))
            return false;
        oem_let_fills_pattern(compile, node.pattern, node.value);
        node.exposed = compile->nodes[node.body].exposed;
        if (late && !oem_late_output(compile, &node))
            return false;
        return oem_node(compile, node, out);
    }
    if (plan->computation.control.operation == CETTA_CONTROL_SEQUENCE_MANY) {
        /* Sequential lets, the last binding innermost. */
        Atom *bindings = expr->expr.len == 3u ? expr->expr.elems[1] : NULL;
        const PettaPlanNode *bindings_plan = petta_plan_child(plan, 1u);
        if (!bindings || bindings->kind != ATOM_EXPR || !bindings_plan)
            return oem_reject(compile, "let* bindings");
        uint32_t body = 0u;
        const PettaPlanNode *body_plan = petta_plan_child(plan, 2u);
        if (!(tail ? oem_build_tail : oem_build_value)(
                compile, expr->expr.elems[2], body_plan, depth + 1u, &body))
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
            bool late = false;
            if (!lowered ||
                !oem_pattern_output_independent(compile, pair->expr.elems[0],
                    petta_plan_child(pair_plan, 0u),
                    compile->nodes[body].exposed, 0u, &late) ||
                !oem_build_producer(compile, pair->expr.elems[1],
                                    petta_plan_child(pair_plan, 1u),
                                    depth + 1u, counted, &let.value))
                return false;
            oem_let_fills_pattern(compile, let.pattern, let.value);
            let.exposed = compile->nodes[body].exposed;
            if ((late && !oem_late_output(compile, &let)) ||
                !oem_node(compile, let, &body))
                return false;
        }
        *out = body;
        return true;
    }
    return oem_reject(compile, "not a binding control");
}

static bool oem_snapshot_let(const Atom *expr);

/* A tail position: the construct's output is the activation's output. */
static bool oem_build_tail_native(OemCompile *compile, Atom *expr,
                           const PettaPlanNode *plan, uint32_t depth,
                           uint32_t *out) {
    if (!expr || depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "expression too deep");
    if (!(expr->kind == ATOM_EXPR && expr->expr.len > 0u &&
          expr->expr.elems[0]->kind == ATOM_SYMBOL && plan))
        return oem_build_value(compile, expr, plan, depth, out);
    SymbolId head = expr->expr.elems[0]->sym_id;
    OemNode node = {0};
    if (plan->computation.control.operation == CETTA_CONTROL_FAILURE)
        return oem_build_failure(compile, out);
    if (oem_sequence(expr, plan))
        return oem_build_sequence(compile, expr, plan, depth, true, out);
    if (plan->computation.control.operation == CETTA_CONTROL_BRANCH) {
        if (expr->expr.len != 4u)
            return oem_reject(compile, "if arity");
        CettaExprIndex producer = (CettaExprIndex)plan->computation.control.source + 1u;
        CettaExprIndex when_true = (CettaExprIndex)plan->computation.control.first + 1u;
        CettaExprIndex when_false = (CettaExprIndex)plan->computation.control.last + 1u;
        Atom *test = expr->expr.elems[producer];
        const PettaPlanNode *test_plan = petta_plan_child(plan, producer);
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
            !petta_semantics_existence_view(
                test, compile->host ? compile->host->reify_head
                                    : SYMBOL_ID_NONE, NULL);
        bool guarded = comparison &&
            oem_builtin_source(compile, test, test_plan);
        if (guarded && compile->host->builtin_equations(
                compile->host->context, compile->program->space,
                test->expr.elems[0]->sym_id, 2u)) {
            comparison = false;
            guarded = false;
        }
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
        bool exclusive = comparison &&
            oem_fields_are_values(test, test_plan, 1u);
        return ok &&
            oem_build_selected_branch(compile, expr->expr.elems[when_true],
                petta_plan_child(plan, when_true), depth + 1u,
                exclusive ? expr->expr.elems[when_false] : NULL, &node.then_node) &&
            oem_build_selected_branch(compile, expr->expr.elems[when_false],
                petta_plan_child(plan, when_false), depth + 1u,
                exclusive ? expr->expr.elems[when_true] : NULL, &node.else_node) &&
            oem_hole(compile, &node.exposed) &&
            oem_node(compile, node, out) &&
            (!guarded || oem_guard_native_node(
                compile, expr, plan, depth, out));
    }
    if (cetta_control_operation_binding_layout(plan->computation.control, 1u) &&
        expr->expr.len == 4u && oem_snapshot_let(expr))
        return oem_build_host(compile, expr, plan, depth,
                              "collect-then-superpose region", out);
    if ((cetta_control_operation_binding_layout(plan->computation.control, 1u) && expr->expr.len == 4u) ||
        (plan->computation.control.operation == CETTA_CONTROL_SEQUENCE_MANY && expr->expr.len == 3u))
        return oem_build_bindings(compile, expr, plan, depth, true, out);
    if (cetta_control_operation_is_control_form(plan->computation.control) &&
        plan->computation.control.operation != CETTA_CONTROL_ORDERED_CHOICE &&
        plan->computation.control.operation != CETTA_CONTROL_COLLECT &&
        plan->computation.control.operation != CETTA_CONTROL_FIRST_SUCCESS &&
        plan->computation.control.operation != CETTA_CONTROL_FAILURE &&
        plan->computation.control.operation != CETTA_CONTROL_SCOPED_COMMIT &&
        plan->computation.control.operation != CETTA_CONTROL_MATCH_BRANCHES &&
        !oem_relation_call(expr, plan))
        return oem_build_host(compile, expr, plan, depth,
                              "control outside the fragment", out);
    if (head == g_builtin_syms.match && expr->expr.len == 4u)
        return oem_build_match(compile, expr, plan, depth, true, out);
    if (plan->computation.control.operation == CETTA_CONTROL_MATCH_BRANCHES)
        return oem_build_case(compile, expr, plan, depth, out);
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
    else if (compile->nodes[*out].kind == OEM_N_SEQUENCE &&
             compile->nodes[*out].identity_typed &&
             compile->nodes[*out].count > 0u) {
        OemNode *sequence = &compile->nodes[*out];
        uint32_t last = compile->node_children[
            sequence->first + sequence->count - 1u];
        if (compile->nodes[last].kind == OEM_N_CALL &&
            !compile->nodes[last].recovers)
            compile->nodes[last].tail = true;
    }
    return true;
}

/* `(case key ((pattern branch) ...))`: PeTTa's first-match choice.  Each
 * answer of the key meets the patterns in order, by unification; the first
 * that unifies commits to its branch, whose answers are the case's, and a
 * key no pattern admits fails.  A pattern is read as a head is, a `cons` in
 * it as a list pattern; one with a relational occurrence, which PeTTa
 * evaluates first, stays with the host, as does the `Empty` default, which
 * observes whether the key has an answer at all. */
/* `(if (== () (collapse K)) A B)` for the case EXPR,
 * `(case K ((Empty A) ($v B)))` with K `(once (match ...))`, the arms in
 * either order and $v in neither branch: once answers at most one of the
 * match's answers, so the case takes A when it has none and B when it has
 * one, as the if does.  No authored equation may extend case, if, `==`,
 * collapse, once or match, which the host would run.  NULL otherwise. */
static Atom *oem_existence_case_if(OemCompile *compile, Atom *expr,
                                   CettaExprIndex key_child,
                                   CettaExprIndex arms_child) {
    Atom *key = expr->expr.elems[key_child];
    Atom *arms = expr->expr.elems[arms_child];
    /* The reference evaluation (cetta_native_query_enabled) reads the case
     * as written. */
    if (!cetta_native_query_enabled() ||
        key->kind != ATOM_EXPR || key->expr.len != 2u ||
        !atom_is_symbol_id(key->expr.elems[0], g_builtin_syms.once) ||
        key->expr.elems[1]->kind != ATOM_EXPR ||
        key->expr.elems[1]->expr.len != 4u ||
        !atom_is_symbol_id(key->expr.elems[1]->expr.elems[0],
                           g_builtin_syms.match) ||
        arms->kind != ATOM_EXPR || arms->expr.len != 2u)
        return NULL;
    Atom *empty = NULL;
    Atom *other = NULL;
    for (CettaExprIndex index = 0u; index < 2u; index++) {
        Atom *arm = arms->expr.elems[index];
        if (arm->kind != ATOM_EXPR || arm->expr.len != 2u)
            return NULL;
        if (atom_is_symbol_id(arm->expr.elems[0], g_builtin_syms.empty))
            empty = arm;
        else
            other = arm;
    }
    if (!empty || !other || other->expr.elems[0]->kind != ATOM_VAR ||
        oem_var_occurrences(empty->expr.elems[1],
                            other->expr.elems[0]->var_id, 0u) ||
        oem_var_occurrences(other->expr.elems[1],
                            other->expr.elems[0]->var_id, 0u))
        return NULL;
    /* The if reads `==` and collapse, which the case does not: an
     * authored equation extending either would answer beside them. */
    static const struct { SymbolId *head; uint32_t arity; } intrinsics[] = {
        {&g_builtin_syms.case_text, 2u}, {&g_builtin_syms.if_text, 3u},
        {&g_builtin_syms.op_eq, 2u}, {&g_builtin_syms.collapse, 1u},
        {&g_builtin_syms.once, 1u}, {&g_builtin_syms.match, 3u},
    };
    for (size_t index = 0u;
         compile->host && compile->host->builtin_equations &&
         index < sizeof(intrinsics) / sizeof(intrinsics[0]);
         index++) {
        if (compile->host->builtin_equations(
                compile->host->context, compile->program->space,
                *intrinsics[index].head, intrinsics[index].arity))
            return NULL;
    }
    CettaOpenEquationProgram *program = compile->program;
    if (!program->atoms_ready) {
        arena_init(&program->atoms);
        arena_set_hashcons(&program->atoms, NULL);
        program->atoms_ready = true;
    }
    Arena *atoms = &program->atoms;
    Atom *collapse = atom_expr2(
        atoms, atom_symbol_canonical(g_builtin_syms.collapse), key);
    Atom *test = collapse
        ? atom_expr3(atoms, atom_symbol_canonical(g_builtin_syms.op_eq),
                     atom_unit_canonical(), collapse)
        : NULL;
    Atom *elems[4] = {
        atom_symbol_canonical(g_builtin_syms.if_text), test,
        empty->expr.elems[1], other->expr.elems[1],
    };
    return test && elems[0] ? atom_expr(atoms, elems, 4u) : NULL;
}

static bool oem_build_case(OemCompile *compile, Atom *expr,
                           const PettaPlanNode *plan, uint32_t depth,
                           uint32_t *out) {
    CettaControlOperationPlan control = plan->computation.control;
    CettaExprLen arity = expr->expr.len - 1u;
    if (!cetta_control_operation_match_branches_layout(control, arity))
        return oem_reject(compile, "invalid case operand plan");
    CettaExprIndex key_child = (CettaExprIndex)control.source + 1u;
    CettaExprIndex branches_child = (CettaExprIndex)control.first + 1u;
    Atom *arms = expr->expr.elems[branches_child];
    const PettaPlanNode *arms_plan = petta_plan_child(plan, branches_child);
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
    if (!data) {
        Atom *test = oem_existence_case_if(compile, expr, key_child,
                                           branches_child);
        const PettaPlanNode *test_plan = test && compile->petta
            ? petta_program_plan_transient(compile->petta, test, NULL)
            : NULL;
        if (test_plan)
            return oem_build_tail_native(compile, test, test_plan, depth,
                                         out);
        return oem_build_host(compile, expr, plan, depth,
                              "case outside the fragment", out);
    }
    OemNode node = {.kind = OEM_N_CASE, .count = 2u * arms->expr.len};
    uint32_t *children = malloc(sizeof(*children) *
                                (node.count ? node.count : 1u));
    if (!children)
        return oem_reject(compile, "out of memory");
    bool ok = oem_build_value(compile, expr->expr.elems[key_child],
                              petta_plan_child(plan, key_child), depth + 1u,
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
/* A term as a template over the slots its variables already have. */
static bool oem_lower_known(OemCompile *compile, Atom *atom, uint32_t depth,
                            uint32_t *out) {
    if (depth > OEM_MAX_DEPTH)
        return oem_reject(compile, "expression too deep");
    if (atom->kind == ATOM_VAR) {
        uint32_t slot = 0u;
        return oem_slot_of(compile, atom->var_id, &slot) &&
            oem_template_slot(compile, slot, out);
    }
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return oem_template_literal(compile, atom, out);
    uint32_t inline_children[8];
    uint32_t *children = atom->expr.len <= 8u
        ? inline_children
        : malloc(sizeof(*children) * atom->expr.len);
    if (!children)
        return oem_reject(compile, "out of memory");
    bool ok = true;
    for (CettaExprIndex index = 0u; ok && index < atom->expr.len; index++)
        ok = oem_lower_known(compile, atom->expr.elems[index], depth + 1u,
                             &children[index]);
    ok = ok && oem_template_build(compile, children,
                                  (uint32_t)atom->expr.len, out);
    if (children != inline_children)
        free(children);
    return ok;
}

/* Under `evaluates_instances`, a match leg's own source, `(match space
 * pattern template)` with this leg's patterns as a conjunction, and its
 * exposed output, as the leg's fallback; its index plus one goes in the
 * leg's node. */
static bool oem_match_fallback(OemCompile *compile, Atom *space,
                               Atom *const *patterns, uint32_t count,
                               Atom *template, bool tail, uint32_t exposed,
                               uint32_t *index_out) {
    CettaOpenEquationProgram *program = compile->program;
    if (!program->atoms_ready) {
        arena_init(&program->atoms);
        arena_set_hashcons(&program->atoms, NULL);
        program->atoms_ready = true;
    }
    Atom *pattern = patterns[0];
    if (count > 1u) {
        Atom **conjuncts = arena_alloc(&program->atoms,
                                       sizeof(*conjuncts) * (count + 1u));
        if (!conjuncts)
            return oem_reject(compile, "out of memory");
        conjuncts[0] = atom_symbol_id(&program->atoms, g_builtin_syms.comma);
        memcpy(conjuncts + 1u, patterns, sizeof(*conjuncts) * count);
        pattern = conjuncts[0]
            ? atom_expr(&program->atoms, conjuncts, count + 1u) : NULL;
    }
    Atom *head = atom_symbol_id(&program->atoms, g_builtin_syms.match);
    Atom *parts[4] = {head, space, pattern, template};
    Atom *source = head && pattern
        ? atom_expr(&program->atoms, parts, 4u) : NULL;
    uint32_t lowered = 0u;
    if (!source)
        return oem_reject(compile, "out of memory");
    if (!oem_lower_known(compile, source, 0u, &lowered))
        return false;
    if (!oem_reserve((void **)&program->match_fallbacks,
                     &program->match_fallback_cap,
                     program->match_fallback_len + 1u,
                     sizeof(*program->match_fallbacks)))
        return oem_reject(compile, "out of memory");
    program->match_fallbacks[program->match_fallback_len].source = lowered;
    program->match_fallbacks[program->match_fallback_len].exposed = exposed;
    program->match_fallbacks[program->match_fallback_len].tail = tail;
    program->match_fallbacks[program->match_fallback_len].legs = count;
    *index_out = ++program->match_fallback_len;
    return true;
}

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
    if (compile->host && compile->host->evaluates_instances &&
        !oem_match_fallback(compile, space, patterns, count, template, tail,
                            node.exposed, &node.relation))
        return false;
    return oem_node(compile, node, out);
}

/* ATOM with VALUE for VAR. */
static Atom *oem_var_substitute(Arena *arena, Atom *atom, VarId var,
                                Atom *value) {
    if (atom->kind == ATOM_VAR)
        return var_id_eq(atom->var_id, var) ? value : atom;
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
        return atom;
    Atom **elems = arena_alloc(arena, sizeof(*elems) * (size_t)atom->expr.len);
    if (!elems)
        return NULL;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        elems[i] = oem_var_substitute(arena, atom->expr.elems[i], var, value);
        if (!elems[i])
            return NULL;
    }
    return atom_expr(arena, elems, atom->expr.len);
}

/* `(let $L (collapse (match S P T)) BODY)`, BODY reading $L once, as the
 * list `(let PAT (superpose $L) E)` spreads, alone or under a collapse or
 * its count: a region the host's machine reads on a snapshot. */
static bool oem_snapshot_let(const Atom *expr) {
    Atom *binder = expr->expr.elems[1];
    Atom *source = expr->expr.elems[2];
    Atom *body = expr->expr.elems[3];
    if (binder->kind != ATOM_VAR || source->kind != ATOM_EXPR ||
        source->expr.len != 2u ||
        !atom_is_symbol_id(source->expr.elems[0], g_builtin_syms.collapse) ||
        source->expr.elems[1]->kind != ATOM_EXPR ||
        source->expr.elems[1]->expr.len != 4u ||
        !atom_is_symbol_id(source->expr.elems[1]->expr.elems[0],
                           g_builtin_syms.match) ||
        body->kind != ATOM_EXPR)
        return false;
    const Atom *spread = body;
    if (body->expr.len == 2u &&
        (atom_is_symbol_id(body->expr.elems[0], g_builtin_syms.size_atom) ||
         atom_is_symbol_id(body->expr.elems[0], g_builtin_syms.collapse))) {
        spread = body->expr.elems[1];
        if (atom_is_symbol_id(body->expr.elems[0], g_builtin_syms.size_atom)) {
            if (spread->kind != ATOM_EXPR || spread->expr.len != 2u ||
                !atom_is_symbol_id(spread->expr.elems[0],
                                   g_builtin_syms.collapse))
                return false;
            spread = spread->expr.elems[1];
        }
    }
    return spread->kind == ATOM_EXPR && spread->expr.len == 4u &&
        atom_is_symbol_id(spread->expr.elems[0], g_builtin_syms.let) &&
        spread->expr.elems[2]->kind == ATOM_EXPR &&
        spread->expr.elems[2]->expr.len == 2u &&
        atom_is_symbol_id(spread->expr.elems[2]->expr.elems[0],
                          g_builtin_syms.superpose) &&
        spread->expr.elems[2]->expr.elems[1]->kind == ATOM_VAR &&
        var_id_eq(spread->expr.elems[2]->expr.elems[1]->var_id,
                  binder->var_id) &&
        oem_var_occurrences(body, binder->var_id, 0u) == 1u;
}

/* `(match space pattern template)`: the template's code runs once per row
 * of the space that unifies with the pattern.  A conjunction `(, p1 p2 ...)`
 * matches its patterns in turn, as the nested matches equation search
 * evaluates it by. */
static bool oem_build_match(OemCompile *compile, Atom *expr,
                            const PettaPlanNode *plan, uint32_t depth,
                            bool tail, uint32_t *out) {
    Atom *pattern = expr->expr.elems[2];
    Atom *template = expr->expr.elems[3];
    const PettaPlanNode *template_plan = plan ? petta_plan_child(plan, 3u)
                                              : NULL;
    /* `(match S Q[$x] (let P $x B))`, $x occurring nowhere else in the
     * equation: the let unifies P with the row's part at $x, as matching Q
     * with P there does, so the pattern reads P and the template is B.  The
     * reference evaluation (cetta_native_query_enabled) reads the equation
     * as written. */
    if (cetta_native_query_enabled() &&
        template->kind == ATOM_EXPR && template->expr.len == 4u &&
        template_plan &&
        cetta_control_operation_binding_layout(
            template_plan->computation.control, 1u) &&
        template->expr.elems[2]->kind == ATOM_VAR &&
        !petta_semantics_contains_cons_constraint(template->expr.elems[1])) {
        VarId var = template->expr.elems[2]->var_id;
        if (!compile->program->atoms_ready) {
            arena_init_detached(&compile->program->atoms);
            compile->program->atoms_ready = true;
        }
        Atom *refined =
            oem_var_occurrences(pattern, var, 0u) == 1u &&
                    oem_var_occurrences(compile->lhs, var, 0u) +
                            oem_var_occurrences(compile->rhs, var, 0u) == 2u
                ? oem_var_substitute(&compile->program->atoms, pattern, var,
                                     template->expr.elems[1])
                : NULL;
        if (refined) {
            pattern = refined;
            template = template->expr.elems[3];
            template_plan = petta_plan_child(template_plan, 3u);
        }
    }
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
        connective ? pattern->expr.elems + 1u : &pattern,
        connective ? pattern->expr.len - 1u : 1u, template,
        template_plan, depth, tail, connective, out);
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
    case OEM_N_BUILTIN_ENTRY: {
        uint32_t entry = 0u;
        bool ok = oem_emit(compile, (OemStep){
                .kind = OEM_S_BUILTIN_ENTRY, .relation = node.relation,
                .value = node.value, .pattern = node.exposed,
            }, &entry) &&
            (node.exposed == compile->nodes[node.body].exposed ||
             oem_emit(compile, (OemStep){.kind = OEM_S_BIND,
                .pattern = node.exposed,
                .value = compile->nodes[node.body].exposed}, NULL)) &&
            oem_emit_goals(compile, node.body);
        if (ok)
            compile->program->steps[entry].target = compile->program->step_len;
        return ok;
    }
    case OEM_N_LET:
        return (node.pattern == compile->nodes[node.value].exposed ||
                oem_emit(compile, (OemStep){
                             .kind = OEM_S_BIND, .pattern = node.pattern,
                             .value = compile->nodes[node.value].exposed,
                         }, NULL)) &&
            oem_emit_goals(compile, node.value) &&
            oem_emit_goals(compile, node.body) &&
            (!node.late_output || oem_emit_late_binds(compile, &node));
    case OEM_N_SEQUENCE:
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
        uint32_t entry = 0u;
        if (node.check_entry &&
            !oem_emit(compile, (OemStep){
                .kind = OEM_S_ENTRY, .relation = node.call_plan,
                .op = compile->program->relations[node.relation].head,
                .arg_count = node.count,
                .value = node.value, .pattern = node.exposed,
                .recovers = node.recovers,
                .type_dead = node.identity_typed,
            }, &entry))
            return false;
        if (!oem_emit_element_goals(compile, &node, &first_arg) ||
            (node.kind == OEM_N_PRIM &&
             !oem_op_literal(compile, node.op, &head)))
            return false;
        bool ok = oem_emit(compile, (OemStep){
                            .kind = node.kind == OEM_N_CALL
                                ? (node.tail ? OEM_S_TAIL : OEM_S_CALL)
                                : OEM_S_PRIM,
                            .recovers = node.recovers,
                            .checked_body =
                                (node.checked_body || node.check_entry) &&
                                        !node.protocol_skipped
                                    ? (node.identity_typed ? 2u : 1u) : 0u,
                            .op = node.op, .relation = node.relation,
                            .first_arg = first_arg, .arg_count = node.count,
                            .pattern = node.exposed, .value = head,
                        }, NULL);
        if (ok && node.check_entry) {
            compile->program->steps[entry].target = compile->program->step_len;
            /* A stale tail call's host answer returns to this equation's
             * caller; the ordinary tail path never reaches this return. */
            if (node.tail)
                ok = oem_emit(compile, (OemStep){
                    .kind = OEM_S_RET, .value = compile->output,
                }, NULL);
        }
        return ok;
    }
    case OEM_N_MATCH: {
        /* A fallback resumes where the template's code ends. */
        uint32_t match_step = 0u;
        if (!oem_emit_goals(compile, node.value) ||
            !oem_emit(compile, (OemStep){
                          .kind = OEM_S_MATCH, .pattern = node.pattern,
                          .match_row = node.match_row,
                          .relation = node.relation,
                          .value = compile->nodes[node.value].exposed,
                      }, &match_step) ||
            !oem_emit_goals(compile, node.body))
            return false;
        if (node.relation)
            compile->program->steps[match_step].target =
                compile->program->step_len;
        return true;
    }
    case OEM_N_HOST:
        return oem_emit(compile, (OemStep){
                            .kind = OEM_S_HOST, .op = node.op,
                            .relation = node.relation, .value = node.value,
                            .pattern = node.exposed, .target = node.host_fast,
                            .type_dead = node.type_dead,
                            .existence = node.existence,
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
    OemNode *target = &compile->nodes[branch];
    const OemTemplate *output = &compile->program->templates[target->exposed];
    if (target->kind == OEM_N_BUILTIN_ENTRY &&
        output->kind == OEM_T_SLOT && output->slot >= compile->var_len)
        oem_let_fills_pattern(compile, node->exposed, branch);
    bool meets = !((target->kind == OEM_N_CALL && target->tail) ||
                   target->kind == OEM_N_FAIL ||
                   target->exposed == node->exposed);
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
    case OEM_N_BUILTIN_ENTRY: {
        uint32_t entry = 0u;
        bool ok = oem_emit(compile, (OemStep){
                .kind = OEM_S_BUILTIN_ENTRY, .relation = node.relation,
                .value = node.value, .pattern = node.exposed,
            }, &entry) &&
            (node.exposed == compile->nodes[node.body].exposed ||
             oem_emit(compile, (OemStep){.kind = OEM_S_BIND,
                .pattern = node.exposed,
                .value = compile->nodes[node.body].exposed}, NULL)) &&
            oem_emit_tail(compile, node.body);
        if (!ok)
            return false;
        compile->program->steps[entry].target = compile->program->step_len;
        return oem_emit(compile, (OemStep){.kind = OEM_S_RET,
                            .value = compile->output}, NULL);
    }
    case OEM_N_SEQUENCE:
        if (!node.tail)
            return oem_emit_goals(compile, index) &&
                oem_emit(compile, (OemStep){.kind = OEM_S_RET,
                                            .value = compile->output}, NULL);
        for (uint32_t child = 0u; child + 1u < node.count; child++) {
            if (!oem_emit_goals(compile,
                                compile->node_children[node.first + child]))
                return false;
        }
        return oem_emit_tail(compile,
                              compile->node_children[node.first + node.count - 1u]);
    case OEM_N_LET:
        /* A late output's lists meet their holes after the body, so the
         * body runs as a value and the activation then returns. */
        if (node.late_output)
            return oem_emit_goals(compile, index) &&
                oem_emit(compile, (OemStep){.kind = OEM_S_RET,
                                            .value = compile->output}, NULL);
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
                         .relation = node.relation,
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
        uint32_t first_attempt = compile->program->step_len;
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
        /* Which arms' first tests can decide anything: an arm whose root
         * binds tests nothing, and leaves no earlier arm the last. */
        CettaOpenEquationProgram *program = compile->program;
        uint32_t last_open = UINT32_MAX;
        uint32_t ordinal = 0u;
        for (uint32_t at = first_attempt; at < program->step_len &&
             program->steps[at].kind == OEM_S_CASE;
             at = program->steps[at].target, ordinal++) {
            if (!oem_template_tests(program, program->steps[at].pattern))
                last_open = ordinal;
        }
        ordinal = 0u;
        for (uint32_t at = first_attempt; at < program->step_len &&
             program->steps[at].kind == OEM_S_CASE;
             at = program->steps[at].target, ordinal++) {
            OemStep *attempt = &program->steps[at];
            bool later_test = last_open == UINT32_MAX || last_open <= ordinal;
            attempt->arm = (uint8_t)(
                (oem_template_tests(program, attempt->pattern)
                     ? OEM_ARM_TESTS : 0u) |
                (later_test ? OEM_ARM_LATER_TEST : 0u));
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
        if (oem_builtin_source(compile, expr, plan)) {
            *kind_out = root ? OEM_OUTPUT_OPEN : OEM_OUTPUT_TEMPLATE;
            return root || oem_hole(compile, template_out);
        }
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
    if (!plan || plan->output == PETTA_PLAN_OUTPUT_OPAQUE ||
        oem_builtin_source(compile, expr, plan)) {
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
enum {
    OEM_SLOT_MUST_SEEN = 1u,
    OEM_SLOT_MAY_SEEN = 2u,
    /* A saved source can acquire this slot on the fallback path. It does
     * not make the native path allocate or read the slot early. */
    OEM_SLOT_FALLBACK = 4u,
};

static void oem_template_reads(const CettaOpenEquationProgram *program,
                               uint32_t template_index, uint8_t *flow,
                               uint8_t *celled) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_SLOT) {
        if (celled && !(flow[item->slot] & OEM_SLOT_MUST_SEEN))
            celled[item->slot] = 1u;
        flow[item->slot] = OEM_SLOT_MUST_SEEN | OEM_SLOT_MAY_SEEN;
        return;
    }
    if (item->kind != OEM_T_BUILD && !oem_template_is_list(item))
        return;
    for (uint32_t index = 0u; index < item->count; index++)
        oem_template_reads(program,
                           program->template_children[item->first + index],
                           flow, celled);
}

static void oem_template_fallback_reads(
    const CettaOpenEquationProgram *program, uint32_t template_index,
    uint8_t *flow) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_SLOT) {
        flow[item->slot] |= OEM_SLOT_FALLBACK;
        return;
    }
    if (item->kind == OEM_T_BUILD || oem_template_is_list(item))
        for (uint32_t child = 0u; child < item->count; child++)
            oem_template_fallback_reads(program,
                program->template_children[item->first + child], flow);
}

/* A case pattern binds a slot on the successful edge at its first
 * occurrence and reads it at later occurrences. The failed edge restores
 * every binding and retains the incoming flow. */
static void oem_template_case_stores(
        const CettaOpenEquationProgram *program, uint32_t template_index,
        uint8_t *flow, uint8_t *stored) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_SLOT) {
        if (!(flow[item->slot] & OEM_SLOT_MUST_SEEN))
            stored[item->slot] = 1u;
        flow[item->slot] |= OEM_SLOT_MUST_SEEN | OEM_SLOT_MAY_SEEN;
        return;
    }
    if (item->kind == OEM_T_BUILD || oem_template_is_list(item))
        for (uint32_t child = 0u; child < item->count; child++)
            oem_template_case_stores(
                program, program->template_children[item->first + child],
                flow, stored);
}

/* A step's result slot when the region may store the result there: a
 * primitive's, a bind's, a cut's truth value, or a host step's the region
 * may decide, whose pattern is one slot.  UINT32_MAX otherwise. */
static uint32_t oem_step_result_slot(const CettaOpenEquationProgram *program,
                                     const OemStep *step) {
    bool storing = step->kind == OEM_S_PRIM || step->kind == OEM_S_BIND ||
        step->kind == OEM_S_ENTRY || step->kind == OEM_S_BUILTIN_ENTRY ||
        step->kind == OEM_S_CALL || step->kind == OEM_S_EQUATION_CUT ||
        (step->kind == OEM_S_HOST && step->target >= OEM_HOST_PURE);
    const OemTemplate *pattern = &program->templates[step->pattern];
    return storing && pattern->kind == OEM_T_SLOT ? pattern->slot
                                                  : UINT32_MAX;
}

/* Mark the slots the body stores at their first occurrence on every path,
 * and the steps that store them. Tests and call-entry branches point
 * forwards; the ordinary and stale call-entry paths can rejoin. At each
 * step, intersect the slots every incoming path mentioned and union the
 * slots any incoming path mentioned. A read needs a cell if some path has
 * not mentioned its slot; a direct store is safe only if none has. A slot
 * whose first occurrence is a read on some path, or which the activation
 * itself reads or assigns, keeps its cell; any other slot the body mentions
 * is first stored on every path that mentions it, and a slot no path
 * mentions is never read or stored and needs no cell either.  A store made
 * while a frame newer than the activation is live is trailed
 * (`oem_store_slot`): restoring that frame resumes the path before the
 * store, and the slot must be empty again, since a collection traces every
 * filled slot. Each step is visited once, rather than once per path. */
static bool oem_mark_first_stores(OemCompile *compile, OemEquation *equation) {
    CettaOpenEquationProgram *program = compile->program;
    uint32_t slots = compile->slot_count ? compile->slot_count : 1u;
    uint32_t first = equation->first_step;
    uint32_t end = program->step_len;
    size_t steps = end - first ? end - first : 1u;
    if ((size_t)slots > SIZE_MAX / 5u ||
        steps > (SIZE_MAX - (size_t)slots * 5u) / 2u ||
        steps > (SIZE_MAX - (size_t)slots * 5u - steps * 2u) / slots)
        return oem_reject(compile, "slot analysis too wide");
    size_t bytes = (size_t)slots * 5u + steps * 2u + steps * slots;
    uint8_t inline_analysis[512];
    uint8_t *analysis = bytes <= sizeof(inline_analysis)
        ? inline_analysis : malloc(bytes);
    if (!analysis)
        return oem_reject(compile, "out of memory");
    memset(analysis, 0, bytes);
    uint8_t *assigned = analysis;
    uint8_t *celled = assigned + slots;
    uint8_t *stored = celled + slots;
    /* The slots some path mentions; a slot none mentions is never read or
     * stored, and needs no cell. */
    uint8_t *mentioned = stored + slots;
    uint8_t *entry_flow = mentioned + slots;
    uint8_t *candidate = entry_flow + slots;
    uint8_t *reached = candidate + steps;
    uint8_t *slot_flow = reached + steps;
    bool ok = true;
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
    if (ok) {
        reached[0] = 1u;
        for (uint32_t slot = 0u; slot < slots; slot++)
            slot_flow[slot] = celled[slot]
                ? OEM_SLOT_MUST_SEEN | OEM_SLOT_MAY_SEEN : 0u;
    }
    for (uint32_t pc = first; ok && pc < end; pc++) {
        if (!reached[pc - first])
            continue;
        uint8_t *flow = &slot_flow[(size_t)(pc - first) * slots];
        const OemStep *step = &program->steps[pc];
        uint32_t result = oem_step_result_slot(program, step);
        if (step->kind == OEM_S_CALL || step->kind == OEM_S_TAIL ||
            step->kind == OEM_S_PRIM || step->kind == OEM_S_TEST) {
            for (uint32_t arg = 0u; arg < step->arg_count; arg++)
                oem_template_reads(
                    program, program->step_args[step->first_arg + arg],
                    flow, celled);
        }
        if (step->kind == OEM_S_BIND || step->kind == OEM_S_MATCH ||
            step->kind == OEM_S_HOST || step->kind == OEM_S_CASE)
            oem_template_reads(program, step->value, flow, celled);
        if (step->kind == OEM_S_CASE) {
            memcpy(entry_flow, flow, slots);
            oem_template_case_stores(program, step->pattern, flow, stored);
        }
        if (step->kind == OEM_S_HOST &&
            (step->target == OEM_HOST_COLLECT ||
             step->target == OEM_HOST_ALTERNATIVES ||
             step->target == OEM_HOST_ONCE)) {
            for (uint32_t arg = 0u; arg < step->arg_count; arg++)
                oem_template_reads(
                    program, program->step_args[step->first_arg + arg],
                    flow, celled);
            if (step->target == OEM_HOST_COLLECT)
                oem_template_reads(
                    program, program->controls[step->control].yield,
                    flow, celled);
        }
        /* A tail call returns its value in place of the equation's:
         * nothing reads its pattern. */
        if (step->kind != OEM_S_RET && step->kind != OEM_S_FAIL &&
            step->kind != OEM_S_TEST && step->kind != OEM_S_TAIL &&
            step->kind != OEM_S_CASE &&
            result == UINT32_MAX)
            oem_template_reads(program, step->pattern, flow, celled);
        uint8_t result_flow = result != UINT32_MAX ? flow[result] : 0u;
        if (result != UINT32_MAX) {
            if (!(flow[result] & OEM_SLOT_MUST_SEEN))
                stored[result] = 1u;
            if (!(flow[result] & OEM_SLOT_MAY_SEEN))
                candidate[pc - first] = flow[result] & OEM_SLOT_FALLBACK
                    ? OEM_STORE_IF_EMPTY : OEM_STORE_FIRST;
            flow[result] = OEM_SLOT_MUST_SEEN | OEM_SLOT_MAY_SEEN;
        }
        for (uint32_t slot = 0u; slot < slots; slot++)
            mentioned[slot] |= flow[slot] &
                (OEM_SLOT_MUST_SEEN | OEM_SLOT_MAY_SEEN);
        if (step->kind == OEM_S_RET || step->kind == OEM_S_FAIL ||
            step->kind == OEM_S_TAIL)
            continue;
        bool branch = step->kind == OEM_S_TEST ||
            step->kind == OEM_S_CASE || step->kind == OEM_S_ENTRY ||
            step->kind == OEM_S_BUILTIN_ENTRY ||
            (step->kind == OEM_S_MATCH && step->target);
        uint32_t targets[2] = {branch ? step->target : pc + 1u, pc + 1u};
        for (uint32_t edge = 0u; edge < (branch ? 2u : 1u); edge++) {
            /* ENTRY stores only on its stale-plan path. Its ordinary
             * path has yet to execute the argument goals and CALL. */
            if (edge == 1u && (step->kind == OEM_S_ENTRY ||
                 step->kind == OEM_S_BUILTIN_ENTRY) && result != UINT32_MAX) {
                flow[result] = result_flow;
            }
            uint32_t target = targets[edge];
            if (target <= pc || target > end) {
                ok = oem_reject(compile, "non-forward slot analysis edge");
                break;
            }
            if (target == end)
                continue;
            uint8_t *edge_flow =
                edge == 0u && step->kind == OEM_S_CASE ? entry_flow : flow;
            if (edge == 0u && (step->kind == OEM_S_ENTRY ||
                              step->kind == OEM_S_BUILTIN_ENTRY)) {
                /* Only the stale path reads the saved source. Its unfilled
                 * slots acquire cells at that boundary, not on the normal
                 * path before a let's producer has even run. */
                memcpy(entry_flow, flow, slots);
                oem_template_fallback_reads(program, step->value, entry_flow);
                edge_flow = entry_flow;
                for (uint32_t slot = 0u; slot < slots; slot++)
                    mentioned[slot] |= edge_flow[slot] &
                        (OEM_SLOT_MUST_SEEN | OEM_SLOT_MAY_SEEN);
            }
            uint8_t *to_flow = &slot_flow[(size_t)(target - first) * slots];
            if (!reached[target - first]) {
                memcpy(to_flow, edge_flow, slots);
                reached[target - first] = 1u;
            } else {
                for (uint32_t slot = 0u; slot < slots; slot++)
                    to_flow[slot] =
                        (to_flow[slot] & edge_flow[slot] & OEM_SLOT_MUST_SEEN) |
                        ((to_flow[slot] | edge_flow[slot]) &
                         (OEM_SLOT_MAY_SEEN | OEM_SLOT_FALLBACK));
            }
        }
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
                base + oem_step_result_slot(program, step)]
                ? candidate[pc - first] : 0u;
        }
    }
    if (analysis != inline_analysis)
        free(analysis);
    return ok || oem_reject(compile, "out of memory");
}

/* Whether `formal` has a variable that one of `later` mentions. */
static bool oem_formal_read_later(const Atom *formal, Atom *const *later,
                                  uint32_t count) {
    if (formal->kind == ATOM_VAR) {
        for (uint32_t i = 0u; i < count; i++) {
            if (cetta_observation_atom_contains_var(later[i], formal->var_id))
                return true;
        }
        return false;
    }
    if (formal->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0u; i < formal->expr.len; i++) {
        if (oem_formal_read_later(formal->expr.elems[i], later, count))
            return true;
    }
    return false;
}

/* Guard `index` is dead when no later guard mentions a variable of its
 * requirement or of an earlier one: an earlier guard can alias two
 * requirement variables, so its own variables are not enough.  The
 * signature comes from freshened declarations, so its variables occur in
 * no head or body and no consumer outside the protocol reads them. */
static bool oem_guard_dead(Atom *const *formals, uint32_t index,
                           uint32_t guards) {
    for (uint32_t earlier = 0u; earlier <= index; earlier++) {
        if (oem_formal_read_later(formals[earlier], formals + index + 1u,
                                  guards - index - 1u))
            return false;
    }
    return true;
}

/* One selected signature. The body receives only prepared values;
 * reading them for its operands cannot rerun their producers. Guards
 * share signature slots and use the existing machine continuation. */
static bool oem_build_typed_protocol(OemCompile *compile,
                                      const OemLocalSource *local,
                                      uint32_t *out) {
    Atom *call = local->rhs;
    uint32_t arity = local->contract.arity;
    if (arity > (UINT32_MAX - 2u) / 2u)
        return oem_reject(compile, "typed call too wide");
    uint32_t *actions = malloc(sizeof(*actions) * (2u * arity + 2u));
    uint32_t *operands = malloc(sizeof(*operands) * (arity ? arity : 1u));
    Atom **formals = malloc(sizeof(*formals) * ((size_t)arity + 1u));
    bool ok = actions && operands && formals;
    uint32_t count = 0u;
    uint32_t guards = 0u;
    uint32_t guard_index = 0u;
    /* Under call contracts a position whose declared type is a variable has
     * no guard: every value has a type it could take there. */
    bool contracts = compile->host->call_contracts;
    bool guard_result = local->contract.guard_result &&
        !(contracts && local->contract.result_type->kind == ATOM_VAR);
    for (uint32_t index = 0u; ok && index < arity; index++) {
        PettaTypeArgument requirement = petta_type_call_argument(
            &local->contract, index + 1u, false);
        if (requirement.demand == PETTA_TYPE_TRANSLATE_AND_GUARD &&
            !(contracts && requirement.formal->kind == ATOM_VAR))
            formals[guards++] = requirement.formal;
    }
    if (ok && guard_result)
        formals[guards++] = local->contract.result_type;
    for (uint32_t index = 0u; ok && index < arity; index++) {
        PettaTypeArgument requirement = petta_type_call_argument(
            &local->contract, index + 1u, false);
        uint32_t argument = 0u;
        if (requirement.demand == PETTA_TYPE_RAW) {
            OemNode raw = {.kind = OEM_N_VALUE};
            ok = oem_lower_pattern(compile, call->expr.elems[index + 1u],
                NULL, 0u, false, &raw.exposed) &&
                oem_node(compile, raw, &argument);
        } else {
            ok = oem_build_value(compile, call->expr.elems[index + 1u],
                petta_plan_child(local->plan, index + 1u), 0u, &argument);
        }
        if (!ok)
            break;
        actions[count++] = argument;
        OemNode ready = {
            .kind = OEM_N_VALUE, .exposed = compile->nodes[argument].exposed,
        };
        ok = oem_node(compile, ready, &operands[index]);
        if (ok && requirement.demand == PETTA_TYPE_TRANSLATE_AND_GUARD &&
            !(contracts && requirement.formal->kind == ATOM_VAR)) {
            OemNode guard = {
                .kind = OEM_N_HOST, .value = ready.exposed,
                .host_fast = local->overload ? OEM_HOST_TYPE_MATCH
                                              : OEM_HOST_TYPE_ACCEPT,
                .type_dead = oem_guard_dead(formals, guard_index, guards),
            };
            guard_index++;
            ok = oem_lower_pattern(compile, requirement.formal, NULL,
                0u, false, &guard.exposed) &&
                oem_node(compile, guard, &actions[count]);
            count++;
        }
    }
    OemNode body = {
        .kind = OEM_N_CALL, .count = arity, .checked_body = true,
        .identity_typed = local->identity_typed,
        .recovers = local->plan->dispatch_handler,
    };
    uint32_t body_node = 0u;
    if (ok && local->identity_typed) {
        CettaOpenEquationProgram *program = compile->program;
        ok = oem_reserve((void **)&program->host_plans,
                         &program->host_plan_cap,
                         program->host_plan_len + 1u,
                         sizeof(*program->host_plans)) &&
            oem_lower_pattern(compile, call, NULL, 0u, false, &body.value);
        if (ok) {
            body.call_plan = program->host_plan_len;
            program->host_plans[program->host_plan_len++] = local->plan;
            body.check_entry = true;
        }
    }
    ok = ok && oem_node_children(compile, operands, arity, &body.first) &&
        oem_relation_index(compile->program, call->expr.elems[0]->sym_id,
            arity, local->hold_body, &body.relation) &&
        oem_hole(compile, &body.exposed) &&
        oem_node(compile, body, &body_node);
    if (ok)
        actions[count++] = body_node;
    if (ok && guard_result) {
        OemNode guard = {
            .kind = OEM_N_HOST, .value = body.exposed,
            .host_fast = local->overload ? OEM_HOST_TYPE_MATCH
                                          : OEM_HOST_TYPE_ACCEPT,
            .type_dead = true,
        };
        ok = oem_lower_pattern(compile, local->contract.result_type, NULL,
            0u, false, &guard.exposed) &&
            oem_node(compile, guard, &actions[count]);
        count++;
    }
    OemNode sequence = {
        .kind = OEM_N_SEQUENCE, .count = count, .exposed = body.exposed,
        .identity_typed = local->identity_typed,
    };
    ok = ok && oem_node_children(compile, actions, count, &sequence.first) &&
        oem_node(compile, sequence, out);
    free(actions);
    free(operands);
    free(formals);
    return ok || oem_reject(compile, "typed protocol compile");
}

/* PeTTa's decons-atom has no answer, and no effect, for a closed value that
 * is not a non-empty list (oem_list_step).  When an equation's body begins
 * by destructuring a head variable that way, with nothing before it, a call
 * whose argument there is such a value cannot answer from the equation:
 * selection may refute it as it refutes a head that cannot match, and a call
 * whose other equations all fall that way leaves no alternative behind its
 * answer.  Only a failure that cannot be observed is skipped; an equation
 * whose body could answer, or act, is always tried. */
static void oem_note_entry_guard(const OemCompile *compile,
                                 OemEquation *equation) {
    const CettaOpenEquationProgram *program = compile->program;
    equation->has_entry_guard = false;
    if (!compile->petta || equation->first_step >= program->step_len)
        return;
    /* A let's pattern shapes the call's output before the call
     * (`(let ($h $t) (decons-atom $xs) ...)` binds the output's fresh cell to
     * ($h $t) first): such a binding cannot fail and has no effect. */
    uint32_t pc = equation->first_step;
    while (pc < program->step_len && program->steps[pc].kind == OEM_S_BIND)
        pc++;
    if (pc >= program->step_len)
        return;
    const OemStep *step = &program->steps[pc];
    if (step->kind != OEM_S_HOST || step->target != OEM_HOST_LIST ||
        (step->op != g_builtin_syms.decons_atom &&
         step->op != g_builtin_syms.petta_decons))
        return;
    const OemTemplate *output = &program->templates[step->pattern];
    if (output->kind != OEM_T_SLOT)
        return;
    for (uint32_t shaping = equation->first_step; shaping < pc; shaping++) {
        const OemTemplate *shaped =
            &program->templates[program->steps[shaping].value];
        if (shaped->kind != OEM_T_SLOT || shaped->slot != output->slot)
            return;
    }
    const OemTemplate *goal = &program->templates[step->value];
    if (goal->kind != OEM_T_BUILD || goal->count != 2u)
        return;
    const OemTemplate *arg =
        &program->templates[program->template_children[goal->first + 1u]];
    if (arg->kind != OEM_T_SLOT)
        return;
    for (uint32_t index = 0u; index < equation->op_count; index++) {
        const OemMatchOp *op = &program->ops[equation->first_op + index];
        if (op->kind == OEM_M_BIND && op->operand == arg->slot) {
            equation->has_entry_guard = true;
            equation->entry_guard_reg = op->reg;
            return;
        }
    }
}

/* Whether a step from `first` on calls a relation `head` at `arity`: the
 * steps a relation's equations compiled to, its controls' bodies among
 * them, call the relation itself. */
static bool oem_steps_call_relation(const CettaOpenEquationProgram *program,
                                    uint32_t first, SymbolId head,
                                    uint32_t arity) {
    for (uint32_t index = first; index < program->step_len; index++) {
        const OemStep *step = &program->steps[index];
        if ((step->kind == OEM_S_CALL || step->kind == OEM_S_TAIL) &&
            step->relation < program->relation_len &&
            program->relations[step->relation].head == head &&
            program->relations[step->relation].arity == arity)
            return true;
    }
    return false;
}

static bool oem_compile_equation(CettaOpenEquationProgram *program,
                                 PettaProgram *petta,
                                 const CettaOpenEquationHost *host,
                                 bool authored, bool named, bool body_is_data, Atom *lhs,
                                 Atom *rhs, const PettaPlanNode *rhs_plan,
                                 const OemLocalSource *typed,
                                 const char **reason) {
    if (program->read_heads_complete &&
        (!oem_record_read_names(program, lhs) ||
         !oem_record_read_names(program, rhs) ||
         (typed && !oem_record_read_names(program, typed->contract.signature))))
        program->read_heads_complete = false;
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
        oem_register_vars(&compile, rhs, 0u) &&
        (!typed || oem_register_vars(&compile, typed->contract.signature, 0u));
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
    if (ok && typed) {
        ok = oem_build_typed_protocol(&compile, typed, &root);
    } else if (ok && body_is_data) {
        OemNode value = {.kind = OEM_N_VALUE};
        ok = oem_lower_pattern(&compile, rhs, NULL, 0u, false,
                                &value.exposed) &&
            oem_node(&compile, value, &root);
    } else {
        ok = ok && oem_build_tail(&compile, rhs, rhs_plan, 0u, &root);
    }
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
        if (body_is_data) {
            equation.output_kind = OEM_OUTPUT_TEMPLATE;
            equation.output = node->exposed;
        } else if (equation.dest_kind == OEM_DEST_SLOT)
            ok = oem_known_output(&compile, rhs, rhs_plan, 0u, true,
                                  &equation.output_kind, &equation.output);
        /* An output the plan fixes before the body would meet the
         * destination before a late output's pattern is read. */
        if (ok && compile.late_outputs &&
            equation.output_kind == OEM_OUTPUT_TEMPLATE)
            ok = oem_reject(&compile,
                "pattern head can be bound by the demanded result");
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
    if (ok)
        oem_note_entry_guard(&compile, &equation);
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
    free(compile.late_binds);
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
            false, false, local.lhs, local.rhs, local.plan,
            local.typed ? &local : NULL, &reason);
    }
    OemRelation *relation = &program->relations[relation_index];
    if (!ok) {
        program->equation_len = first;
        relation->failed = true;
        if (relation_index == 0u && program->lexical_source) {
            *reason_out = reason ? reason : "local entry body";
            return false;
        }
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

/* A typed relation whose equations admit a trusted version gets one,
 * compiled after it like any relation the program calls.  The version keeps
 * its entry facts on the relation, which a checked call must meet to enter
 * it, and on itself the calls it compiles as entries into other versions. */
static void oem_attach_trusted_version(CettaOpenEquationProgram *program,
                                       PettaProgram *petta,
                                       uint32_t relation_index) {
    OemRelation *relation = &program->relations[relation_index];
    if (relation->trusted_version || relation->has_trusted ||
        relation->body_is_data || relation->local ||
        program->relation_len >= OEM_MAX_RELATIONS)
        return;
    PettaContractVersion version;
    if (!petta_contract_trusted_version(petta, program->space, relation->head,
                                        relation->arity, &version))
        return;
    OemRelation trusted = {
        .head = relation->head, .arity = relation->arity,
        .trusted_version = true, .version = version,
    };
    uint32_t trusted_index = 0u;
    if (!OEM_PUSH(program, relation, trusted, &trusted_index))
        return;
    /* The space names are values entry facts are checked over: canonical
     * atoms, which no program owns. */
    for (uint32_t k = 0u; k < version.space_count; k++) {
        Atom *name = atom_symbol_canonical(version.spaces[k]);
        if (!name)
            return;
        program->relations[relation_index].version_spaces[k] = name;
    }
    program->relations[relation_index].has_trusted = true;
    program->relations[relation_index].trusted = trusted_index;
    program->relations[relation_index].version = version;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_PETTA_CONTRACT_VERSION_COMPILED);
}

static bool oem_same_variable(const Atom *left, const Atom *right) {
    return left && right && left->kind == ATOM_VAR &&
        right->kind == ATOM_VAR && var_id_eq(left->var_id, right->var_id);
}

/* Recognize the complete structural catamorphism
 *
 *   f xs = case xs of C0 -> 0; C1 x rest -> 1 + f rest
 *
 * from source syntax.  Every variable occurrence, constructor, literal,
 * recursive head, and arithmetic operation is checked.  A near miss keeps
 * the ordinary relational program. */
static bool oem_exact_int(const Atom *atom, int64_t value) {
    return atom && atom->kind == ATOM_GROUNDED &&
           atom->ground.gkind == GV_INT && atom->ground.ival == value;
}

/* An exact decrement-and-tail recurrence, independent of names. */
static bool oem_recognize_algebraic_drop(Atom *equation, SymbolId head,
                                         SymbolId *nil_out,
                                         SymbolId *cons_out) {
    if (!equation || equation->kind != ATOM_EXPR || equation->expr.len != 3u)
        return false;
    Atom *lhs = equation->expr.elems[1], *rhs = equation->expr.elems[2];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != 3u ||
        !atom_is_symbol_id(lhs->expr.elems[0], head) ||
        lhs->expr.elems[1]->kind != ATOM_VAR ||
        lhs->expr.elems[2]->kind != ATOM_VAR ||
        oem_same_variable(lhs->expr.elems[1], lhs->expr.elems[2]) || !rhs ||
        rhs->kind != ATOM_EXPR || rhs->expr.len != 4u ||
        !atom_is_symbol_id(rhs->expr.elems[0], g_builtin_syms.if_text) ||
        !oem_same_variable(rhs->expr.elems[2], lhs->expr.elems[2]))
        return false;
    Atom *test = rhs->expr.elems[1], *cases = rhs->expr.elems[3];
    if (!test || test->kind != ATOM_EXPR || test->expr.len != 3u ||
        !atom_is_symbol_id(test->expr.elems[0], g_builtin_syms.op_eq) ||
        !oem_same_variable(test->expr.elems[1], lhs->expr.elems[1]) ||
        !oem_exact_int(test->expr.elems[2], 0) || !cases ||
        cases->kind != ATOM_EXPR || cases->expr.len != 3u ||
        !atom_is_symbol_id(cases->expr.elems[0], g_builtin_syms.case_text) ||
        !oem_same_variable(cases->expr.elems[1], lhs->expr.elems[2]))
        return false;
    Atom *arms = cases->expr.elems[2];
    if (!arms || arms->kind != ATOM_EXPR || arms->expr.len != 2u)
        return false;
    Atom *zero = arms->expr.elems[0], *more = arms->expr.elems[1];
    if (!zero || zero->kind != ATOM_EXPR || zero->expr.len != 2u ||
        zero->expr.elems[0]->kind != ATOM_SYMBOL ||
        !atom_is_symbol_id(zero->expr.elems[1], zero->expr.elems[0]->sym_id) ||
        !more || more->kind != ATOM_EXPR || more->expr.len != 2u)
        return false;
    Atom *pattern = more->expr.elems[0], *binding = more->expr.elems[1];
    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len != 3u ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
        pattern->expr.elems[1]->kind != ATOM_VAR ||
        pattern->expr.elems[2]->kind != ATOM_VAR || !binding ||
        binding->kind != ATOM_EXPR || binding->expr.len != 4u ||
        !atom_is_symbol_id(binding->expr.elems[0], g_builtin_syms.let) ||
        binding->expr.elems[1]->kind != ATOM_VAR)
        return false;
    Atom *vars[5] = {lhs->expr.elems[1], lhs->expr.elems[2],
                     pattern->expr.elems[1], pattern->expr.elems[2],
                     binding->expr.elems[1]};
    for (unsigned a = 0u; a < 5u; a++)
        for (unsigned b = a + 1u; b < 5u; b++)
            if (oem_same_variable(vars[a], vars[b]))
                return false;
    Atom *previous = binding->expr.elems[2],
         *recursion = binding->expr.elems[3];
    if (!previous || previous->kind != ATOM_EXPR || previous->expr.len != 3u ||
        !atom_is_symbol_id(previous->expr.elems[0], g_builtin_syms.op_minus) ||
        !oem_same_variable(previous->expr.elems[1], vars[0]) ||
        !oem_exact_int(previous->expr.elems[2], 1) || !recursion ||
        recursion->kind != ATOM_EXPR || recursion->expr.len != 3u ||
        !atom_is_symbol_id(recursion->expr.elems[0], head) ||
        !oem_same_variable(recursion->expr.elems[1], vars[4]) ||
        !oem_same_variable(recursion->expr.elems[2], vars[3]))
        return false;
    *nil_out = zero->expr.elems[0]->sym_id;
    *cons_out = pattern->expr.elems[0]->sym_id;
    return *nil_out != *cons_out;
}

static bool oem_recognize_algebraic_length(
        Atom *equation, SymbolId head, SymbolId *nil_out,
        SymbolId *cons_out) {
    if (!equation || equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u)
        return false;
    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != 2u ||
        !atom_is_symbol_id(lhs->expr.elems[0], head) ||
        lhs->expr.elems[1]->kind != ATOM_VAR ||
        !rhs || rhs->kind != ATOM_EXPR || rhs->expr.len != 3u ||
        !atom_is_symbol_id(rhs->expr.elems[0], g_builtin_syms.case_text) ||
        !oem_same_variable(rhs->expr.elems[1], lhs->expr.elems[1]))
        return false;
    Atom *arms = rhs->expr.elems[2];
    if (!arms || arms->kind != ATOM_EXPR || arms->expr.len != 2u)
        return false;
    Atom *nil_arm = arms->expr.elems[0];
    Atom *cons_arm = arms->expr.elems[1];
    if (!nil_arm || nil_arm->kind != ATOM_EXPR || nil_arm->expr.len != 2u ||
        nil_arm->expr.elems[0]->kind != ATOM_SYMBOL ||
        nil_arm->expr.elems[1]->kind != ATOM_GROUNDED ||
        nil_arm->expr.elems[1]->ground.gkind != GV_INT ||
        nil_arm->expr.elems[1]->ground.ival != 0 ||
        !cons_arm || cons_arm->kind != ATOM_EXPR ||
        cons_arm->expr.len != 2u)
        return false;
    Atom *pattern = cons_arm->expr.elems[0];
    Atom *successor = cons_arm->expr.elems[1];
    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len != 3u ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
        pattern->expr.elems[1]->kind != ATOM_VAR ||
        pattern->expr.elems[2]->kind != ATOM_VAR ||
        var_id_eq(pattern->expr.elems[1]->var_id,
                  pattern->expr.elems[2]->var_id) ||
        oem_same_variable(pattern->expr.elems[1], lhs->expr.elems[1]) ||
        oem_same_variable(pattern->expr.elems[2], lhs->expr.elems[1]) ||
        !successor || successor->kind != ATOM_EXPR ||
        successor->expr.len != 3u ||
        !atom_is_symbol_id(successor->expr.elems[0], g_builtin_syms.op_plus) ||
        successor->expr.elems[1]->kind != ATOM_GROUNDED ||
        successor->expr.elems[1]->ground.gkind != GV_INT ||
        successor->expr.elems[1]->ground.ival != 1)
        return false;
    Atom *recursion = successor->expr.elems[2];
    if (!recursion || recursion->kind != ATOM_EXPR ||
        recursion->expr.len != 2u ||
        !atom_is_symbol_id(recursion->expr.elems[0], head) ||
        !oem_same_variable(recursion->expr.elems[1],
                           pattern->expr.elems[2]))
        return false;
    *nil_out = nil_arm->expr.elems[0]->sym_id;
    *cons_out = pattern->expr.elems[0]->sym_id;
    return *nil_out != *cons_out;
}

/* Recognize the complete structural append catamorphism
 *
 *   f left right = case left of
 *     C0 -> right
 *     C1 x rest -> let tail = f rest right in C1 x tail
 *
 * independently of the relation and constructor spellings. */
static bool oem_recognize_algebraic_append(
        Atom *equation, SymbolId head, SymbolId *nil_out,
        SymbolId *cons_out) {
    if (!equation || equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u)
        return false;
    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != 3u ||
        !atom_is_symbol_id(lhs->expr.elems[0], head) ||
        lhs->expr.elems[1]->kind != ATOM_VAR ||
        lhs->expr.elems[2]->kind != ATOM_VAR ||
        var_id_eq(lhs->expr.elems[1]->var_id, lhs->expr.elems[2]->var_id) ||
        !rhs || rhs->kind != ATOM_EXPR || rhs->expr.len != 3u ||
        !atom_is_symbol_id(rhs->expr.elems[0], g_builtin_syms.case_text) ||
        !oem_same_variable(rhs->expr.elems[1], lhs->expr.elems[1]))
        return false;
    Atom *arms = rhs->expr.elems[2];
    if (!arms || arms->kind != ATOM_EXPR || arms->expr.len != 2u)
        return false;
    Atom *nil_arm = arms->expr.elems[0];
    Atom *cons_arm = arms->expr.elems[1];
    if (!nil_arm || nil_arm->kind != ATOM_EXPR || nil_arm->expr.len != 2u ||
        nil_arm->expr.elems[0]->kind != ATOM_SYMBOL ||
        !oem_same_variable(nil_arm->expr.elems[1], lhs->expr.elems[2]) ||
        !cons_arm || cons_arm->kind != ATOM_EXPR ||
        cons_arm->expr.len != 2u)
        return false;
    Atom *pattern = cons_arm->expr.elems[0];
    Atom *binding = cons_arm->expr.elems[1];
    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len != 3u ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
        pattern->expr.elems[1]->kind != ATOM_VAR ||
        pattern->expr.elems[2]->kind != ATOM_VAR ||
        var_id_eq(pattern->expr.elems[1]->var_id,
                  pattern->expr.elems[2]->var_id) ||
        oem_same_variable(pattern->expr.elems[1], lhs->expr.elems[1]) ||
        oem_same_variable(pattern->expr.elems[1], lhs->expr.elems[2]) ||
        oem_same_variable(pattern->expr.elems[2], lhs->expr.elems[1]) ||
        oem_same_variable(pattern->expr.elems[2], lhs->expr.elems[2]) ||
        !binding || binding->kind != ATOM_EXPR || binding->expr.len != 4u ||
        !atom_is_symbol_id(binding->expr.elems[0], g_builtin_syms.let) ||
        binding->expr.elems[1]->kind != ATOM_VAR)
        return false;
    Atom *tail_var = binding->expr.elems[1];
    if (oem_same_variable(tail_var, lhs->expr.elems[1]) ||
        oem_same_variable(tail_var, lhs->expr.elems[2]) ||
        oem_same_variable(tail_var, pattern->expr.elems[1]) ||
        oem_same_variable(tail_var, pattern->expr.elems[2]))
        return false;
    Atom *recursion = binding->expr.elems[2];
    Atom *result = binding->expr.elems[3];
    if (!recursion || recursion->kind != ATOM_EXPR ||
        recursion->expr.len != 3u ||
        !atom_is_symbol_id(recursion->expr.elems[0], head) ||
        !oem_same_variable(recursion->expr.elems[1],
                           pattern->expr.elems[2]) ||
        !oem_same_variable(recursion->expr.elems[2], lhs->expr.elems[2]) ||
        !result || result->kind != ATOM_EXPR || result->expr.len != 3u ||
        result->expr.elems[0]->kind != ATOM_SYMBOL ||
        result->expr.elems[0]->sym_id != pattern->expr.elems[0]->sym_id ||
        !oem_same_variable(result->expr.elems[1], pattern->expr.elems[1]) ||
        !oem_same_variable(result->expr.elems[2], tail_var))
        return false;
    *nil_out = nil_arm->expr.elems[0]->sym_id;
    *cons_out = pattern->expr.elems[0]->sym_id;
    return *nil_out != *cons_out;
}

/* Recognize a projection-map catamorphism: each list item must have one
 * fixed constructor, one selected field is copied, and the output spine is
 * rebuilt in source order. */
static bool oem_recognize_algebraic_project_map(
        Atom *equation, SymbolId head, SymbolId *nil_out,
        SymbolId *cons_out, SymbolId *item_out,
        uint32_t *item_arity_out, uint32_t *projection_out) {
    if (!equation || equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u)
        return false;
    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != 2u ||
        !atom_is_symbol_id(lhs->expr.elems[0], head) ||
        lhs->expr.elems[1]->kind != ATOM_VAR ||
        !rhs || rhs->kind != ATOM_EXPR || rhs->expr.len != 3u ||
        !atom_is_symbol_id(rhs->expr.elems[0], g_builtin_syms.case_text) ||
        !oem_same_variable(rhs->expr.elems[1], lhs->expr.elems[1]))
        return false;
    Atom *arms = rhs->expr.elems[2];
    if (!arms || arms->kind != ATOM_EXPR || arms->expr.len != 2u)
        return false;
    Atom *nil_arm = arms->expr.elems[0];
    Atom *cons_arm = arms->expr.elems[1];
    if (!nil_arm || nil_arm->kind != ATOM_EXPR || nil_arm->expr.len != 2u ||
        nil_arm->expr.elems[0]->kind != ATOM_SYMBOL ||
        !atom_is_symbol_id(nil_arm->expr.elems[1],
                           nil_arm->expr.elems[0]->sym_id) ||
        !cons_arm || cons_arm->kind != ATOM_EXPR ||
        cons_arm->expr.len != 2u)
        return false;
    Atom *pattern = cons_arm->expr.elems[0];
    Atom *binding = cons_arm->expr.elems[1];
    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len != 3u ||
        pattern->expr.elems[0]->kind != ATOM_SYMBOL ||
        pattern->expr.elems[1]->kind != ATOM_EXPR ||
        pattern->expr.elems[1]->expr.len < 2u ||
        pattern->expr.elems[1]->expr.elems[0]->kind != ATOM_SYMBOL ||
        pattern->expr.elems[2]->kind != ATOM_VAR ||
        oem_same_variable(pattern->expr.elems[2], lhs->expr.elems[1]) ||
        !binding || binding->kind != ATOM_EXPR || binding->expr.len != 4u ||
        !atom_is_symbol_id(binding->expr.elems[0], g_builtin_syms.let) ||
        binding->expr.elems[1]->kind != ATOM_VAR)
        return false;
    Atom *item = pattern->expr.elems[1];
    Atom *rest = pattern->expr.elems[2];
    Atom *tail_var = binding->expr.elems[1];
    if (oem_same_variable(tail_var, lhs->expr.elems[1]) ||
        oem_same_variable(tail_var, rest))
        return false;
    for (CettaExprIndex i = 1u; i < item->expr.len; i++) {
        if (item->expr.elems[i]->kind != ATOM_VAR ||
            oem_same_variable(item->expr.elems[i], lhs->expr.elems[1]) ||
            oem_same_variable(item->expr.elems[i], rest) ||
            oem_same_variable(item->expr.elems[i], tail_var))
            return false;
        for (CettaExprIndex j = 1u; j < i; j++)
            if (oem_same_variable(item->expr.elems[i], item->expr.elems[j]))
                return false;
    }
    Atom *recursion = binding->expr.elems[2];
    Atom *result = binding->expr.elems[3];
    if (!recursion || recursion->kind != ATOM_EXPR ||
        recursion->expr.len != 2u ||
        !atom_is_symbol_id(recursion->expr.elems[0], head) ||
        !oem_same_variable(recursion->expr.elems[1], rest) ||
        !result || result->kind != ATOM_EXPR || result->expr.len != 3u ||
        result->expr.elems[0]->kind != ATOM_SYMBOL ||
        result->expr.elems[0]->sym_id != pattern->expr.elems[0]->sym_id ||
        !oem_same_variable(result->expr.elems[2], tail_var))
        return false;
    uint32_t projection = UINT32_MAX;
    for (CettaExprIndex i = 1u; i < item->expr.len; i++) {
        if (oem_same_variable(result->expr.elems[1], item->expr.elems[i])) {
            projection = i;
            break;
        }
    }
    if (projection == UINT32_MAX)
        return false;
    *nil_out = nil_arm->expr.elems[0]->sym_id;
    *cons_out = pattern->expr.elems[0]->sym_id;
    *item_out = item->expr.elems[0]->sym_id;
    *item_arity_out = item->expr.len;
    *projection_out = projection;
    return *nil_out != *cons_out;
}

/* Recognize a short-circuit fold which returns one literal when an item has
 * a designated constructor and another at the end of the spine. */
static bool oem_recognize_algebraic_contains(
        Atom *equation, SymbolId head, SymbolId *nil_out,
        SymbolId *cons_out, SymbolId *item_out,
        uint32_t *item_arity_out, SymbolId *true_out,
        SymbolId *false_out) {
    if (!equation || equation->kind != ATOM_EXPR ||
        equation->expr.len != 3u)
        return false;
    Atom *lhs = equation->expr.elems[1];
    Atom *rhs = equation->expr.elems[2];
    if (!lhs || lhs->kind != ATOM_EXPR || lhs->expr.len != 2u ||
        !atom_is_symbol_id(lhs->expr.elems[0], head) ||
        lhs->expr.elems[1]->kind != ATOM_VAR ||
        !rhs || rhs->kind != ATOM_EXPR || rhs->expr.len != 3u ||
        !atom_is_symbol_id(rhs->expr.elems[0], g_builtin_syms.case_text) ||
        !oem_same_variable(rhs->expr.elems[1], lhs->expr.elems[1]))
        return false;
    Atom *arms = rhs->expr.elems[2];
    if (!arms || arms->kind != ATOM_EXPR || arms->expr.len != 3u)
        return false;
    Atom *nil_arm = arms->expr.elems[0];
    Atom *hit_arm = arms->expr.elems[1];
    Atom *next_arm = arms->expr.elems[2];
    if (!nil_arm || nil_arm->kind != ATOM_EXPR || nil_arm->expr.len != 2u ||
        nil_arm->expr.elems[0]->kind != ATOM_SYMBOL ||
        nil_arm->expr.elems[1]->kind != ATOM_SYMBOL ||
        !hit_arm || hit_arm->kind != ATOM_EXPR || hit_arm->expr.len != 2u ||
        hit_arm->expr.elems[1]->kind != ATOM_SYMBOL ||
        !next_arm || next_arm->kind != ATOM_EXPR ||
        next_arm->expr.len != 2u)
        return false;
    Atom *hit = hit_arm->expr.elems[0];
    Atom *next = next_arm->expr.elems[0];
    Atom *recursion = next_arm->expr.elems[1];
    if (!hit || hit->kind != ATOM_EXPR || hit->expr.len != 3u ||
        hit->expr.elems[0]->kind != ATOM_SYMBOL ||
        hit->expr.elems[1]->kind != ATOM_EXPR ||
        hit->expr.elems[1]->expr.len < 1u ||
        hit->expr.elems[1]->expr.elems[0]->kind != ATOM_SYMBOL ||
        hit->expr.elems[2]->kind != ATOM_VAR ||
        !next || next->kind != ATOM_EXPR || next->expr.len != 3u ||
        next->expr.elems[0]->kind != ATOM_SYMBOL ||
        next->expr.elems[0]->sym_id != hit->expr.elems[0]->sym_id ||
        next->expr.elems[1]->kind != ATOM_VAR ||
        next->expr.elems[2]->kind != ATOM_VAR ||
        oem_same_variable(next->expr.elems[1], next->expr.elems[2]) ||
        !recursion || recursion->kind != ATOM_EXPR ||
        recursion->expr.len != 2u ||
        !atom_is_symbol_id(recursion->expr.elems[0], head) ||
        !oem_same_variable(recursion->expr.elems[1], next->expr.elems[2]))
        return false;
    Atom *item = hit->expr.elems[1];
    for (CettaExprIndex i = 1u; i < item->expr.len; i++) {
        if (item->expr.elems[i]->kind != ATOM_VAR ||
            oem_same_variable(item->expr.elems[i], hit->expr.elems[2]) ||
            oem_same_variable(item->expr.elems[i], next->expr.elems[1]) ||
            oem_same_variable(item->expr.elems[i], lhs->expr.elems[1]))
            return false;
        for (CettaExprIndex j = 1u; j < i; j++)
            if (oem_same_variable(item->expr.elems[i], item->expr.elems[j]))
                return false;
    }
    if (oem_same_variable(hit->expr.elems[2], lhs->expr.elems[1]) ||
        oem_same_variable(next->expr.elems[1], lhs->expr.elems[1]) ||
        nil_arm->expr.elems[1]->sym_id == hit_arm->expr.elems[1]->sym_id)
        return false;
    *nil_out = nil_arm->expr.elems[0]->sym_id;
    *cons_out = hit->expr.elems[0]->sym_id;
    *item_out = item->expr.elems[0]->sym_id;
    *item_arity_out = item->expr.len;
    *true_out = hit_arm->expr.elems[1]->sym_id;
    *false_out = nil_arm->expr.elems[1]->sym_id;
    return *nil_out != *cons_out;
}

static bool oem_algebraic_constructors_inert(
        CettaOpenEquationProgram *program, SymbolId nil, SymbolId cons,
        SymbolId item) {
    Arena scratch;
    arena_init(&scratch);
    bool inert = petta_contract_symbol_uninterpreted(
            program->space, &scratch, nil) &&
        petta_contract_symbol_uninterpreted(
            program->space, &scratch, cons) &&
        (item == SYMBOL_ID_NONE || petta_contract_symbol_uninterpreted(
            program->space, &scratch, item));
    arena_free(&scratch);
    return inert;
}

/* A fold replaces the recursive calls as well as their bodies.  Their
 * protocol must therefore have one nonrejecting branch, or be untyped.
 * Reuse the protocol compiler's judgment instead of inferring this from
 * the equation's syntax or its outer caller's completed guards. */
static bool oem_algebraic_recursive_protocol(
        CettaOpenEquationProgram *program, PettaProgram *petta,
        const CettaOpenEquationHost *host, SymbolId head, uint32_t arity) {
    if (!host || !host->call_protocol_admitted ||
        !host->call_protocol_admitted(
            host->context, program->space, head, arity))
        return false;
    Arena scratch;
    arena_init(&scratch);
    PettaTypeCall *calls = NULL;
    uint32_t count = 0u;
    bool hold_body = false;
    Atom *name = atom_symbol_id(&scratch, head);
    bool available = name && petta_program_type_calls(
        petta, program->space, name, arity, &scratch,
        &calls, &count, &hold_body);
    OemCompile compile = {.host = host};
    bool admitted = available && !hold_body &&
        (count == 0u || (count == 1u && calls[0].arity == arity &&
            oem_identity_typed_protocol(&compile, &calls[0], hold_body)));
    free(calls);
    arena_free(&scratch);
    return admitted;
}

typedef struct {
    uint32_t function, field;
} OemScalarCall;
typedef struct {
    uint64_t base;
    OemScalarCall *calls;
    uint32_t count;
} OemScalarLinear;
typedef struct {
    SymbolId tag;
    uint32_t fields;
    bool wildcard, leaf;
    OemScalarLinear value;
} OemScalarArm;
typedef struct {
    SymbolId head;
    OemScalarArm *arms;
    uint32_t count;
} OemScalarFunction;
struct OemScalarProgram {
    OemScalarFunction *functions;
    uint32_t len, cap;
};
typedef struct OemScalarBinding {
    VarId id;
    OemScalarLinear value;
    bool used;
    struct OemScalarBinding *previous;
} OemScalarBinding;
typedef struct {
    OemScalarProgram *result;
    CettaOpenEquationProgram *program;
    PettaProgram *petta;
    const CettaOpenEquationHost *host;
} OemScalarCompile;

static void oem_scalar_free(OemScalarProgram *scalar) {
    if (!scalar)
        return;
    for (uint32_t f = 0; f < scalar->len; f++) {
        for (uint32_t a = 0; a < scalar->functions[f].count; a++)
            free(scalar->functions[f].arms[a].value.calls);
        free(scalar->functions[f].arms);
    }
    free(scalar->functions);
    free(scalar);
}
static __attribute__((cold, noinline)) bool
oem_scalar_function(OemScalarCompile *, SymbolId, uint32_t *);

static __attribute__((cold, noinline)) bool
oem_scalar_expression(OemScalarCompile *compile, Atom *source, Atom *parameter,
                      Atom *const *fields, uint32_t field_count,
                      OemScalarBinding *bindings, uint32_t depth,
                      OemScalarLinear *out);

static __attribute__((cold, noinline)) bool
oem_scalar_bindings(OemScalarCompile *compile, Atom *pairs, uint32_t at,
                    Atom *body, Atom *parameter, Atom *const *fields,
                    uint32_t field_count, OemScalarBinding *bindings,
                    uint32_t depth, OemScalarLinear *out) {
    if (at == pairs->expr.len)
        return oem_scalar_expression(compile, body, parameter, fields,
                                     field_count, bindings, depth + 1u, out);
    Atom *pair = pairs->expr.elems[at];
    if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u ||
        pair->expr.elems[0]->kind != ATOM_VAR)
        return false;
    Atom *binder = pair->expr.elems[0];
    if (oem_same_variable(binder, parameter))
        return false;
    for (uint32_t index = 0; index < field_count; index++)
        if (oem_same_variable(binder, fields[index]))
            return false;
    for (OemScalarBinding *b = bindings; b; b = b->previous)
        if (var_id_eq(binder->var_id, b->id))
            return false;
    OemScalarBinding next = {.id = binder->var_id, .previous = bindings};
    if (!oem_scalar_expression(compile, pair->expr.elems[1], parameter, fields,
                               field_count, bindings, depth + 1u, &next.value))
        return false;
    bool ok =
        oem_scalar_bindings(compile, pairs, at + 1u, body, parameter, fields,
                            field_count, &next, depth + 1u, out) &&
        next.used;
    free(next.value.calls);
    if (!ok) {
        free(out->calls);
        *out = (OemScalarLinear){0};
    }
    return ok;
}

static __attribute__((cold, noinline)) bool
oem_scalar_expression(OemScalarCompile *compile, Atom *source, Atom *parameter,
                      Atom *const *fields, uint32_t field_count,
                      OemScalarBinding *bindings, uint32_t depth,
                      OemScalarLinear *out) {
    *out = (OemScalarLinear){0};
    if (!source || depth > OEM_MAX_DEPTH)
        return false;
    if (source->kind == ATOM_GROUNDED && source->ground.gkind == GV_INT &&
        source->ground.ival >= 0) {
        out->base = (uint64_t)source->ground.ival;
        return true;
    }
    if (source->kind == ATOM_VAR) {
        for (OemScalarBinding *b = bindings; b; b = b->previous) {
            if (!var_id_eq(source->var_id, b->id))
                continue;
            if (b->used)
                return false;
            *out = b->value;
            out->calls = NULL;
            if (b->value.count) {
                if (!cetta_expr_len_mul_fits_size(b->value.count,
                                                  sizeof(*out->calls))) {
                    *out = (OemScalarLinear){0};
                    return false;
                }
                out->calls = malloc(sizeof(*out->calls) * b->value.count);
                if (!out->calls) {
                    *out = (OemScalarLinear){0};
                    return false;
                }
                memcpy(out->calls, b->value.calls,
                       sizeof(*out->calls) * b->value.count);
            }
            b->used = true;
            return true;
        }
        return false;
    }
    if (source->kind != ATOM_EXPR || !source->expr.len ||
        source->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    SymbolId head = source->expr.elems[0]->sym_id;
    if (source->expr.len == 3u && head == g_builtin_syms.op_plus) {
        OemScalarLinear left = {0}, right = {0};
        bool ok = oem_scalar_expression(compile, source->expr.elems[1],
                                        parameter, fields, field_count,
                                        bindings, depth + 1u, &left) &&
                  oem_scalar_expression(compile, source->expr.elems[2],
                                        parameter, fields, field_count,
                                        bindings, depth + 1u, &right) &&
                  left.base <= (uint64_t)INT64_MAX - right.base &&
                  right.count <= UINT32_MAX - left.count;
        if (ok) {
            out->base = left.base + right.base;
            out->count = left.count + right.count;
            ok = cetta_expr_len_mul_fits_size(out->count, sizeof(*out->calls));
            out->calls = ok && out->count
                             ? malloc(sizeof(*out->calls) * out->count)
                             : NULL;
            ok = ok && (!out->count || out->calls);
            if (ok && left.count)
                memcpy(out->calls, left.calls,
                       sizeof(*out->calls) * left.count);
            if (ok && right.count)
                memcpy(out->calls + left.count, right.calls,
                       sizeof(*out->calls) * right.count);
        }
        free(left.calls);
        free(right.calls);
        if (!ok) {
            free(out->calls);
            *out = (OemScalarLinear){0};
        }
        return ok;
    }
    if (source->expr.len == 4u && head == g_builtin_syms.let) {
        Atom *parts[] = {source->expr.elems[1], source->expr.elems[2]};
        AtomExprBorrowedView pair, pairs;
        if (!atom_expr_borrowed_view_init(&pair, parts, 2u))
            return false;
        Atom *entries[] = {&pair.root};
        if (!atom_expr_borrowed_view_init(&pairs, entries, 1u))
            return false;
        return oem_scalar_bindings(compile, &pairs.root, 0u, source->expr.elems[3],
                                   parameter, fields, field_count, bindings,
                                   depth + 1u, out);
    }
    if (source->expr.len == 3u && head == g_builtin_syms.let_star) {
        Atom *pairs = source->expr.elems[1];
        return pairs && pairs->kind == ATOM_EXPR &&
               oem_scalar_bindings(compile, pairs, 0u, source->expr.elems[2],
                                   parameter, fields, field_count, bindings,
                                   depth + 1u, out);
    }
    if (source->expr.len == 2u && source->expr.elems[1]->kind == ATOM_VAR) {
        for (uint32_t index = 0; index < field_count; index++) {
            if (!oem_same_variable(source->expr.elems[1], fields[index]))
                continue;
            uint32_t function = 0;
            if (!oem_scalar_function(compile, head, &function))
                return false;
            out->calls = malloc(sizeof(*out->calls));
            if (!out->calls)
                return false;
            out->count = 1u;
            out->calls[0] = (OemScalarCall){function, index};
            return true;
        }
    }
    return false;
}

static __attribute__((cold, noinline)) bool
oem_scalar_function(OemScalarCompile *compile, SymbolId head, uint32_t *out) {
    OemScalarProgram *scalar = compile->result;
    for (uint32_t f = 0; f < scalar->len; f++)
        if (scalar->functions[f].head == head) {
            *out = f;
            return true;
        }
    if (scalar->len >= 64u ||
        !oem_algebraic_recursive_protocol(compile->program, compile->petta,
                                          compile->host, head, 1u))
        return false;
    PettaEquationCandidate *candidates = NULL;
    size_t count = 0;
    if (!petta_program_candidate_snapshot(
            compile->petta, compile->program->space, head, &candidates, &count))
        return false;
    bool ok = count == 1u &&
              candidates[0].activation_layout.binding == CETTA_RULE_BIND_UNIFY;
    Atom *equation = ok ? candidates[0].equation : NULL;
    Atom *lhs =
        equation && equation->kind == ATOM_EXPR && equation->expr.len == 3u
            ? equation->expr.elems[1]
            : NULL;
    Atom *rhs = lhs ? equation->expr.elems[2] : NULL;
    ok = ok && lhs && lhs->kind == ATOM_EXPR && lhs->expr.len == 2u &&
         lhs->expr.elems[1]->kind == ATOM_VAR && rhs &&
         rhs->kind == ATOM_EXPR && rhs->expr.len == 3u &&
         atom_is_symbol_id(rhs->expr.elems[0], g_builtin_syms.case_text) &&
         oem_same_variable(lhs->expr.elems[1], rhs->expr.elems[1]);
    Atom *arms = ok ? rhs->expr.elems[2] : NULL;
    ok = ok && arms && arms->kind == ATOM_EXPR && arms->expr.len &&
         arms->expr.len <= 32u &&
         oem_reserve((void **)&scalar->functions, &scalar->cap,
                     scalar->len + 1u, sizeof(*scalar->functions));
    uint32_t index = scalar->len;
    if (ok) {
        scalar->functions[scalar->len++] = (OemScalarFunction){.head = head};
        *out = index;
    }
    OemScalarArm *compiled =
        ok ? calloc(arms->expr.len, sizeof(*compiled)) : NULL;
    ok = ok && compiled;
    for (uint32_t a = 0; ok && a < arms->expr.len; a++) {
        Atom *arm = arms->expr.elems[a];
        ok = arm && arm->kind == ATOM_EXPR && arm->expr.len == 2u;
        Atom *pattern = ok ? arm->expr.elems[0] : NULL;
        Atom *const *fields = NULL;
        uint32_t field_count = 0;
        if (ok && pattern->kind == ATOM_SYMBOL) {
            compiled[a].tag = pattern->sym_id;
            compiled[a].leaf = true;
            ok = oem_algebraic_constructors_inert(
                compile->program, pattern->sym_id, pattern->sym_id,
                SYMBOL_ID_NONE);
        } else if (ok && pattern->kind == ATOM_EXPR && pattern->expr.len > 0u &&
                   pattern->expr.len <= 9u &&
                   pattern->expr.elems[0]->kind == ATOM_SYMBOL) {
            compiled[a].tag = pattern->expr.elems[0]->sym_id;
            fields = pattern->expr.elems + 1u;
            field_count = pattern->expr.len - 1u;
            compiled[a].fields = field_count;
            ok = oem_algebraic_constructors_inert(
                compile->program, compiled[a].tag, compiled[a].tag,
                SYMBOL_ID_NONE);
            for (uint32_t f = 0; ok && f < field_count; f++) {
                ok = fields[f]->kind == ATOM_VAR &&
                     !oem_same_variable(fields[f], lhs->expr.elems[1]);
                for (uint32_t prior = 0; ok && prior < f; prior++)
                    ok = !oem_same_variable(fields[f], fields[prior]);
            }
        } else if (ok && pattern->kind == ATOM_VAR &&
                   a + 1u == arms->expr.len &&
                   !oem_same_variable(pattern, lhs->expr.elems[1])) {
            compiled[a].wildcard = true;
        } else
            ok = false;
        if (ok)
            ok = oem_scalar_expression(compile, arm->expr.elems[1],
                                       lhs->expr.elems[1], fields, field_count,
                                       NULL, 0u, &compiled[a].value);
    }
    if (ok && !oem_record_read_names(compile->program, equation))
        compile->program->read_heads_complete = false;
    if (ok) {
        scalar->functions[index].arms = compiled;
        scalar->functions[index].count = arms->expr.len;
    } else if (compiled) {
        for (uint32_t a = 0; a < arms->expr.len; a++)
            free(compiled[a].value.calls);
        free(compiled);
    }
    free(candidates);
    return ok;
}

static __attribute__((cold, noinline)) OemScalarProgram *
oem_scalar_compile(CettaOpenEquationProgram *program, PettaProgram *petta,
                   const CettaOpenEquationHost *host, SymbolId head) {
    if (!host || !host->builtin_allowed ||
        !host->builtin_allowed(host->context, g_builtin_syms.op_plus) ||
        space_equations_may_match_known_head(program->space,
                                             g_builtin_syms.op_plus))
        return NULL;
    OemScalarProgram *result = calloc(1u, sizeof(*result));
    if (!result)
        return NULL;
    OemScalarCompile compile = {result, program, petta, host};
    uint32_t root = 0;
    if (!oem_scalar_function(&compile, head, &root)) {
        oem_scalar_free(result);
        return NULL;
    }
    return result;
}

static bool oem_compile_relation(CettaOpenEquationProgram *program,
                                 PettaProgram *petta, uint32_t relation_index,
                                 const CettaOpenEquationHost *host,
                                 const char **reason) {
    SymbolId head = program->relations[relation_index].head;
    uint32_t arity = program->relations[relation_index].arity;
    bool (*admitted)(void *, Space *, SymbolId, uint32_t) = host
        ? (host->body_admitted
               ? host->body_admitted : host->relation_admitted)
        : NULL;
    if (admitted && !admitted(host->context, program->space, head, arity)) {
        *reason = "relation owned by the host";
        return false;
    }
    oem_attach_trusted_version(program, petta, relation_index);
    PettaEquationCandidate *candidates = NULL;
    size_t candidate_count = 0u;
    if (!petta_program_candidate_snapshot(petta, program->space, head,
                                          &candidates, &candidate_count)) {
        *reason = "no candidate snapshot";
        return false;
    }
    SymbolId algebraic_nil = SYMBOL_ID_NONE;
    SymbolId algebraic_cons = SYMBOL_ID_NONE;
    SymbolId algebraic_item = SYMBOL_ID_NONE;
    uint32_t algebraic_item_arity = 0u;
    uint32_t algebraic_projection = 0u;
    SymbolId algebraic_true = SYMBOL_ID_NONE;
    SymbolId algebraic_false = SYMBOL_ID_NONE;
    bool body_is_data = program->relations[relation_index].body_is_data;
    bool algebraic_length = !body_is_data && candidate_count == 1u &&
        arity == 1u &&
        candidates[0].activation_layout.binding == CETTA_RULE_BIND_UNIFY &&
        oem_recognize_algebraic_length(
            candidates[0].equation, head, &algebraic_nil, &algebraic_cons);
    bool algebraic_append = !body_is_data && candidate_count == 1u &&
        arity == 2u &&
        candidates[0].activation_layout.binding == CETTA_RULE_BIND_UNIFY &&
        oem_recognize_algebraic_append(
            candidates[0].equation, head, &algebraic_nil, &algebraic_cons);
    bool algebraic_project_map = !body_is_data && candidate_count == 1u &&
        arity == 1u &&
        candidates[0].activation_layout.binding == CETTA_RULE_BIND_UNIFY &&
        oem_recognize_algebraic_project_map(
            candidates[0].equation, head, &algebraic_nil, &algebraic_cons,
            &algebraic_item, &algebraic_item_arity,
            &algebraic_projection);
    bool algebraic_contains = !body_is_data && candidate_count == 1u &&
        arity == 1u &&
        candidates[0].activation_layout.binding == CETTA_RULE_BIND_UNIFY &&
        oem_recognize_algebraic_contains(
            candidates[0].equation, head, &algebraic_nil, &algebraic_cons,
            &algebraic_item, &algebraic_item_arity,
            &algebraic_true, &algebraic_false);
    bool algebraic_drop =
        !body_is_data && candidate_count == 1u && arity == 2u &&
        candidates[0].activation_layout.binding == CETTA_RULE_BIND_UNIFY &&
        oem_recognize_algebraic_drop(candidates[0].equation, head,
                                     &algebraic_nil, &algebraic_cons);
    if ((algebraic_length || algebraic_append || algebraic_project_map ||
         algebraic_contains || algebraic_drop) &&
        (!oem_algebraic_constructors_inert(program, algebraic_nil,
                                           algebraic_cons, algebraic_item) ||
         !oem_algebraic_recursive_protocol(program, petta, host, head,
                                           arity))) {
        algebraic_length = false;
        algebraic_append = false;
        algebraic_project_map = false;
        algebraic_contains = false;
        algebraic_drop = false;
    }
    if (algebraic_drop &&
        (!host || !host->builtin_allowed ||
         !host->builtin_allowed(host->context, g_builtin_syms.op_eq) ||
         !host->builtin_allowed(host->context, g_builtin_syms.op_minus) ||
         space_equations_may_match_known_head(program->space,
                                              g_builtin_syms.op_eq) ||
         space_equations_may_match_known_head(program->space,
                                              g_builtin_syms.op_minus)))
        algebraic_drop = false;
    if (algebraic_length &&
        (!host || !host->builtin_allowed ||
         !host->builtin_allowed(host->context, g_builtin_syms.op_plus) ||
         space_equations_may_match_known_head(
             program->space, g_builtin_syms.op_plus))) {
        algebraic_length = false;
    }
    program->compiling_trusted =
        program->relations[relation_index].trusted_version;
    program->trusted_relation = relation_index;
    /* Equations of this arity, in declaration order. */
    uint32_t first = program->equation_len;
    uint32_t first_step = program->step_len;
    bool ok = true;
    for (size_t index = 0u; ok && index < candidate_count; index++) {
        if (candidates[index].activation_layout.binding != CETTA_RULE_BIND_UNIFY) {
            *reason = "directional rule requires its canonical head matcher";
            ok = false;
            break;
        }
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
        ok = oem_compile_equation(program, petta, host, true, true,
                                  program->relations[relation_index].body_is_data,
                                  lhs, equation->expr.elems[2], rhs_plan,
                                  NULL, reason);
    }
    program->compiling_trusted = false;
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
    program->relations[relation_index].pure_entry_candidate =
                !program->relations[relation_index].local &&
                    !program->relations[relation_index].body_is_data &&
                    arity <= OEM_PURE_RELATION_INLINE_ARGS &&
                    oem_steps_call_relation(program, first_step, head,
                                            arity);
    if (algebraic_drop) {
        program->relations[relation_index].algebraic_fold =
            OEM_ALGEBRAIC_FOLD_DROP;
        program->relations[relation_index].algebraic_nil = algebraic_nil;
        program->relations[relation_index].algebraic_cons = algebraic_cons;
    } else if (algebraic_length) {
        program->relations[relation_index].algebraic_fold =
            OEM_ALGEBRAIC_FOLD_LENGTH;
        program->relations[relation_index].algebraic_nil = algebraic_nil;
        program->relations[relation_index].algebraic_cons = algebraic_cons;
    } else if (algebraic_append) {
        program->relations[relation_index].algebraic_fold =
            OEM_ALGEBRAIC_FOLD_APPEND;
        program->relations[relation_index].algebraic_nil = algebraic_nil;
        program->relations[relation_index].algebraic_cons = algebraic_cons;
    } else if (algebraic_project_map) {
        program->relations[relation_index].algebraic_fold =
            OEM_ALGEBRAIC_FOLD_PROJECT_MAP;
        program->relations[relation_index].algebraic_nil = algebraic_nil;
        program->relations[relation_index].algebraic_cons = algebraic_cons;
        program->relations[relation_index].algebraic_item = algebraic_item;
        program->relations[relation_index].algebraic_item_arity =
            algebraic_item_arity;
        program->relations[relation_index].algebraic_projection =
            algebraic_projection;
    } else if (algebraic_contains) {
        program->relations[relation_index].algebraic_fold =
            OEM_ALGEBRAIC_FOLD_CONTAINS;
        program->relations[relation_index].algebraic_nil = algebraic_nil;
        program->relations[relation_index].algebraic_cons = algebraic_cons;
        program->relations[relation_index].algebraic_item = algebraic_item;
        program->relations[relation_index].algebraic_item_arity =
            algebraic_item_arity;
        program->relations[relation_index].algebraic_true = algebraic_true;
        program->relations[relation_index].algebraic_false = algebraic_false;
    }
    if (program->relations[relation_index].algebraic_fold ==
            OEM_ALGEBRAIC_FOLD_NONE &&
        !body_is_data && arity == 1u &&
        !program->relations[relation_index].local) {
        OemScalarProgram *scalar =
            oem_scalar_compile(program, petta, host, head);
        if (scalar) {
            program->relations[relation_index].scalar_sum = scalar;
            program->relations[relation_index].algebraic_fold =
                OEM_ALGEBRAIC_FOLD_SCALAR_SUM;
        }
    }
    oem_note_first_tests(program, relation_index);
    if (!oem_build_index(program, relation_index))
        return oem_index_failed(reason);
    return true;
}

/* A trusted version that does not compile is dropped: its relation is then
 * entered only through its checked protocol, as without a version, and
 * whatever it had emitted stays unreached. */
static void oem_compile_trusted_version(CettaOpenEquationProgram *program,
                                        PettaProgram *petta,
                                        uint32_t relation_index,
                                        const CettaOpenEquationHost *host) {
    const char *reason = NULL;
    if (oem_compile_relation(program, petta, relation_index, host, &reason))
        return;
    program->relations[relation_index].failed = true;
    for (uint32_t index = 0u; index < program->relation_len; index++) {
        if (program->relations[index].has_trusted &&
            program->relations[index].trusted == relation_index)
            program->relations[index].has_trusted = false;
    }
}

/* A version that does not compile leaves the versions that enter it from a
 * discharged call with nowhere to go: drop every version with such calls,
 * so each of their relations is entered through its checked protocol. */
static void oem_drop_versions_entering_failed(
    CettaOpenEquationProgram *program) {
    bool failed = false;
    for (uint32_t index = 0u; index < program->relation_len; index++)
        failed = failed || (program->relations[index].trusted_version &&
                            program->relations[index].failed);
    if (!failed)
        return;
    for (uint32_t index = 0u; index < program->relation_len; index++) {
        OemRelation *relation = &program->relations[index];
        if (!relation->trusted_version || relation->failed ||
            relation->version.discharged_count == 0u)
            continue;
        relation->failed = true;
        for (uint32_t parent = 0u; parent < program->relation_len; parent++) {
            if (program->relations[parent].has_trusted &&
                program->relations[parent].trusted == index)
                program->relations[parent].has_trusted = false;
        }
    }
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

static bool oem_compile_ready_entry(CettaOpenEquationProgram *program,
    PettaProgram *petta, const CettaOpenEquationHost *host,
    SymbolId head, uint32_t arity, const char **reason) {
    if (!host || !host->call_protocol_admitted ||
        !host->call_protocol_admitted(host->context, program->space, head, arity)) {
        *reason = "ready call protocol owned by the host";
        return false;
    }
    arena_init(&program->atoms);
    arena_set_hashcons(&program->atoms, NULL);
    program->atoms_ready = true;
    Atom **parts = arena_alloc(&program->atoms,
        sizeof(*parts) * ((size_t)arity + 1u));
    if (!parts)
        return false;
    parts[0] = atom_symbol_id(&program->atoms, head);
    for (uint32_t i = 0u; i < arity; ++i) {
        parts[i + 1u] = atom_var_with_id(&program->atoms, "__ready_arg", fresh_var_id());
        if (!parts[i + 1u])
            return false;
    }
    Atom *call = parts[0] ? atom_expr(&program->atoms, parts, (CettaExprLen)arity + 1u) : NULL;
    const PettaPlanNode *plan = call
        ? petta_program_plan_current_in(petta, &program->atoms, call) : NULL;
    uint32_t first = program->equation_len;
    bool ok = plan && oem_compile_equation(program, petta, host,
        false, false, false, call, call, plan, NULL, reason);
    if (!ok)
        return false;
    program->relations[0].first_equation = first;
    program->relations[0].equation_count = program->equation_len - first;
    oem_note_first_tests(program, 0u);
    return oem_build_index(program, 0u);
}

typedef struct {
    Atom *body;
    Atom *const *parameters;
} OemBodySource;

static bool oem_compile_body_entry(CettaOpenEquationProgram *program,
    PettaProgram *petta, const CettaOpenEquationHost *host, const OemBodySource *entry,
    uint32_t arity, const char **reason) {
    enum { MAX_PARAMETERS = 64u };
    if (!arity || arity > MAX_PARAMETERS || !entry->body || !entry->parameters)
        return false;
    program->lexical_source = calloc(1u, sizeof(*program->lexical_source));
    if (!program->lexical_source)
        return false;
    atomic_init(&program->lexical_source->refs, 1u);
    Arena *source = &program->lexical_source->atoms;
    arena_init_detached(source);
    Atom *body = atom_deep_copy(source, entry->body);
    Atom *parameters[MAX_PARAMETERS];
    for (uint32_t index = 0u; body && index < arity; index++) {
        Atom *parameter = entry->parameters[index];
        if (!parameter || parameter->kind != ATOM_VAR)
            return false;
        for (uint32_t previous = 0u; previous < index; previous++)
            if (var_id_eq(parameters[previous]->var_id, parameter->var_id))
                return false;
        parameters[index] = atom_deep_copy(source, parameter);
        if (!parameters[index])
            return false;
    }
    if (!body || petta_semantics_lambda_body(body, NULL))
        return false;
    arena_init(&program->atoms);
    arena_set_hashcons(&program->atoms, NULL);
    program->atoms_ready = true;
    const PettaPlanNode *plan = petta_program_plan_current_in(
        petta, &program->atoms, body);
    OemCompile compile = {.program = program, .petta = petta, .host = host};
    uint32_t relation = 0u;
    bool ok = plan && oem_local_relation(&compile, parameters, arity,
        &body, &plan, 1u, &relation) && relation == 0u;
    if (!ok)
        *reason = compile.reason ? compile.reason : "lexical body plan";
    return ok;
}

static CettaOpenEquationProgram *oem_compile_certificate(
    PettaProgram *petta, Space *space, SymbolId head, uint32_t arity,
    const CettaOpenEquationHost *host, const OemBodySource *body, const char **reason_out) {
    const char *reason = NULL;
    if (reason_out)
        *reason_out = NULL;
    /* Candidate rows come from one space's index; a module space's queries
       compose its imports. */
    if (!petta || !space || (!body && head == SYMBOL_ID_NONE) ||
        space_has_dependencies(space))
        return NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PROGRAM_COMPILE);
    CettaOpenEquationProgram *program = calloc(1u, sizeof(*program));
    if (!program)
        return NULL;
    atomic_init(&program->refs, 1u);
    program->ledger_counted = cetta_runtime_stats_is_enabled();
    if (program->ledger_counted)
        cetta_runtime_stats_gauge_add(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PROGRAMS_LIVE,
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PROGRAMS_LIVE_PEAK, 1);
    program->space = space;
    program->entry_kind = host && host->entry_ready_call ? CETTA_OPEN_ENTRY_READY_CALL
        : host && host->entry_body_is_data ? CETTA_OPEN_ENTRY_BODY_DATA : CETTA_OPEN_ENTRY_BODY_VALUE;
    program->compilation_mutation_epoch = space_global_mutation_epoch();
    program->token = space_program_token(space);
    program->source_rows = space_read_token(space);
    program->source_row_count = space_length64(space);
    program->read_heads_complete = host && host->capture_reads;
    program->host_reads_complete = program->read_heads_complete;
    program->host_read_mask = oem_host_read_mask(host);
    OemHostRecorder recorder = {.program = program, .host = host};
    CettaOpenEquationHost observed_host;
    if (program->host_reads_complete) {
        observed_host = oem_recording_host(&recorder);
        host = &observed_host;
    }
    program->spelling = symbol_intern_cstr(g_symbols, "_");
    uint32_t entry = 0u;
    bool ok = body
        ? oem_compile_body_entry(program, petta, host, body, arity, &reason)
        : oem_relation_index(program, head, arity,
            host && host->entry_body_is_data, &entry) && entry == 0u;
    for (uint32_t index = 0u; ok && index < program->relation_len; index++) {
        if (index == 0u && program->entry_kind == CETTA_OPEN_ENTRY_READY_CALL)
            ok = oem_compile_ready_entry(program, petta, host, head, arity, &reason);
        else if (program->relations[index].local)
            ok = oem_compile_local_relation(program, petta, host, index,
                                            &reason);
        else if (program->relations[index].trusted_version)
            oem_compile_trusted_version(program, petta, index, host);
        else
            ok = oem_compile_relation(program, petta, index, host, &reason);
    }
    if (ok && body) {
        ok = !program->relations[0].failed;
        /* A lambda inherits its caller's equation-cut barrier. Until that
         * barrier is represented by an explicit entry argument, such code
         * stays with the canonical machine. */
        for (uint32_t pc = 0u; ok && pc < program->step_len; pc++)
            ok = program->steps[pc].kind != OEM_S_EQUATION_CUT;
        if (!ok)
            reason = "lexical entry or cut barrier";
    }
    oem_host_reads_finish(&recorder);
    if (!ok) {
        const char *rejection = reason ? reason : "compile";
        if (reason_out)
            *reason_out = rejection;
        /* Resource failure may cease without any source change, and a
         * concurrent mutation never licenses a certificate for either
         * snapshot. Semantic refusals are stable under the recorded reads. */
        if (strcmp(rejection, "out of memory") == 0 ||
            strcmp(rejection, "compile") == 0 ||
            !space_program_token_is_current(program->token)) {
            cetta_open_equation_program_release(program);
            return NULL;
        }
        oem_read_heads_finish(program);
        return program;
    }
    oem_drop_versions_entering_failed(program);
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
    /* Slot analysis and all branch indices retain the source entry. Only
     * its dispatch is specialized, after the complete body is available. */
    for (uint32_t pc = 0u; pc + 1u < program->step_len; pc++) {
        OemStep *step = &program->steps[pc];
        if (step->kind != OEM_S_BUILTIN_ENTRY)
            continue;
        uint32_t next = pc + 1u;
        while (next < program->step_len && next - pc < UINT16_MAX &&
               (program->steps[next].kind == OEM_S_ENTRY ||
                program->steps[next].kind == OEM_S_BUILTIN_ENTRY))
            next++;
        step->ready = (uint16_t)(next - pc);
        if (next == program->step_len)
            continue;
        switch (program->steps[next].kind) {
        case OEM_S_PRIM: step->kind = OEM_S_BUILTIN_PRIM; break;
        case OEM_S_TEST: step->kind = OEM_S_BUILTIN_TEST; break;
        case OEM_S_FAIL: step->kind = OEM_S_BUILTIN_FAIL; break;
        case OEM_S_HOST: step->kind = OEM_S_BUILTIN_HOST; break;
        case OEM_S_BIND: step->kind = OEM_S_BUILTIN_BIND; break;
        case OEM_S_CALL:
        case OEM_S_TAIL: step->kind = OEM_S_BUILTIN_CALL; break;
        default: break;
        }
    }
    if (!space_program_token_is_current(program->token)) {
        if (reason_out)
            *reason_out = "program changed during compilation";
        cetta_open_equation_program_release(program);
        return NULL;
    }
    program->admitted = true;
    oem_read_heads_finish(program);
    return program;
}

bool cetta_open_equation_program_admitted(
        const CettaOpenEquationProgram *program) {
    return program && program->admitted;
}

CettaOpenEquationProgram *cetta_open_equation_program_compile_certificate(
    PettaProgram *petta, Space *space, SymbolId head, uint32_t arity,
    const CettaOpenEquationHost *host, const char **reason_out) {
    return oem_compile_certificate(petta, space, head, arity, host, NULL, reason_out);
}

CettaOpenEquationProgram *cetta_open_equation_program_compile_body(
    PettaProgram *petta, Space *space, Atom *body, Atom *const *parameters, uint32_t arity,
    const CettaOpenEquationHost *host, const char **reason_out) {
    if (!body || !parameters ||
        (host && (host->entry_body_is_data || host->entry_ready_call)))
        return NULL;
    OemBodySource source = {.body = body, .parameters = parameters};
    CettaOpenEquationProgram *program = oem_compile_certificate(
        petta, space, SYMBOL_ID_NONE, arity, host, &source, reason_out);
    if (program && !program->admitted) {
        cetta_open_equation_program_release(program);
        return NULL;
    }
    return program;
}

CettaOpenEquationProgram *cetta_open_equation_program_compile(
        PettaProgram *petta, Space *space, SymbolId head, uint32_t arity,
        const CettaOpenEquationHost *host, const char **reason_out) {
    CettaOpenEquationProgram *program =
        cetta_open_equation_program_compile_certificate(
            petta, space, head, arity, host, reason_out);
    if (program && !cetta_open_equation_program_admitted(program)) {
        cetta_open_equation_program_release(program);
        return NULL;
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
    /* The nearest parent that contributes a host recovery or collection
     * receipt. Rebuilt with the parent when a collection moves the chain. */
    const struct OemCont *receiver_parent;
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

static inline const OemCont *oem_receiver_cont(const OemCont *cont) {
    if (cont && (cont->recovers ||
                 (cont->pc < cont->program->step_len &&
                  cont->program->steps[cont->pc].kind == OEM_S_YIELD)))
        return cont;
    return cont ? cont->receiver_parent : NULL;
}

/* A match's rows, snapshotted in the region when the match ran; `arena`
 * is the identity of the arena that holds the block. */
typedef struct {
    uint32_t len;
    uint32_t arena;
    Atom *rows[];
} OemRows;

/* A match over a large space read lazily: the rows an occurrence cursor
 * pinned to the space when the match ran offers in row order, as the
 * snapshot would hold them, with the next one already read.  A frame holds
 * one only while it has that next row; it is released with the frame.  The
 * rows are ids in the space's universe, read where they are stored. */
typedef struct OemLazyRows {
    SpaceOccurrenceCursor occurrences;
    const TermUniverse *universe;
    AtomId next;
} OemLazyRows;

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
        OemLazyRows *lazy;
        OemHostCells *host;
        struct OemCollectBox *collect;
        CettaPreparedPureAnswerCursor *prepared_answers;
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
    /* Some equation's head has taken this call's arguments: under
     * `unmatched_call_is_value` the call then has no value of its own. */
    bool head_matched;
    /* A match frame reading its rows lazily (`lazy`, not `rows`). */
    bool lazy_rows;
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
/* A callee's owned body cursor. Its closed answers resume `cont`. */
#define OEM_PREPARED_FRAME (UINT32_MAX - 6u)
/* The lowest of the frame kinds that are no relation's. */
#define OEM_FIRST_MARKER_FRAME OEM_PREPARED_FRAME

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
    uint32_t host_height;
    /* Each value is then a joint (answer, delayed components) graph. */
    bool promoted;
    /* Under `raises_he_errors`, the errors raised within the collection,
     * newest first: its answers if none of them succeeded. */
    OemCollectItem *errors;
    uint32_t error_count;
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
    /* A borrowed suffix of right, used only within one atomic unification.
     * It becomes an arena-owned header before entering a binding. */
    CettaExprLen right_offset;
} OemPair;

/* A negative occurrence fact earned while this branch's bindings hold.
 * Collisions discard facts; the ordinary graph walk remains the fallback. */
typedef struct {
    const Atom *atom;
    uint64_t epoch;
} OemGroundFact;

/* A component is complete when its pending children have left the walk.
 * An unchanged unsettled count certifies that no open or unresolved shared
 * graph was read within that component, even if a sibling remains open. */
typedef struct {
    Atom *atom;
    uint64_t unsettled;
    uint32_t pending;
} OemOccursClose;

typedef struct {
    const Atom *source;
    size_t dense;
} OemOccursBucket;

/* Dense bucket indices certify occupancy by a round trip. Resetting used
 * empties the set without clearing its retained allocation. */
typedef struct {
    OemOccursBucket *buckets;
    size_t *dense;
    size_t capacity, used;
} OemOccursSet;

/* One lexical callable plan owned by a cursor. Its compiled syntax lives in
 * the cursor's nonmoving names arena, so collection and backtracking cannot
 * invalidate literal pointers retained by the opaque plan. */
typedef struct {
    int64_t identity;
    uint32_t arity;
    bool answers_only;
    void *prepared;
} OemPureCallable;

typedef struct {
    int64_t identity;
    uint32_t arity;
    SpaceProgramToken source;
    uint64_t authority_epoch;
    CettaOpenEquationProgram *program;
} OemCallable;

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
    /* Charged to the resource ledger's open-cursor gauge at open. */
    bool ledger_counted;
    /* Every match reads its snapshot, whose rows are decoded, none a large
     * space's rows by their ids (oem_stored_reference). */
    bool stored_reference;
    /* Verdicts for the entry facts of trusted versions, made on demand. */
    PettaContractMemo *contract_memo;
    CettaOpenEquationProgram **versions;
    uint32_t version_len, version_cap;
    /* The version count at which the loop next releases the versions no
     * live frame or continuation runs (oem_sweep_versions). */
    uint32_t version_sweep_at;
    Arena region;
    /* The old generation: what collections promoted from the region, the
     * young generation, which minor collections leave in place.  Cells
     * below `old_cells` may be named by old atoms, so they keep their
     * indices until a major collection, which collects both generations
     * once the old one reaches `major_after`. */
    Arena old;
    bool old_ready;
    uint32_t old_cells;
    /* A pattern trial trails the bindings and first slot stores it may
     * undo. Its initial cell set may be empty, so activity is separate. */
    uint32_t trial_cells;
    bool trial_active;
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
    /* Whether the cursor may hold atoms of the host's answer arena or of its
     * stable storage (runtime.stable): set when an import shares them or an
     * export or the generation link refers to them, cleared when a detach
     * copies them in.  A detach from storage the cursor does not borrow from
     * has nothing to copy. */
    bool borrows_answer_arena;
    bool borrows_stable;
    /* Atoms detaching collections have copied in, in all. */
    uint64_t detach_copied;
    CettaFrameIdentity epoch;
    Atom **cells;
    uint32_t cell_len, cell_cap;
    uint32_t *trail;
    uint32_t trail_len, trail_cap;
    OemStore *stores;
    uint32_t store_len, store_cap;
    OemFrame *frames;
    uint32_t frame_len, frame_cap;
    /* Frames holding external cursors, released when their choices retire. */
    uint32_t external_frames;
    OemPair *pairs;
    uint32_t pair_cap;
    Atom **walk;
    uint32_t walk_cap;
    OemOccursSet occurs_seen;
    OemOccursClose *occurs_close;
    uint32_t occurs_close_cap;
    OemGroundFact *ground_facts;
    uint32_t ground_fact_cap;
    uint64_t ground_fact_epoch;
    bool ground_facts_live;
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
    OemPureCallable *pure_callables;
    uint32_t pure_callable_len, pure_callable_cap;
    OemCallable *callables;
    uint32_t callable_len, callable_cap;
    /* Advanced by each authority change the host reports; `version_epochs`
     * records the epoch each retained version was entered under. */
    uint64_t authority_epoch;
    /* Repeated checks of the same version share one result while both
     * space mutation and host authority are unchanged. */
    const CettaOpenEquationProgram *checked_program;
    uint64_t checked_mutation_epoch;
    bool checked_current;
    /* Heads an evaluated call named that no program of the region runs,
     * under the authorities and the space program of the lookup. */
    struct OemAbsentHead *absent;
    uint32_t absent_len, absent_cap;
    uint64_t *version_epochs;
    uint32_t poll;
    CettaOpenEquationHandoff handoff;
    /* The depth bound failed an activation whose head matched. */
    bool bound_cut;
    /* The last activation's head took the call's arguments: its parameter
     * operations all succeeded, whatever the destination then did. HE's
     * value of an unmatched call reads it (`unmatched_call_is_value`). */
    bool params_matched;
    /* The host's choice height the last CUT step reported. */
    uint32_t cut_height;
    uint32_t raise_height;
    /* The error the last raise carried, resolved. */
    Atom *raised_value;
    /* A host answer's error, raised when its goal's continuation resumes
     * (`raises_he_errors`). */
    Atom *pending_raise;
    /* Whether the cursor itself changed a space (an admitted add-atom). */
    bool effected;
    /* value_reducible's answers for symbols, which hold for the cursor's
     * life: the calls it runs add no rule. */
    struct {
        SymbolId symbol;
        bool reducible;
    } symbol_reducible[64];
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
    CettaCallOutcome host_return;
    /* The newest host goal was issued inside a call dispatched at run
     * time, whose handler it runs under. */
    bool host_recovers;
    /* The current goal itself, rather than its entered caller, opens the
     * recovery scope. A whole-branch export closes it after this goal. */
    bool host_own_recovers;
    /* No work remains after this host goal except returning its bindings. */
    bool host_terminal;
    OemCollectBox *completed_collection;
    const OemCont *collection_after;
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
    /* An intrinsic provider failed, without producing an answer. */
    OEM_RUN_FAULT,
    /* Within the body runner only: a match counted its rows, and the step
     * after it runs once for all of them. */
    OEM_RUN_FOLDED,
    /* Within the body runner only: an operation entered a new frame. */
    OEM_RUN_ENTERED,
    /* The inline realization declined. No host continuation was published;
     * its caller owns the alternate realization. Internal
     * only: callback refusal leaves the language entrance state intact. */
    OEM_RUN_DECLINED,
} OemRun;

static OemRun oem_service_fault(CettaOpenEquationCursor *cursor) {
    if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_NONE)
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_SERVICE_ERROR;
    return OEM_RUN_FAULT;
}

static inline bool oem_is_cell(const CettaOpenEquationCursor *cursor,
                               const Atom *atom, uint32_t *index_out) {
    if (atom->kind != ATOM_VAR ||
        !cetta_frame_identity_eq(var_epoch_suffix(atom->var_id), cursor->epoch))
        return false;
    /* Cells are the cursor's own variables, numbered from one. */
    *index_out = (uint32_t)(var_base_id(atom->var_id) - 1u);
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

/* Imported variables are the cursor's own globally named cells. A root read
 * follows that same binding authority without creating a substitution image.
 * Collection, binding and rollback occur outside this stable observation. */
static bool oem_sequence_value_root(
        const void *context, BindingValue source, BindingValue *out) {
    if (!out)
        return false;
    *out = binding_value_from_atom(NULL);
    if (!context || !source.skeleton || binding_value_is_contextual(source))
        return false;
    Atom *root = oem_deref(context, source.skeleton);
    if (!root)
        return false;
    *out = binding_value_from_atom(root);
    return true;
}

/* Arity, argument evaluation, dispatch authority and guards belong to the
 * enclosing call. This observes only a completed sequence's logical length;
 * private storage and undecided values retain their existing interpreter. */
static bool oem_sequence_size_value(
        CettaOpenEquationCursor *cursor, SymbolId operation, Atom *argument,
        Atom **out) {
    *out = NULL;
    if (operation != g_builtin_syms.size_atom && operation != g_builtin_syms.size)
        return false;
    PettaSequenceValueView view;
    if (!petta_semantics_sequence_value_read(
            (PettaSequenceValueReader){oem_sequence_value_root, cursor},
            binding_value_from_atom(argument), &view) || !view.sequence)
        return false;
    *out = atom_int(&cursor->region, (int64_t)view.length);
    return true;
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
            var_epoch_id(var_id_from_base((uint64_t)index + 1u), cursor->epoch));
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

static inline __attribute__((always_inline)) bool oem_bind_before(
    CettaOpenEquationCursor *cursor, uint32_t index, Atom *value,
    uint32_t newest) {
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

static inline uint32_t oem_choice_cells(const CettaOpenEquationCursor *cursor) {
    if (cursor->trial_active)
        return cursor->trial_cells;
    return cursor->frame_len
        ? cursor->frames[cursor->frame_len - 1u].cell_mark : 0u;
}

static inline __attribute__((always_inline)) bool oem_bind(
    CettaOpenEquationCursor *cursor, uint32_t index, Atom *value) {
    return oem_bind_before(cursor, index, value, oem_choice_cells(cursor));
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

static bool oem_occurs_set_active(const OemOccursSet *set, size_t bucket) {
    size_t dense = set->buckets[bucket].dense;
    return dense < set->used && set->dense[dense] == bucket;
}

static size_t oem_occurs_set_slot(
        const OemOccursSet *set, const Atom *source) {
    size_t slot = oem_atom_map_hash(source) & (set->capacity - 1u);
    while (oem_occurs_set_active(set, slot) &&
           set->buckets[slot].source != source)
        slot = (slot + 1u) & (set->capacity - 1u);
    return slot;
}

static bool oem_occurs_set_grow(OemOccursSet *set) {
    if (set->capacity > SIZE_MAX / (2u * sizeof(*set->buckets)))
        return false;
    size_t capacity = set->capacity ? set->capacity * 2u : 64u;
    OemOccursSet grown = {
        .buckets = calloc(capacity, sizeof(*grown.buckets)),
        .dense = malloc((capacity / 2u) * sizeof(*grown.dense)),
        .capacity = capacity,
    };
    if (!grown.buckets || !grown.dense) {
        free(grown.buckets);
        free(grown.dense);
        return false;
    }
    for (size_t at = 0u; at < set->used; at++) {
        const Atom *source = set->buckets[set->dense[at]].source;
        size_t slot = oem_occurs_set_slot(&grown, source);
        grown.buckets[slot] = (OemOccursBucket){source, grown.used};
        grown.dense[grown.used++] = slot;
    }
    free(set->buckets);
    free(set->dense);
    *set = grown;
    return true;
}

/* Every active bucket points back to its own dense slot. Stale keys cannot
 * become occupied merely because another check reuses a dense position. */
static bool oem_occurs_set_mark(
        OemOccursSet *set, const Atom *source, bool *first) {
    if (!set->capacity && !oem_occurs_set_grow(set))
        return false;
    size_t slot = oem_occurs_set_slot(set, source);
    if (oem_occurs_set_active(set, slot)) {
        *first = false;
        return true;
    }
    if (set->used == set->capacity / 2u) {
        if (!oem_occurs_set_grow(set))
            return false;
        slot = oem_occurs_set_slot(set, source);
    }
    set->buckets[slot] = (OemOccursBucket){source, set->used};
    set->dense[set->used++] = slot;
    *first = true;
    return true;
}

static inline void oem_forget_ground_facts(CettaOpenEquationCursor *cursor) {
    if (!cursor->ground_facts_live)
        return;
    cursor->ground_facts_live = false;
    if (++cursor->ground_fact_epoch == 0u) {
        if (cursor->ground_fact_cap)
            memset(cursor->ground_facts, 0,
                   sizeof(*cursor->ground_facts) * cursor->ground_fact_cap);
        cursor->ground_fact_epoch = 1u;
    }
}

static inline bool oem_known_ground(const CettaOpenEquationCursor *cursor,
                                     const Atom *atom) {
    const OemGroundFact *fact = &cursor->ground_facts[
        oem_atom_map_hash(atom) & (cursor->ground_fact_cap - 1u)];
    return fact->epoch == cursor->ground_fact_epoch && fact->atom == atom;
}

static inline void oem_record_ground_atom(CettaOpenEquationCursor *cursor,
                                         const Atom *atom) {
    if (!atom || (atom->arena_id != cursor->region.identity &&
                  (!cursor->old_ready || atom->arena_id != cursor->old.identity)))
        return;
    OemGroundFact *fact = &cursor->ground_facts[
        oem_atom_map_hash(atom) & (cursor->ground_fact_cap - 1u)];
    *fact = (OemGroundFact){atom, cursor->ground_fact_epoch};
    cursor->ground_facts_live = true;
}

/* A ground-fact table for `used` facts: false when none could be made. */
static bool oem_ground_facts_reserve(CettaOpenEquationCursor *cursor,
                                     size_t used) {
    uint32_t capacity = 64u;
    while (capacity < 65536u && capacity < used * 2u)
        capacity *= 2u;
    if (capacity > cursor->ground_fact_cap) {
        OemGroundFact *facts = calloc(capacity, sizeof(*facts));
        if (!facts)
            return false;
        free(cursor->ground_facts);
        cursor->ground_facts = facts;
        cursor->ground_fact_cap = capacity;
    }
    return true;
}

static void oem_record_ground_graph(CettaOpenEquationCursor *cursor,
                                    const OemOccursSet *visited, Atom *root) {
    if (!oem_ground_facts_reserve(cursor, visited->used))
        return;
    /* Only cursor-owned headers may be retained: external answer storage
     * can be released independently. A forward binding cannot reopen a
     * dereferenced ground graph; rollback and collection forget the facts. */
    if (visited->used > 1u)
        for (size_t index = 0u; index < visited->used; index++)
            oem_record_ground_atom(
                cursor, visited->buckets[visited->dense[index]].source);
    oem_record_ground_atom(cursor, root);
}

static inline void oem_record_ground(CettaOpenEquationCursor *cursor,
                                     const OemOccursSet *visited, Atom *root) {
    /* Repeated shallow graphs reuse the table without another capacity
     * calculation or a scan of the traversal's empty inline buckets. */
    if (visited->used <= 1u && cursor->ground_fact_cap)
        oem_record_ground_atom(cursor, root);
    else
        oem_record_ground_graph(cursor, visited, root);
}

/* Does cell `index` occur in `atom`? The dereferenced graph is fixed
 * throughout this read. GraphOccurrence's worklist projection permits
 * duplicate pending pointers, but expands each expression only once.  Most
 * values an occurs check meets are small, and a walk of a few expansions
 * needs no table to end: a shared expression is only read again.  So a walk
 * starts without one (oem_occurs_walk), and one that outgrows that budget
 * goes on from where it is with a table of the expressions it expanded
 * (oem_occurs_tabled). */
enum { OEM_OCCURS_UNTABLED_EXPANSIONS = 16u };

/* The walk past its budget: the expressions it expanded seed the table,
 * and the `len` pending atoms of the cursor's walk are read on. */
static __attribute__((noinline)) bool oem_occurs_tabled(
    CettaOpenEquationCursor *cursor, uint32_t index, Atom *atom,
    uint32_t len, Atom *const *expanded, uint32_t expanded_len, bool open,
    bool *failed, bool cached) {
    bool found = false;
    uint64_t unsettled = open ? 1u : 0u;
    uint32_t close_len = 0u;
    /* Completion metadata is optional: allocation failure leaves the
     * ordinary occurrence walk and its result intact. */
    bool certificates = oem_ground_facts_reserve(cursor, expanded_len + 1u) &&
        oem_reserve((void **)&cursor->occurs_close,
                    &cursor->occurs_close_cap, 16u, sizeof(*cursor->occurs_close));
    OemOccursSet *visited = &cursor->occurs_seen;
    visited->used = 0u;
    for (uint32_t at = 0u; !*failed && at < expanded_len; at++) {
        bool first;
        if (!oem_occurs_set_mark(visited, expanded[at], &first))
            *failed = true;
    }
    while (!*failed) {
        while (certificates && close_len &&
               cursor->occurs_close[close_len - 1u].pending == len) {
            OemOccursClose completed = cursor->occurs_close[--close_len];
            if (completed.unsettled == unsettled)
                oem_record_ground_atom(cursor, completed.atom);
        }
        if (!len)
            break;
        Atom *current = oem_deref(cursor, cursor->walk[--len]);
        uint32_t other = 0u;
        if (oem_is_cell(cursor, current, &other)) {
            open = true;
            if (unsettled == UINT64_MAX)
                certificates = false;
            else
                unsettled++;
            if (other == index) {
                found = true;
                break;
            }
            continue;
        }
        if (current->kind == ATOM_VAR) {
            open = true;
            if (unsettled == UINT64_MAX)
                certificates = false;
            else
                unsettled++;
        }
        if (current->kind != ATOM_EXPR || !atom_has_vars(current) ||
            ((cached || certificates) && oem_known_ground(cursor, current)))
            continue;
        bool first;
        if (!oem_occurs_set_mark(visited, current, &first)) {
            *failed = true;
            break;
        }
        if (!first) {
            /* A seen graph without a completed certificate may be open.
             * Conservatively prevent its containing node being certified. */
            if (unsettled == UINT64_MAX)
                certificates = false;
            else
                unsettled++;
            continue;
        }
        if (certificates) {
            if (close_len == UINT32_MAX ||
                !oem_reserve((void **)&cursor->occurs_close,
                             &cursor->occurs_close_cap, close_len + 1u,
                             sizeof(*cursor->occurs_close)))
                certificates = false;
            else {
                cursor->occurs_close[close_len++] =
                    (OemOccursClose){current, unsettled, len};
                if (visited->used > cursor->ground_fact_cap &&
                    cursor->ground_fact_cap < 65536u)
                    (void)oem_ground_facts_reserve(cursor, visited->used);
            }
        }
        if (current->expr.len > UINT32_MAX - len ||
            !oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         len + current->expr.len, sizeof(*cursor->walk))) {
            *failed = true;
            break;
        }
        for (CettaExprIndex child = 0u; child < current->expr.len; child++)
            cursor->walk[len++] = current->expr.elems[child];
    }
    if (!found && !open && !*failed)
        oem_record_ground(cursor, visited, atom);
    return found;
}

static inline __attribute__((always_inline)) bool oem_occurs_walk(
    CettaOpenEquationCursor *cursor, uint32_t index, Atom *atom,
    bool *failed, bool cached) {
    if (cached && oem_known_ground(cursor, atom))
        return false;
    Atom *expanded[OEM_OCCURS_UNTABLED_EXPANSIONS];
    uint32_t expanded_len = 0u;
    uint32_t len = 0u;
    bool open = false;
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
            open = true;
            continue;
        }
        if (current->kind == ATOM_VAR)
            open = true;
        if (current->kind != ATOM_EXPR || !atom_has_vars(current) ||
            (cached && oem_known_ground(cursor, current)))
            continue;
        if (expanded_len == OEM_OCCURS_UNTABLED_EXPANSIONS) {
            /* The slot `current` was read from is still the walk's. */
            cursor->walk[len++] = current;
            return oem_occurs_tabled(cursor, index, atom, len, expanded,
                                     expanded_len, open, failed, cached);
        }
        expanded[expanded_len++] = current;
        if (current->expr.len > UINT32_MAX - len ||
            !oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         len + current->expr.len, sizeof(*cursor->walk))) {
            *failed = true;
            return false;
        }
        for (CettaExprIndex child = 0u; child < current->expr.len; child++)
            cursor->walk[len++] = current->expr.elems[child];
    }
    if (!open && oem_ground_facts_reserve(cursor, expanded_len + 1u)) {
        for (uint32_t at = 0u; at < expanded_len; at++)
            oem_record_ground_atom(cursor, expanded[at]);
        oem_record_ground_atom(cursor, atom);
    }
    return false;
}

static bool oem_occurs(CettaOpenEquationCursor *cursor, uint32_t index,
                       Atom *atom, bool *failed) {
    /* A singleton support summary names the only cell the immutable
     * structure can reach. Follow that cell's current value, not every
     * occurrence of its name. The cursor epoch separates foreign names. */
    for (;;) {
        if (!atom_has_vars(atom))
            return false;
        VarId support = atom_single_variable_id(atom);
        if (var_id_is_none(support))
            break;
        if (!cetta_frame_identity_eq(var_epoch_suffix(support), cursor->epoch))
            return false;
        uint32_t other = (uint32_t)(var_base_id(support) - 1u);
        Atom *value = cursor->cells[other];
        if (!value)
            return other == index;
        atom = value;
    }
    /* Closed immutable structure contains no cells. A branch with no
     * retained facts uses the ordinary walk without per-node cache probes.
     * The two walks share their implementation and differ only in lookup. */
    return cursor->ground_facts_live
        ? oem_occurs_walk(cursor, index, atom, failed, true)
        : oem_occurs_walk(cursor, index, atom, failed, false);
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

/* Unification reads its choice mark only when a cell is actually bound:
 * the newest frame's, or a running trial's (oem_choice_cells). */
static OemUnify oem_unify(CettaOpenEquationCursor *cursor, Atom *left,
                          Atom *right) {
    /* Keep the active pair local; only deferred siblings need traversal
     * storage. Matching, writes and their order use the same loop. */
    uint32_t len = 0u;
    OemPair pair = {left, right, 0u};
    for (;;) {
        Atom *a = oem_deref(cursor, pair.left);
        Atom *b = oem_deref(cursor, pair.right);
        Atom right_view;
        if (pair.right_offset) {
            if (b->kind != ATOM_EXPR || pair.right_offset > b->expr.len)
                return OEM_UNIFY_ERROR;
            atom_header_copy(&right_view, b);
            right_view.expr.elems += pair.right_offset;
            right_view.expr.len -= pair.right_offset;
            right_view.hash_cache = 0u;
            /* The parent's summaries need not describe its suffix. Keep
             * them unknown until a retained header computes exact facts. */
            right_view.structural_facts = 0u;
            b = &right_view;
        }
        if (a == b)
            goto next_pair;
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
            if (value == &right_view) {
                value = atom_expr_suffix(&cursor->region, pair.right,
                                          pair.right_offset);
                if (!value)
                    return OEM_UNIFY_ERROR;
            }
            bool failed = false;
            /* Distinct dereferenced cells cannot contain one another;
             * younger-to-older linking already preserves acyclicity. */
            if (!(left_open && right_open) && atom_has_vars(value) &&
                oem_occurs(cursor, index, value, &failed))
                return OEM_UNIFY_FAIL;
            if (failed || !oem_bind(cursor, index, value))
                return OEM_UNIFY_ERROR;
            goto next_pair;
        }
        if (a->kind == ATOM_VAR || b->kind == ATOM_VAR)
            return OEM_UNIFY_ERROR;
        /* A cons cell and a flat list are one list value when their
         * elements are: the cell's head meets the list's first element and
         * its tail the rest of the list, which shares the list's storage.
         * A cell is never the empty list. */
        bool left_list_cell = oem_is_list_cell(a);
        bool right_list_cell = oem_is_list_cell(b);
        if (left_list_cell != right_list_cell) {
            Atom **flat = left_list_cell ? &b : &a;
            if (atom_is_list(*flat)) {
                /* The shared list adapter keeps typed tails typed, ending
                 * in [] rather than (). A bare list tag never enters the
                 * region as a value. */
                *flat = petta_semantics_flat_list_spine(&cursor->region, *flat);
                if (!*flat)
                    return OEM_UNIFY_ERROR;
                if (!oem_is_list_cell(*flat))
                    return OEM_UNIFY_FAIL;
                left_list_cell = oem_is_list_cell(a);
                right_list_cell = oem_is_list_cell(b);
            }
        }
        if (!oem_decomposition_compatible(a, b))
            return OEM_UNIFY_FAIL;
        if (left_list_cell != right_list_cell) {
            Atom *list_cell = left_list_cell ? a : b;
            Atom *list = left_list_cell ? b : a;
            if (list->kind != ATOM_EXPR || list->expr.len == 0u)
                return OEM_UNIFY_FAIL;
            Atom *list_root = list == &right_view ? pair.right : list;
            CettaExprLen offset = list == &right_view ? pair.right_offset : 0u;
            if (len == UINT32_MAX ||
                !oem_reserve((void **)&cursor->pairs, &cursor->pair_cap,
                             len + 1u, sizeof(*cursor->pairs)))
                return OEM_UNIFY_ERROR;
            cursor->pairs[len++] =
                (OemPair){list_cell->expr.elems[2], list_root, offset + 1u};
            pair = (OemPair){list_cell->expr.elems[1], list->expr.elems[0], 0u};
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
                goto next_pair;
            }
            if (a->expr.len > 0u) {
                if (a->expr.len - 1u > UINT32_MAX - len ||
                    !oem_reserve((void **)&cursor->pairs, &cursor->pair_cap,
                                 len + (a->expr.len - 1u), sizeof(*cursor->pairs)))
                    return OEM_UNIFY_ERROR;
                for (CettaExprIndex child = a->expr.len; child-- > 1u;)
                    cursor->pairs[len++] =
                        (OemPair){a->expr.elems[child], b->expr.elems[child], 0u};
                pair = (OemPair){a->expr.elems[0], b->expr.elems[0], 0u};
                continue;
            }
            goto next_pair;
        }
        if (!atom_eq(a, b))
            return OEM_UNIFY_FAIL;
next_pair:
        if (len == 0u)
            return OEM_UNIFY_OK;
        pair = cursor->pairs[--len];
    }
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
        /* Store the current root, retaining an unbound cell when it is
         * still open. Keeping a bound cell here would make a closed queue
         * retain its whole chain of old bindings and rescan that chain on
         * every later occurs check. Construction and its bindings share
         * the activation's existing rollback/region lifetime. */
        children[index] = oem_deref(cursor, children[index]);
    }
    /* A list onto a complete flat list is flat; a pattern keeps its cell. */
    return item->kind == OEM_T_BUILD
        ? atom_expr(arena, children, item->count)
        : item->kind == OEM_T_LIST
            ? petta_semantics_cons_value(arena, children[0], children[1])
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
        arena_set_ledger_tag(&cursor->scratch, CETTA_ARENA_LEDGER_CURSOR);
        arena_set_block_growth(&cursor->scratch, OEM_FIRST_BLOCK_BYTES);
        arena_set_hashcons(&cursor->scratch, NULL);
        cursor->scratch_ready = true;
    }
    return &cursor->scratch;
}

static bool oem_store_slot(CettaOpenEquationCursor *cursor, Atom **locals,
                           uint32_t slot, Atom *value);

/* A pattern that must construct structure for an open key acquires cells
 * only for the slots that structure reaches. Ground constructor matching
 * stores those slots directly and never calls this path. */
static bool oem_ensure_template_cells(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        uint32_t template_index, uint32_t depth) {
    if (depth > OEM_MAX_DEPTH)
        return false;
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_SLOT) {
        if (locals[item->slot])
            return true;
        Atom *cell = oem_new_cell(cursor);
        return cell && oem_store_slot(cursor, locals, item->slot, cell);
    }
    if (item->kind == OEM_T_LITERAL)
        return true;
    for (uint32_t child = 0u; child < item->count; child++) {
        if (!oem_ensure_template_cells(
                cursor, program, locals,
                program->template_children[item->first + child], depth + 1u))
            return false;
    }
    return true;
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
        if (!oem_ensure_template_cells(cursor, program, locals,
                                       template_index, depth))
            return OEM_UNIFY_ERROR;
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
    } else {
        Atom *const *elements = NULL;
        CettaExprLen length = 0u;
        if (!atom_sequence_view(term, &elements, &length) || length == 0u)
            return term->kind == ATOM_VAR ? OEM_UNIFY_ERROR : OEM_UNIFY_FAIL;
        first = elements[0];
        rest = atom_is_list(term)
            ? atom_sequence_like(&cursor->region, term, elements + 1u, length - 1u)
            : atom_expr_suffix(&cursor->region, term, 1u);
        if (!rest)
            return OEM_UNIFY_ERROR;
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
    if (item->kind == OEM_T_SLOT) {
        if (!locals[item->slot])
            return oem_store_slot(cursor, locals, item->slot, term)
                ? OEM_UNIFY_OK : OEM_UNIFY_ERROR;
        return oem_unify(cursor, locals[item->slot], term);
    }
    if (item->kind != OEM_T_BUILD)
        return oem_unify_list_template(cursor, program, locals,
                                       template_index, term, depth);
    term = oem_deref(cursor, term);
    uint32_t cell = 0u;
    if (oem_is_cell(cursor, term, &cell) || depth > OEM_MAX_DEPTH ||
        oem_is_list_cell(term)) {
        if (!oem_ensure_template_cells(cursor, program, locals,
                                       template_index, depth))
            return OEM_UNIFY_ERROR;
        Atom *built = oem_instantiate(cursor, program, locals, template_index);
        return built ? oem_unify(cursor, built, term) : OEM_UNIFY_ERROR;
    }
    if (term->kind != ATOM_EXPR || term->expr.len != item->count ||
        oem_value_representation(term) != item->representation)
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

/* ---- structural refutation ---------------------------------------------- */

/* A first-order pattern's structure, tested against a closed value without
 * binding: a value refutes a literal it is not (`atom_eq`, the unifier's
 * equality of leaves), and an expression of another length or
 * representation, on which unification fails before it binds anything.  An
 * open value (an unbound cell, a cell of a list, a variable of the host's)
 * refutes nothing.  Equation heads and case arms are selected with these
 * alike, by their first tests (oem_first_test_refutes, oem_arm_refutes) and,
 * for a call, by every test of its heads (oem_may_match): one selection by
 * structure, of every matching equation for a call and of the first
 * matching arm for a case. */
static inline bool oem_refutes_literal(Atom *term, Atom *literal) {
    return !atom_eq(term, literal);
}

static inline bool oem_refutes_expression(const Atom *term, uint32_t length,
                                          uint8_t representation) {
    return oem_value_representation(term) != representation ||
        term->kind != ATOM_EXPR || term->expr.len != length;
}

/* Whether the first test of a case arm's pattern, its root's, refutes the
 * closed value `key`: a literal, or an expression's length and
 * representation.  A root that binds tests nothing. */
static inline bool oem_arm_refutes(const CettaOpenEquationProgram *program,
                                   uint32_t template_index, Atom *key) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_LITERAL)
        return oem_refutes_literal(key, item->literal);
    if (item->kind == OEM_T_BUILD)
        return oem_refutes_expression(key, item->count, item->representation);
    return false;
}

/* Whether a dereferenced value is closed at its root: neither a variable
 * (an unbound cell, or the host's) nor a cell of a list, which refute
 * nothing. */
static inline bool oem_closed_root(const Atom *term) {
    return term->kind != ATOM_VAR && !oem_is_list_cell(term);
}

/* Whether `key` refutes every arm after `arm` of its case, as the equations
 * after a call's selected one are refuted (oem_enter).  The arms are chained
 * by their targets, the last to the case's failure. */
static bool oem_later_arms_refuted(const CettaOpenEquationProgram *program,
                                   const OemStep *arm, Atom *key) {
    if (!(arm->arm & OEM_ARM_LATER_TEST))
        return false;
    bool tested = false;
    for (uint32_t at = arm->target;
         at < program->step_len && program->steps[at].kind == OEM_S_CASE;
         at = program->steps[at].target) {
        if (!oem_arm_refutes(program, program->steps[at].pattern, key))
            return false;
        tested = true;
    }
    return !tested || oem_closed_root(key);
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
    bool resets = region->head != frame->mark.head ||
        (region->head && region->head->used != frame->mark.used) ||
        !arena_at_mark(region, frame->mark);
    if (resets || cursor->cell_len != frame->cell_mark ||
        cursor->trail_len > frame->trail_mark ||
        cursor->store_len > frame->store_mark)
        oem_forget_ground_facts(cursor);
    if (resets) {
        arena_reset(&cursor->region, frame->mark);
    }
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

/* A trial undone by marks rather than by a frame: it may change the
 * region, cells, bindings and first-occurrence activation slots, and
 * while it runs every cell that existed when it began is trailed.  A failed
 * trial is undone; a successful one keeps only the trail entries the
 * enclosing choice needs.  It makes no choice, so nothing can backtrack into
 * it, and collections, which run at the loop's safe point, never meet one. */
typedef struct {
    OemFrame marks;
    uint32_t oldest;
    uint32_t outer;
    bool outer_active;
} OemTrial;

static inline void oem_trial_begin(CettaOpenEquationCursor *cursor,
                                   OemTrial *trial) {
    trial->marks.mark = arena_mark(&cursor->region);
    trial->marks.cell_mark = cursor->cell_len;
    trial->marks.trail_mark = cursor->trail_len;
    trial->marks.store_mark = cursor->store_len;
    trial->oldest = oem_choice_cells(cursor);
    trial->outer = cursor->trial_cells;
    trial->outer_active = cursor->trial_active;
    cursor->trial_cells = cursor->cell_len;
    cursor->trial_active = true;
}

static inline uint32_t oem_slot_birth(uintptr_t header);

static inline void oem_trial_end(CettaOpenEquationCursor *cursor,
                                 const OemTrial *trial, OemUnify result) {
    cursor->trial_cells = trial->outer;
    cursor->trial_active = trial->outer_active;
    if (result != OEM_UNIFY_OK) {
        oem_restore(cursor, &trial->marks);
        return;
    }
    uint32_t kept = trial->marks.trail_mark;
    for (uint32_t entry = kept; entry < cursor->trail_len; entry++) {
        if (cursor->trail[entry] < trial->oldest)
            cursor->trail[kept++] = cursor->trail[entry];
    }
    cursor->trail_len = kept;
    /* A successful inner trial commits to its outer trial. Without an
     * outer trial, only a choice newer than the activation needs these
     * store records for a later rollback. */
    kept = trial->marks.store_mark;
    for (uint32_t entry = kept; entry < cursor->store_len; entry++) {
        const OemStore *store = &cursor->stores[entry];
        uintptr_t header = ((const uintptr_t *)store->locals)[-1];
        if (cursor->trial_active ||
            cursor->frame_len > oem_slot_birth(header))
            cursor->stores[kept++] = *store;
    }
    cursor->store_len = kept;
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

static void oem_lazy_rows_release(OemLazyRows *lazy) {
    space_occurrence_cursor_release(&lazy->occurrences);
    free(lazy);
}

/* Every external owner follows the frame's backtracking and cut lifetime. */
static __attribute__((noinline)) void oem_release_external_frames(
    CettaOpenEquationCursor *cursor, uint32_t len) {
    for (uint32_t index = len;
         index < cursor->frame_len && cursor->external_frames; index++) {
        OemFrame *frame = &cursor->frames[index];
        if (frame->relation == OEM_MATCH_FRAME && frame->lazy_rows) {
            oem_lazy_rows_release(frame->lazy);
            frame->lazy = NULL;
            frame->lazy_rows = false;
            cursor->external_frames--;
        } else if (frame->relation == OEM_PREPARED_FRAME) {
            cetta_prepared_pure_answer_cursor_close(frame->prepared_answers);
            frame->prepared_answers = NULL;
            cursor->external_frames--;
        }
    }
}

/* Drop the frames from `len` on; the frames below the least length since
 * the last collection are unchanged since it.  A frame reading lazy rows
 * releases them, so its pin on the space ends with it. */
static inline void oem_truncate_frames(CettaOpenEquationCursor *cursor,
                                       uint32_t len) {
    if (__builtin_expect(cursor->external_frames != 0u, 0) &&
        len < cursor->frame_len)
        oem_release_external_frames(cursor, len);
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

/* A copied private source is immutable, but not globally owned. An export
 * leases it only when the value it actually shares comes from that source. */
static OemSource *oem_value_source(
    const CettaOpenEquationCursor *cursor, const Atom *atom) {
    if (atom->arena_id == 0u)
        return NULL;
    OemSource *source = cursor->program->lexical_source;
    if (source && atom->arena_id == source->atoms.identity)
        return source;
    for (uint32_t index = 0u; index < cursor->callable_len; index++) {
        CettaOpenEquationProgram *program = cursor->callables[index].program;
        source = program ? program->lexical_source : NULL;
        if (source && atom->arena_id == source->atoms.identity)
            return source;
    }
    return NULL;
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


/* A region value one export has copied into the host's answer arena
 * keeps this marker in `name_key` until it is exported again or the region's
 * next collection. */
static Atom oem_exported_mark;

static inline bool oem_immutable_string(const Atom *atom) {
    return atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING;
}

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
        } else if (!collect &&
                   (operand || !oem_cursor_owns(cursor, atom)) &&
                   (atom->kind != ATOM_EXPR || !atom_has_vars(atom))) {
            result = atom;
            OemSource *source = operand ? NULL : oem_value_source(cursor, atom);
            if (source) {
                CettaPlainScalar scalar;
                if (atom->kind == ATOM_SYMBOL)
                    result = atom_symbol_canonical(atom->sym_id);
                else if (atom->kind == ATOM_EXPR && atom->expr.len == 0u)
                    result = atom_unit_canonical();
                else if (grounded_plain_scalar_from_atom(atom, &scalar))
                    result = grounded_plain_scalar_materialize(ground ? ground : arena, &scalar);
                else if (!arena_retain_owner(ground ? ground : arena, source,
                                              oem_source_retain, oem_source_release))
                    goto done;
            }
        } else if (ground &&
                   (atom->kind == ATOM_EXPR || oem_immutable_string(atom)) &&
                   atom->name_key &&
                   atom->name_key != &oem_exported_mark &&
                   !atom_has_vars(atom)) {
            result = atom->name_key;
        } else if (atom->kind != ATOM_EXPR || atom->expr.len == 0u) {
            /* A repeated immutable string export has the same stable
             * owner as a promoted expression. Its region header retains
             * that copy, so subsequent host calls share the bytes. */
            bool string = ground && oem_immutable_string(atom);
            bool promote =
                (depth > 0u && cursor->resolve_stack[depth - 1u].promote) ||
                (string && (atom->name_key == &oem_exported_mark ||
                    (old_identity != 0u && atom->arena_id == old_identity)));
            result = atom_deep_copy(promote ? ground : arena, atom);
            if (result && string && promote)
                atom->name_key = result;
            else if (result && string && !atom->name_key)
                atom->name_key = &oem_exported_mark;
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

/* Whether a built pattern is its own resolution as an operand: each part
 * is an unbound cell or holds no variable.  oem_instantiate reads a built
 * pattern's parts through their cells, so resolving such a pattern would
 * rebuild it part for part. */
static inline bool oem_pattern_resolved(const Atom *pattern) {
    if (pattern->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex index = 0u; index < pattern->expr.len; index++) {
        const Atom *part = pattern->expr.elems[index];
        if (part->kind != ATOM_VAR && atom_has_vars(part))
            return false;
    }
    return true;
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
    /* The export may leave stable copies as the region's expressions'
     * images (`name_key`). */
    cursor->borrows_stable = true;
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
    cursor->raised_value = error;
    return OEM_RUN_RAISE;
}

static inline __attribute__((always_inline)) OemRun oem_publish(
    CettaOpenEquationCursor *cursor, Atom *value, Atom **value_out) {
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

/* A host can establish the same compiled reads after a disjoint append.
 * The program stays immutable, and the host owns the validation bookmark. */
static bool oem_program_current(CettaOpenEquationCursor *cursor,
                                 const CettaOpenEquationProgram *program) {
    return space_program_token_is_current(program->token) ||
        (cursor->runtime.program_current &&
         cursor->runtime.program_current(cursor->runtime.context,
                                          program->space, program));
}

/* Whether code of `program` may enter its own relations: nothing it was
 * compiled against changed since it was entered. */
static __attribute__((cold, noinline)) bool oem_version_refresh(
    CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, uint64_t mutation) {
    bool current = oem_program_current(cursor, program);
    if (current && cursor->authority_epoch != 0u) {
        current = false;
        for (uint32_t index = 0u; index < cursor->version_len; index++) {
            if (cursor->versions[index] == program &&
                cursor->version_epochs[index] == cursor->authority_epoch) {
                current = true;
                break;
            }
        }
    }
    cursor->checked_program = program;
    cursor->checked_mutation_epoch = mutation;
    cursor->checked_current = current;
    return current;
}

static inline bool oem_version_current(CettaOpenEquationCursor *cursor,
                                       const CettaOpenEquationProgram *program) {
    uint64_t mutation = space_global_mutation_epoch();
    /* Space and host changes are independent. An unchanged compilation
     * epoch and the original host together certify the initial program;
     * later versions use the existing full token and authority check. */
    if (__builtin_expect(((program->compilation_mutation_epoch ^ mutation) |
                          cursor->authority_epoch) == 0u, 1))
        return true;
    if (__builtin_expect(cursor->checked_program == program &&
        cursor->checked_mutation_epoch == mutation, 1))
        return cursor->checked_current;
    return oem_version_refresh(cursor, program, mutation);
}

/* The current program entered at (head, arity), once per change: a
 * version already obtained under the current authorities, or the host's. */
static const CettaOpenEquationProgram *oem_enter_current_kind(
    CettaOpenEquationCursor *cursor, Space *space, SymbolId head,
    uint32_t arity, bool body_is_data, bool ready_call) {
    CettaOpenEquationEntryKind entry_kind = ready_call ? CETTA_OPEN_ENTRY_READY_CALL
        : body_is_data ? CETTA_OPEN_ENTRY_BODY_DATA : CETTA_OPEN_ENTRY_BODY_VALUE;
    for (uint32_t index = 0u; index < cursor->version_len; index++) {
        const CettaOpenEquationProgram *version = cursor->versions[index];
        if (cursor->version_epochs[index] == cursor->authority_epoch &&
            version->relations[0].head == head &&
            version->relations[0].arity == arity &&
            version->entry_kind == entry_kind &&
            oem_program_current(cursor, version))
            return version;
    }
    if (ready_call ? !cursor->runtime.current_ready : !cursor->runtime.current)
        return NULL;
    /* The host's program comes retained: the cursor keeps that reference,
     * or releases it. */
    CettaOpenEquationProgram *program = ready_call
        ? cursor->runtime.current_ready(cursor->runtime.context, space, head, arity)
        : cursor->runtime.current(cursor->runtime.context, space, head, arity, body_is_data);
    if (!program)
        return NULL;
    if (program->space != space || program->relation_len == 0u ||
        program->relations[0].head != head ||
        program->relations[0].arity != arity ||
        program->entry_kind != entry_kind ||
        !oem_program_current(cursor, program)) {
        cetta_open_equation_program_release(program);
        return NULL;
    }
    /* A program the cursor keeps already is the same code confirmed under
     * the current authorities: its entry takes the epoch, so a cursor holds
     * each program once, however often the authorities change.  Only an
     * entry from an earlier epoch can be it (the scan above returns entries
     * of this one), so a cursor that never saw a change has none. */
    for (uint32_t index = 0u;
         cursor->authority_epoch != 0u && index < cursor->version_len;
         index++) {
        if (cursor->versions[index] == program) {
            cetta_open_equation_program_release(program);
            cursor->version_epochs[index] = cursor->authority_epoch;
            cursor->checked_program = NULL;
            return program;
        }
    }
    if (!oem_reserve((void **)&cursor->versions, &cursor->version_cap,
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
    if (cursor->ledger_counted)
        cetta_runtime_stats_gauge_add(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_VERSIONS_LIVE,
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_VERSIONS_LIVE_PEAK, 1);
    cursor->checked_program = NULL;
    return program;
}

static const CettaOpenEquationProgram *oem_enter_current(
    CettaOpenEquationCursor *cursor, Space *space, SymbolId head,
    uint32_t arity, bool body_is_data) {
    return oem_enter_current_kind(cursor, space, head, arity, body_is_data, false);
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
static inline __attribute__((always_inline)) OemRun oem_host_arithmetic(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    const OemStep *step, Atom *left, Atom *right, Atom **result_out,
    bool *truth_out, Atom **value_out, const bool he) {
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
    CettaCallOutcome outcome;
    if (!grounded_call(&cursor->region, head, args, 2u, &outcome)) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return OEM_RUN_HANDOFF;
    }
    if (outcome.kind == CETTA_CALL_RAISED)
        return oem_publish_raise(cursor, outcome.term, value_out);
    if (outcome.kind != CETTA_CALL_VALUE)
        return OEM_RUN_FAILED;
    Atom *result = outcome.term;
    if ((he && cursor->runtime.withdraws_on_error) && atom_is_error(result)) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_WITHDRAWN;
        return OEM_RUN_HANDOFF;
    }
    if ((he && cursor->runtime.raises_he_errors) && atom_is_error(result))
        return oem_publish_raise(cursor, result, value_out);
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

static bool oem_row_part_reducible(CettaOpenEquationCursor *cursor,
                                   Space *space, Atom *part);

/* Whether a part of `row` that one of the pattern's variables takes would
 * evaluate further (value_reducible).  Only such parts reach a template
 * instance: a part the pattern's own constant meets, a bound variable's
 * value included, is no row data.  A part the pattern cannot meet fails
 * its unification later.  Past the depth bound the rest of the row is
 * judged whole. */
static bool oem_row_bound_part_reducible(CettaOpenEquationCursor *cursor,
                                         Space *space, Atom *pattern,
                                         Atom *row, uint32_t depth) {
    pattern = oem_deref(cursor, pattern);
    if (pattern->kind == ATOM_VAR || oem_is_list_cell(pattern) ||
        row->kind == ATOM_VAR || oem_is_list_cell(row) || depth >= 8u)
        return oem_row_part_reducible(cursor, space, row);
    if (pattern->kind != ATOM_EXPR || row->kind != ATOM_EXPR ||
        pattern->expr.len != row->expr.len)
        return false;
    for (CettaExprIndex child = 0u; child < pattern->expr.len; child++)
        if (oem_row_bound_part_reducible(cursor, space,
                                         pattern->expr.elems[child],
                                         row->expr.elems[child], depth + 1u))
            return true;
    return false;
}

#define OEM_FLAT_PATTERN_VARIABLES 8u

/* The positions of a pattern's variables when every one of them is a whole
 * part of it, as in (edge $x $y): false when one sits deeper, or the
 * pattern is itself a variable, which oem_row_bound_part_reducible
 * walks. */
static bool oem_flat_pattern_variables(CettaOpenEquationCursor *cursor,
                                       Atom *pattern, uint32_t *positions,
                                       uint32_t *count, CettaExprLen *length) {
    pattern = oem_deref(cursor, pattern);
    if (pattern->kind != ATOM_EXPR || oem_is_list_cell(pattern))
        return false;
    *count = 0u;
    *length = pattern->expr.len;
    for (CettaExprIndex child = 0u; child < pattern->expr.len; child++) {
        Atom *part = oem_deref(cursor, pattern->expr.elems[child]);
        if (part->kind == ATOM_VAR) {
            if (*count == OEM_FLAT_PATTERN_VARIABLES)
                return false;
            positions[(*count)++] = (uint32_t)child;
        } else if (oem_is_list_cell(part) ||
                   (part->kind == ATOM_EXPR && atom_has_vars(part))) {
            return false;
        }
    }
    return true;
}

/* value_reducible of a row part; a symbol's answer is kept for the
 * cursor's life (symbol_reducible). */
static bool oem_row_part_reducible(CettaOpenEquationCursor *cursor,
                                   Space *space, Atom *part) {
    if (part->kind != ATOM_SYMBOL || part->sym_id == SYMBOL_ID_NONE)
        return cursor->runtime.value_reducible(cursor->runtime.context,
                                               space, part);
    uint32_t slot = (uint32_t)((uint64_t)part->sym_id * 2654435761u) & 63u;
    if (cursor->symbol_reducible[slot].symbol == part->sym_id)
        return cursor->symbol_reducible[slot].reducible;
    bool reducible = cursor->runtime.value_reducible(cursor->runtime.context,
                                                     space, part);
    cursor->symbol_reducible[slot].symbol = part->sym_id;
    cursor->symbol_reducible[slot].reducible = reducible;
    return reducible;
}

/* Whether a row part at one of a flat pattern's variable positions would
 * evaluate further (value_reducible). */
static bool oem_row_positions_reducible(CettaOpenEquationCursor *cursor,
                                        Space *space, Atom *row,
                                        const uint32_t *positions,
                                        uint32_t count) {
    for (uint32_t index = 0u; index < count; index++)
        if (oem_row_part_reducible(cursor, space,
                                   row->expr.elems[positions[index]]))
            return true;
    return false;
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
 * tests their root keys before the rows themselves.  The facts it keeps as
 * bits count too: reading every row would give them back to rows. */
static bool oem_space_small(const Space *space) {
    return space->native.len - space->native.marked +
               space_dense_pairs(space) <= MATCH_TRIE_THRESHOLD;
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

/* A prepared expression's immediate coordinates are literals or live slots.
 * Read those together without building the query root. Nested open structure
 * keeps the ordinary route. Exact matching still uses the retained template
 * over current locals, so correlations and occurs checks remain observable. */
static bool oem_template_candidates_view(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, uint32_t template_index, Space *space,
    CettaIndex **candidates, CettaIndex *count) {
    enum { INLINE_COORDINATES = 16u };
    const OemTemplate *root = &program->templates[template_index];
    if (root->kind != OEM_T_BUILD ||
        !cetta_expr_len_mul_fits_size(root->count, sizeof(Atom *)))
        return false;
    Atom *inline_coordinates[INLINE_COORDINATES];
    Atom **coordinates = root->count <= INLINE_COORDINATES
        ? inline_coordinates : malloc(sizeof(*coordinates) * root->count);
    if (!coordinates)
        return false;
    bool ready = true;
    for (uint32_t index = 0u; ready && index < root->count; index++) {
        const OemTemplate *part = &program->templates[
            program->template_children[root->first + index]];
        Atom *value = part->kind == OEM_T_LITERAL ? part->literal
            : part->kind == OEM_T_SLOT ? oem_deref(cursor, locals[part->slot])
                                      : NULL;
        if (!value || (value->kind != ATOM_VAR && atom_has_vars(value)) ||
            atom_structural_may_have_list_carrier(value)) {
            ready = false;
            break;
        }
        coordinates[index] = value;
    }
    bool selected = ready && space_match_candidates_expression_coordinates64(
        space, coordinates, root->count, candidates, count);
    if (coordinates != inline_coordinates)
        free(coordinates);
    return selected;
}

/* The same row-field demand as a materialized pattern, read over the
 * prepared pattern's current slots. No binding fact survives this query. */
static bool oem_template_flat_variables(CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, Atom **locals,
    uint32_t template_index, uint32_t *positions, uint32_t *count,
    CettaExprLen *length) {
    const OemTemplate *root = &program->templates[template_index];
    if (root->kind != OEM_T_BUILD)
        return false;
    *count = 0u;
    *length = root->count;
    for (uint32_t index = 0u; index < root->count; index++) {
        const OemTemplate *part = &program->templates[
            program->template_children[root->first + index]];
        Atom *value = part->kind == OEM_T_LITERAL ? part->literal
            : part->kind == OEM_T_SLOT ? locals[part->slot] : NULL;
        if (!value)
            return false;
        value = oem_deref(cursor, value);
        if (value->kind == ATOM_VAR) {
            if (*count == OEM_FLAT_PATTERN_VARIABLES)
                return false;
            positions[(*count)++] = index;
        } else if (oem_is_list_cell(value) ||
                   (value->kind == ATOM_EXPR && atom_has_vars(value))) {
            return false;
        }
    }
    return true;
}

static bool oem_template_bound_part_reducible(CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, Atom **locals,
    uint32_t template_index, Space *space, Atom *row, uint32_t depth) {
    const OemTemplate *item = &program->templates[template_index];
    if (item->kind == OEM_T_LITERAL)
        return oem_row_bound_part_reducible(cursor, space, item->literal, row, depth);
    if (item->kind == OEM_T_SLOT)
        return oem_row_bound_part_reducible(cursor, space, locals[item->slot], row, depth);
    if (item->kind != OEM_T_BUILD || row->kind == ATOM_VAR ||
        oem_is_list_cell(row) || depth >= 8u)
        return oem_row_part_reducible(cursor, space, row);
    if (row->kind != ATOM_EXPR || row->expr.len != item->count)
        return false;
    for (uint32_t index = 0u; index < item->count; index++) {
        if (oem_template_bound_part_reducible(cursor, program, locals,
                program->template_children[item->first + index], space,
                row->expr.elems[index], depth + 1u))
            return true;
    }
    return false;
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
    /* Each count attempt is a read-only trial, including failed matches.
     * Merely saving lengths does not trail cells younger than the enclosing
     * choice. The same trial boundary as Boolean unification makes those
     * writes reversible before the next candidate is inspected. */
    OemTrial trial;
    oem_trial_begin(cursor, &trial);
    OemRun run = OEM_RUN_CALLED;
    for (CettaIndex index = 0u; index < offered; index++) {
        Atom *candidate = space_match_candidate_at64(space, indices[index]);
        if (!candidate || !oem_row_may_unify(cursor, query, candidate, 0u))
            continue;
        OemRun tried = oem_try_candidate(cursor, pattern, candidate);
        oem_restore(cursor, &trial.marks);
        if (tried == OEM_RUN_CALLED) {
            count++;
        } else if (tried != OEM_RUN_FAILED) {
            run = tried;
            break;
        }
    }
    oem_trial_end(cursor, &trial, OEM_UNIFY_FAIL);
    free(indices);
    *count_out = count;
    return run;
}

static OemRun oem_match_host_step(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step, Atom *reference);

static OemRun oem_match_fallback_host(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, uint32_t local_count, const OemCont *cont, uint32_t depth,
    const OemStep *step);
static bool oem_fill_cells(CettaOpenEquationCursor *cursor,
                           const CettaOpenEquationProgram *program,
                           Atom **locals, uint32_t template_index,
                           uint32_t depth);

/* The joint rows of a conjunction's legs: the steps of the legs, the slots
 * their patterns name, and the values those slots take on each joint row,
 * `slot_count` per row. */
typedef struct {
    CettaOpenEquationCursor *cursor;
    const CettaOpenEquationProgram *program;
    Atom **locals;
    Space *space;
    const uint32_t *leg_pcs;
    uint32_t legs;
    const uint32_t *slots;
    uint32_t slot_count;
    Atom **values;
    size_t value_len, value_cap;
    uint32_t row_count;
} OemJointRows;

/* The distinct slots a pattern template names, added to `slots`. */
static bool oem_joint_slots(const CettaOpenEquationProgram *program,
                            uint32_t template_index, uint32_t **slots,
                            uint32_t *count, uint32_t *cap,
                            const uint32_t *inline_slots, uint32_t depth) {
    const OemTemplate *item = &program->templates[template_index];
    if (depth > OEM_MAX_DEPTH)
        return false;
    if (item->kind == OEM_T_LITERAL)
        return true;
    if (item->kind == OEM_T_SLOT) {
        for (uint32_t index = 0u; index < *count; index++)
            if ((*slots)[index] == item->slot)
                return true;
        if (*count == *cap) {
            uint32_t grown_cap = *cap * 2u;
            uint32_t *grown = *slots == inline_slots
                ? malloc(sizeof(*grown) * grown_cap)
                : realloc(*slots, sizeof(*grown) * grown_cap);
            if (!grown)
                return false;
            if (*slots == inline_slots)
                memcpy(grown, inline_slots, sizeof(*grown) * *count);
            *slots = grown;
            *cap = grown_cap;
        }
        (*slots)[(*count)++] = item->slot;
        return true;
    }
    for (uint32_t index = 0u; index < item->count; index++)
        if (!oem_joint_slots(program,
                             program->template_children[item->first + index],
                             slots, count, cap, inline_slots, depth + 1u))
            return false;
    return true;
}

/* The slots' values on one joint row.  Each must be ground: a value that
 * holds a cell would lose that cell's binding when the reading is undone,
 * so such a conjunction is the host's. */
static OemRun oem_joint_record(OemJointRows *join) {
    if (join->value_len + join->slot_count > join->value_cap) {
        size_t cap = join->value_cap ? join->value_cap * 2u : 64u;
        while (cap < join->value_len + join->slot_count)
            cap *= 2u;
        Atom **grown = realloc(join->values, cap * sizeof(*grown));
        if (!grown)
            return OEM_RUN_HANDOFF;
        join->values = grown;
        join->value_cap = cap;
    }
    for (uint32_t index = 0u; index < join->slot_count; index++) {
        Atom *value = oem_deref(join->cursor,
                                join->locals[join->slots[index]]);
        if (!value || atom_has_vars(value))
            return OEM_RUN_DECLINED;
        join->values[join->value_len++] = value;
    }
    if (join->row_count == UINT32_MAX)
        return OEM_RUN_HANDOFF;
    join->row_count++;
    return OEM_RUN_CALLED;
}

static OemRun oem_joint_next_leg(OemJointRows *join, uint32_t leg);

/* A leg's rows, each tried in turn: a row that unifies binds the leg's
 * variables, and the next leg reads on, or the last records the joint row.
 * The leg reads under a marker frame of its own, since a binding is
 * trailed only for a cell older than the newest frame (oem_bind), and
 * every reading is undone before the next, so the region, the cells and
 * the slots are as the leg found them.  A row with variables of its own is
 * the host's: its fresh cells would not outlive the reading. */
static OemRun oem_joint_leg(OemJointRows *join, uint32_t leg,
                            Atom *pattern, Atom *const *rows, uint32_t len) {
    CettaOpenEquationCursor *cursor = join->cursor;
    if (!oem_push_frame(cursor, NULL, OEM_ONCE_FRAME, NULL, NULL, 0u))
        return OEM_RUN_HANDOFF;
    uint32_t frame = cursor->frame_len - 1u;
    OemFrame mark = cursor->frames[frame];
    OemRun result = OEM_RUN_CALLED;
    for (uint32_t index = 0u; index < len; index++) {
        Atom *row = rows[index];
        if (atom_has_vars(row)) {
            result = OEM_RUN_DECLINED;
            break;
        }
        OemRun run = oem_try_candidate(cursor, pattern, row);
        if (run == OEM_RUN_CALLED)
            run = leg + 1u == join->legs
                ? oem_joint_record(join)
                : oem_joint_next_leg(join, leg + 1u);
        oem_restore(cursor, &mark);
        if (run != OEM_RUN_CALLED && run != OEM_RUN_FAILED) {
            result = run;
            break;
        }
    }
    oem_truncate_frames(cursor, frame);
    return result;
}

/* A later leg's rows under the bindings the earlier legs made: its pattern
 * as built now, and the rows the space's index offers for it, judged as
 * the match step judges its own (a row the language would evaluate further
 * where a template instance holds part of it is the host's). */
static OemRun oem_joint_next_leg(OemJointRows *join, uint32_t leg) {
    CettaOpenEquationCursor *cursor = join->cursor;
    const OemStep *step = &join->program->steps[join->leg_pcs[leg]];
    Atom *pattern = oem_instantiate(cursor, join->program, join->locals,
                                    step->pattern);
    Atom *query = !pattern ? NULL
        : oem_pattern_resolved(pattern) ? pattern
        : oem_resolve_mode(cursor, pattern, true);
    bool exact_query = false;
    if (query)
        query = petta_semantics_match_index_pattern(&cursor->region, query,
                                                    &exact_query);
    if (!query)
        return OEM_RUN_HANDOFF;
    CettaIndex *indices = NULL;
    CettaIndex count = space_match_candidates64(join->space, query, &indices);
    if (count > (CettaIndex)UINT32_MAX) {
        free(indices);
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
        return OEM_RUN_HANDOFF;
    }
    Atom **rows = count
        ? arena_alloc(&cursor->region, sizeof(*rows) * (size_t)count)
        : NULL;
    uint32_t len = 0u;
    for (CettaIndex index = 0u; rows && index < count; index++) {
        Atom *candidate =
            space_match_candidate_at64(join->space, indices[index]);
        if (candidate && oem_row_may_unify(cursor, query, candidate, 0u))
            rows[len++] = candidate;
    }
    free(indices);
    if (count && !rows)
        return OEM_RUN_HANDOFF;
    if (len == 0u)
        return OEM_RUN_FAILED;
    if (step->relation && cursor->runtime.value_reducible) {
        uint32_t positions[OEM_FLAT_PATTERN_VARIABLES];
        uint32_t position_count = 0u;
        CettaExprLen flat_len = 0u;
        bool flat = oem_flat_pattern_variables(cursor, pattern, positions,
                                               &position_count, &flat_len);
        for (uint32_t index = 0u; index < len; index++) {
            Atom *row = rows[index];
            if (flat && row->kind == ATOM_EXPR && row->expr.len == flat_len
                    ? oem_row_positions_reducible(cursor, join->space, row,
                                                  positions, position_count)
                    : oem_row_bound_part_reducible(cursor, join->space,
                                                   pattern, row, 0u))
                return OEM_RUN_DECLINED;
        }
    }
    return oem_joint_leg(join, leg, pattern, rows, len);
}

/* A conjunction's rows, completed before any template runs.  Under HE's
 * completion of producers a match evaluates its query whole, so the
 * effects of one row's template reach neither this leg's rows nor a later
 * leg's.  The joint rows of this leg and the legs after it are read here,
 * each as the values the legs' patterns give their variables, and the
 * template's code then runs once per joint row, in row order, from an
 * elements frame over those tuples.  Legs with code of their own between
 * them, a row with variables of its own, a value that holds a cell, and a
 * row the language would evaluate further leave the conjunction to the
 * host, which completes the legs from this one on: reading is pure, so
 * nothing has run that the host would run again. */
static __attribute__((noinline)) OemRun oem_match_complete_rows(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step, Space *space, Atom *pattern,
    Atom *const *rows, uint32_t len) {
    uint32_t legs = program->match_fallbacks[step->relation - 1u].legs;
    uint32_t leg_pcs_inline[8];
    uint32_t slots_inline[16];
    uint32_t *leg_pcs = legs <= 8u
        ? leg_pcs_inline : malloc(sizeof(*leg_pcs) * legs);
    uint32_t *slots = slots_inline;
    uint32_t slot_count = 0u, slot_cap = 16u;
    OemJointRows join = {0};
    OemRun run = OEM_RUN_DECLINED;
    if (!leg_pcs) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
        return OEM_RUN_HANDOFF;
    }
    leg_pcs[0] = pc;
    bool chain = true;
    for (uint32_t leg = 1u; chain && leg < legs; leg++) {
        uint32_t next = leg_pcs[leg - 1u] + 1u;
        chain = next < program->step_len &&
            program->steps[next].kind == OEM_S_MATCH &&
            program->steps[next].match_row;
        leg_pcs[leg] = next;
    }
    for (uint32_t leg = 0u; chain && leg < legs; leg++) {
        uint32_t pattern_template = program->steps[leg_pcs[leg]].pattern;
        if (!oem_fill_cells(cursor, program, locals, pattern_template, 0u) ||
            !oem_joint_slots(program, pattern_template, &slots, &slot_count,
                             &slot_cap, slots_inline, 0u)) {
            run = OEM_RUN_HANDOFF;
            goto done;
        }
    }
    if (!chain)
        goto host;
    if (!pattern)
        pattern = oem_instantiate(cursor, program, locals, step->pattern);
    Atom **hole_parts = arena_alloc(&cursor->region,
        sizeof(*hole_parts) * (slot_count ? slot_count : 1u));
    if (!pattern || !hole_parts) {
        run = OEM_RUN_HANDOFF;
        goto done;
    }
    for (uint32_t index = 0u; index < slot_count; index++)
        hole_parts[index] = oem_deref(cursor, locals[slots[index]]);
    Atom *hole = atom_expr(&cursor->region, hole_parts, slot_count);
    if (!hole) {
        run = OEM_RUN_HANDOFF;
        goto done;
    }
    join = (OemJointRows){
        .cursor = cursor, .program = program, .locals = locals,
        .space = space, .leg_pcs = leg_pcs, .legs = legs,
        .slots = slots, .slot_count = slot_count,
    };
    run = oem_joint_leg(&join, 0u, pattern, rows, len);
    if (run == OEM_RUN_DECLINED)
        goto host;
    if (run != OEM_RUN_CALLED)
        goto done;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_OPEN_EQUATION_JOINT_ROWS_COMPLETED);
    if (join.row_count == 0u) {
        run = OEM_RUN_FAILED;
        goto done;
    }
    Atom **tuples = arena_alloc(&cursor->region,
                                sizeof(*tuples) * (size_t)join.row_count);
    Atom **args = arena_alloc(&cursor->region, sizeof(*args) * 2u);
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    bool built = tuples && args && resume;
    for (uint32_t row = 0u; built && row < join.row_count; row++) {
        tuples[row] = atom_expr(&cursor->region,
                                join.values + (size_t)row * slot_count,
                                slot_count);
        built = tuples[row] != NULL;
    }
    Atom *list = built
        ? atom_expr(&cursor->region, tuples, join.row_count) : NULL;
    if (!list) {
        run = OEM_RUN_HANDOFF;
        goto done;
    }
    args[0] = hole;
    args[1] = list;
    *resume = (OemCont){
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
        .local_count = local_count, .pc = leg_pcs[legs - 1u] + 1u,
        .depth = depth,
    };
    run = oem_push_frame(cursor, program, OEM_ELEMENTS_FRAME, args, resume,
                         depth)
        ? OEM_RUN_CALLED : OEM_RUN_HANDOFF;
    goto done;
host:
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_OPEN_EQUATION_JOINT_ROWS_DECLINED);
    run = oem_match_fallback_host(cursor, program, locals, local_count, cont,
                                  depth, step);
done:
    free(join.values);
    if (leg_pcs != leg_pcs_inline)
        free(leg_pcs);
    if (slots != slots_inline)
        free(slots);
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
static OemRun oem_match_fallback_host(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, uint32_t local_count, const OemCont *cont, uint32_t depth,
    const OemStep *step);

/* The id of the next row a lazy match offers, or CETTA_ATOM_ID_NONE when
 * it has none.  Nothing of the row is decoded. */
static AtomId oem_lazy_rows_fetch(CettaOpenEquationCursor *cursor,
                                  OemLazyRows *lazy) {
    for (;;) {
        AtomId row = CETTA_ATOM_ID_NONE;
        if (space_occurrence_cursor_next_id(&lazy->occurrences, NULL, &row) !=
            SPACE_OCCURRENCE_CURSOR_ITEM)
            return CETTA_ATOM_ID_NONE;
        cursor->stats.match_candidates++;
        if (row != CETTA_ATOM_ID_NONE)
            return row;
    }
}

/* A match over a large space whose rows are read as the search needs them:
 * a first match costs the rows before it, not every row of the pattern.
 * The rows and their order are the snapshot's, since the cursor is pinned
 * to the space as the match runs and keeps those rows through any later
 * change.  False when the space's cursor does not admit the pattern, or
 * under the stored-row reference; the snapshot is taken then. */
static bool oem_match_lazy(CettaOpenEquationCursor *cursor,
                           const CettaOpenEquationProgram *program,
                           uint32_t pc, Atom **locals, uint32_t local_count,
                           const OemCont *cont, uint32_t depth,
                           const OemStep *step, Space *space, OemRun *run) {
    *run = OEM_RUN_HANDOFF;
    if (cursor->stored_reference)
        return false;
    Atom *pattern = oem_instantiate(cursor, program, locals, step->pattern);
    if (!pattern)
        return true;
    Atom *query = oem_pattern_resolved(pattern)
        ? pattern : oem_resolve_mode(cursor, pattern, true);
    query = query ? petta_semantics_match_index_pattern(&cursor->region,
                                                        query, NULL)
                  : NULL;
    if (!query)
        return true;
    OemLazyRows *lazy = malloc(sizeof(*lazy));
    if (!lazy)
        return true;
    space_occurrence_cursor_init_empty(&lazy->occurrences);
    if (!space_occurrence_cursor_init(space, query, &lazy->occurrences)) {
        oem_lazy_rows_release(lazy);
        return false;
    }
    lazy->universe = space_occurrence_cursor_universe(&lazy->occurrences);
    lazy->next = lazy->universe ? oem_lazy_rows_fetch(cursor, lazy)
                                : CETTA_ATOM_ID_NONE;
    if (lazy->next == CETTA_ATOM_ID_NONE) {
        oem_lazy_rows_release(lazy);
        *run = OEM_RUN_FAILED;
        return true;
    }
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    Atom **args = arena_alloc(&cursor->region, sizeof(*args));
    if (!resume || !args ||
        !oem_push_frame(cursor, program, OEM_MATCH_FRAME, args, resume,
                        depth)) {
        oem_lazy_rows_release(lazy);
        return true;
    }
    *resume = (OemCont){
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .depth = depth,
    };
    args[0] = pattern;
    OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
    frame->lazy = lazy;
    frame->lazy_rows = true;
    cursor->external_frames++;
    *run = OEM_RUN_CALLED;
    return true;
}

static inline __attribute__((always_inline)) OemRun oem_match_step_mode(
                             CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             uint32_t pc, Atom **locals, uint32_t local_count,
                             const OemCont *cont, uint32_t depth,
                             const OemStep *step, const bool he) {
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
    bool pattern_borrowed = false;
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
        OemRun lazy_run = OEM_RUN_FAILED;
        if (!he && !counted_only &&
            oem_match_lazy(cursor, program, pc, locals, local_count, cont,
                           depth, step, space, &lazy_run))
            return lazy_run;
        CettaIndex *view_candidates = NULL;
        CettaIndex view_count = 0u;
        bool borrowed = !counted_only && oem_template_candidates_view(
            cursor, program, locals, step->pattern, space,
            &view_candidates, &view_count);
        if (borrowed) {
            pattern_borrowed = true;
            if (view_count > (CettaIndex)UINT32_MAX) {
                free(view_candidates);
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
                return OEM_RUN_HANDOFF;
            }
            rows = view_count ? arena_alloc(&cursor->region,
                sizeof(*rows) + sizeof(Atom *) * (size_t)view_count) : NULL;
            for (CettaIndex index = 0u; rows && index < view_count; index++) {
                Atom *candidate = space_match_candidate_at64(space, view_candidates[index]);
                if (candidate && oem_template_may_unify(
                        cursor, program, locals, step->pattern, candidate))
                    rows->rows[len++] = candidate;
            }
            free(view_candidates);
            if (view_count && !rows)
                return OEM_RUN_HANDOFF;
        } else {
            pattern = oem_instantiate(cursor, program, locals, step->pattern);
            if (!pattern)
                return OEM_RUN_HANDOFF;
            Atom *query = oem_pattern_resolved(pattern)
                ? pattern : oem_resolve_mode(cursor, pattern, true);
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
    }
    if (len == 0u)
        return OEM_RUN_FAILED;
    /* A row the language would evaluate further where a template instance
     * holds part of it sends the leg's own source to the host, before any
     * template runs. */
    if (step->relation && (he && cursor->runtime.value_reducible)) {
        /* A flat pattern's variables are read at their positions; a pattern
         * read in place (a small space) judges each row whole. */
        uint32_t positions[OEM_FLAT_PATTERN_VARIABLES];
        uint32_t position_count = 0u;
        CettaExprLen flat_len = 0u;
        bool flat = pattern
            ? oem_flat_pattern_variables(cursor, pattern, positions,
                                         &position_count, &flat_len)
            : pattern_borrowed && oem_template_flat_variables(
                  cursor, program, locals, step->pattern, positions,
                  &position_count, &flat_len);
        for (uint32_t index = 0u; index < len; index++) {
            Atom *row = rows->rows[index];
            if (flat && row->kind == ATOM_EXPR && row->expr.len == flat_len
                    ? oem_row_positions_reducible(cursor, space, row,
                                                  positions, position_count)
                    : pattern
                    ? oem_row_bound_part_reducible(cursor, space, pattern,
                                                   row, 0u)
                    : pattern_borrowed
                    ? oem_template_bound_part_reducible(cursor, program, locals,
                                                       step->pattern, space, row, 0u)
                    : cursor->runtime.value_reducible(
                          cursor->runtime.context, space, row))
                return oem_match_fallback_host(cursor, program, locals,
                                               local_count, cont, depth,
                                               step);
        }
    }
    /* A conjunction's rows complete before any template runs.  Its legs run
     * in turn, so a later leg would read rows that the templates of this
     * leg's earlier rows changed; the legs from this one on are read whole
     * first (oem_match_complete_rows).  A leg with one row, and a last leg,
     * are read once. */
    if (step->relation && (he && cursor->runtime.completes_producers) && len > 1u &&
        program->match_fallbacks[step->relation - 1u].legs > 1u)
        return oem_match_complete_rows(cursor, program, pc, locals,
                                       local_count, cont, depth, step, space,
                                       pattern, rows->rows, len);
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
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .depth = depth,
    };
    if (args)
        args[0] = pattern;
    cursor->frames[cursor->frame_len - 1u].rows = rows;
    return OEM_RUN_CALLED;
}

/* The match step for a cursor under HE's rules, and for any other, whose
 * copy carries none of their tests. */
static __attribute__((noinline)) OemRun oem_match_step_plain(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step) {
    return oem_match_step_mode(cursor, program, pc, locals, local_count, cont,
                               depth, step, false);
}

static __attribute__((noinline)) OemRun oem_match_step_he(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step) {
    return oem_match_step_mode(cursor, program, pc, locals, local_count, cont,
                               depth, step, true);
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
static __attribute__((cold, noinline)) OemRun oem_raise_scopes(
    CettaOpenEquationCursor *cursor, const OemCont *cont) {
    uint32_t floor = cursor->frame_len;
    ArenaMark collected_mark = {0};
    bool recovered = false;
    /* Classify before releasing anything: an outward continuation may
     * have been allocated above an inner receiver's region mark. */
    for (; cont; cont = cont->parent) {
        if (cont->recovers && cursor->runtime.dispatch_recovers) {
            recovered = true;
            break;
        }
        if (cont->pc >= cont->program->step_len ||
            cont->program->steps[cont->pc].kind != OEM_S_YIELD)
            continue;
        uint32_t index = floor;
        while (index > 0u) {
            const OemFrame *frame = &cursor->frames[index - 1u];
            const OemCont *after = frame->cont;
            if (frame->relation == OEM_COLLECT_FRAME && after &&
                after->program == cont->program &&
                after->locals == cont->locals && after->pc > 0u) {
                const OemStep *step = &after->program->steps[after->pc - 1u];
                if (step->kind == OEM_S_HOST &&
                    step->target == OEM_HOST_COLLECT &&
                    after->program->controls[step->control].resume == cont->pc)
                    break;
            }
            index--;
        }
        if (!index)
            return OEM_RUN_HANDOFF;
        collected_mark = cursor->frames[index - 1u].collect->mark;
        floor = index - 1u;
    }
    uint32_t height = UINT32_MAX;
    for (uint32_t index = cursor->frame_len; index > floor; index--) {
        const OemFrame *frame = &cursor->frames[index - 1u];
        if (oem_frame_is_host(frame)) {
            if (frame->host->height == UINT32_MAX)
                return OEM_RUN_HANDOFF;
            if (frame->host->height < height)
                height = frame->host->height;
        }
    }
    if (floor < cursor->frame_len) {
        oem_truncate_frames(cursor, floor);
        arena_reset(&cursor->collected, collected_mark);
    }
    if (recovered) {
        if (height != UINT32_MAX) {
            cursor->cut_height = height;
            return OEM_RUN_CUT;
        }
        return OEM_RUN_FAILED;
    }
    cursor->raise_height = height == UINT32_MAX ? 0u : height;
    return OEM_RUN_RAISE;
}

/* An error no collection within the cursor holds is the value of the call
 * the cursor runs.  It answers in place of whatever the activations bound
 * the destination to, and the host's completion holds it beside the call's
 * other answers.  The answer is published as a raise, which the cursor's
 * run reports as an answer (oem_cursor_run). */
static __attribute__((cold, noinline)) OemRun oem_he_error_answer(
    CettaOpenEquationCursor *cursor) {
    uint32_t slot = UINT32_MAX;
    for (uint32_t index = 0u; index < cursor->query_var_count; index++)
        if (cursor->query_cells[index] == cursor->destination)
            slot = index;
    if (slot == UINT32_MAX)
        return OEM_RUN_HANDOFF;
    oem_forget_answer_vars(cursor);
    Atom *answer = oem_resolve(cursor, cursor->raised_value);
    for (uint32_t index = 0u;
         answer && index < cursor->query_var_count; index++) {
        cursor->query_values[index] = index == slot ? answer
            : oem_resolve(cursor, cursor->query_cells[index]);
        if (!cursor->query_values[index])
            answer = NULL;
    }
    oem_forget_answer_vars(cursor);
    if (!answer)
        return OEM_RUN_HANDOFF;
    cursor->raised_value = answer;
    cursor->stats.answers++;
    return OEM_RUN_RAISE;
}

/* HE's error under `raises_he_errors`: the innermost collection whose
 * answers `cont` returns to holds it, and the branch fails back to the
 * collection's next alternative; with no collection the error is the
 * call's answer (oem_he_error_answer).  Every evaluation between is an
 * argument or a tail of the next, whose value the error replaces. */
static __attribute__((cold, noinline)) OemRun oem_he_raise(
    CettaOpenEquationCursor *cursor, const OemCont *cont) {
    cursor->raise_height = 0u;
    for (const OemCont *entry = cont; entry; entry = entry->parent) {
        if (entry->pc >= entry->program->step_len ||
            entry->program->steps[entry->pc].kind != OEM_S_YIELD)
            continue;
        OemCollectBox *box = NULL;
        for (uint32_t index = cursor->frame_len; !box && index > 0u; index--)
            if (cursor->frames[index - 1u].relation == OEM_COLLECT_FRAME)
                box = cursor->frames[index - 1u].collect;
        if (!box || box->promoted)
            return OEM_RUN_HANDOFF;
        oem_forget_answer_vars(cursor);
        Atom *copy = oem_resolve_in(cursor, cursor->raised_value, false,
                                    &cursor->collected, NULL, true);
        oem_forget_answer_vars(cursor);
        OemCollectItem *item = copy
            ? arena_alloc(&cursor->collected, sizeof(*item)) : NULL;
        if (!item)
            return OEM_RUN_HANDOFF;
        *item = (OemCollectItem){.value = copy, .prev = box->errors};
        box->errors = item;
        box->error_count++;
        return OEM_RUN_FAILED;
    }
    return oem_he_error_answer(cursor);
}

static inline OemRun oem_raised(CettaOpenEquationCursor *cursor,
                                const OemCont *cont, OemRun run) {
    if (run != OEM_RUN_RAISE)
        return run;
    if (cursor->runtime.raises_he_errors)
        return oem_he_raise(cursor, cont);
    return cursor->runtime.raises_abort_collections
        ? oem_raise_scopes(cursor, cont)
        : oem_recovering(cursor, cont) ? OEM_RUN_FAILED : run;
}

/* CETTA_OPEN_EQUATIONS_DEBUG=host names each goal the region leaves to the
 * host by head and arity; host-terms prints its PeTTa presentation and
 * host-raw prints the internal atom representation. */
static int oem_debug_host_goals(void) {
    static int mode = -1;
    if (mode < 0) {
        const char *setting = getenv("CETTA_OPEN_EQUATIONS_DEBUG");
        mode = setting && strcmp(setting, "host-raw") == 0
            ? 3 : setting && strcmp(setting, "host-terms") == 0
            ? 2 : setting && strcmp(setting, "host") == 0 ? 1 : 0;
    }
    return mode;
}

/* Raw callable diagnostics print one application per translation identity,
 * so a large recursive workload exposes representative closure bodies rather
 * than producing one copy per iteration. */
static bool oem_debug_first_callable(const Atom *goal) {
    static _Thread_local uint64_t seen[64];
    Atom *candidate = (Atom *)goal;
    Atom *base = NULL;
    if (!petta_semantics_partial_view(candidate, &base, NULL) &&
        candidate && candidate->kind == ATOM_EXPR && candidate->expr.len)
        candidate = candidate->expr.elems[0];
    else if (base)
        candidate = base;
    while (petta_semantics_partial_view(candidate, &base, NULL))
        candidate = base;
    int64_t identity = 0;
    if (!atom_petta_callable_identity(candidate, &identity) ||
        identity < 0 || identity >= 4096)
        return false;
    uint64_t mask = UINT64_C(1) << ((uint64_t)identity & 63u);
    uint64_t *word = &seen[(uint64_t)identity >> 6u];
    if (*word & mask)
        return false;
    *word |= mask;
    return true;
}

static __attribute__((cold, noinline)) void oem_debug_host_goal(
    const Atom *goal, CettaOpenEquationHostMode mode) {
    if (mode == CETTA_OPEN_EQUATION_HOST_TYPE_ACCEPT ||
        mode == CETTA_OPEN_EQUATION_HOST_TYPE_MATCH) {
        fprintf(stderr, "open-equations type-guard %s\n",
            mode == CETTA_OPEN_EQUATION_HOST_TYPE_MATCH ? "match" : "accept");
        return;
    }
    int debug = oem_debug_host_goals();
    if (debug >= 2) {
        if (debug == 3 && !oem_debug_first_callable(goal))
            return;
        fputs("open-equations host-term ", stderr);
        if (debug == 3)
            atom_print((Atom *)goal, stderr);
        else
            atom_print_petta((Atom *)goal, stderr);
        fputc('\n', stderr);
        return;
    }
    bool expr = goal->kind == ATOM_EXPR && goal->expr.len > 0u;
    const Atom *head = expr ? goal->expr.elems[0] : goal;
    fprintf(stderr, "open-equations host %s/%u\n",
            head->kind == ATOM_SYMBOL ? symbol_bytes(g_symbols, head->sym_id)
            : head->kind == ATOM_VAR  ? "<variable>"
            : head->kind == ATOM_EXPR ? "<expression>"
                                      : "<value>",
            expr ? (unsigned)goal->expr.len - 1u : 0u);
}

static bool oem_store_slot(CettaOpenEquationCursor *cursor, Atom **locals,
                           uint32_t slot, Atom *value);

/* A return-only continuation can pass its symbolic output cells to the host.
 * Validate the whole chain before doing its stores: a later guard, collection
 * or alternative still needs the cursor. Stores do not evaluate those cells. */
static bool oem_host_returns_only(const CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, uint32_t pc,
    const OemCont *cont) {
    if (cursor->frame_len != 0u)
        return false;
    for (;;) {
        if (pc >= program->step_len || program->steps[pc].kind != OEM_S_RET)
            return false;
        if (!cont)
            return true;
        program = cont->program;
        pc = cont->pc;
        cont = cont->parent;
    }
}

static bool oem_host_return_stores(CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, uint32_t pc, Atom **locals,
    const OemCont *cont) {
    while (cont) {
        if (cont->store) {
            uint32_t slot = cont->program->templates[cont->pattern].slot;
            Atom *held = cont->locals[slot];
            Atom *output = held ? held : oem_instantiate(
                cursor, program, locals, program->steps[pc].value);
            Atom *value = output ? oem_deref(cursor, output) : NULL;
            if (!value || (value != held &&
                !oem_store_slot(cursor, cont->locals, slot, value)))
                return false;
        }
        program = cont->program;
        pc = cont->pc;
        locals = cont->locals;
        cont = cont->parent;
    }
    return true;
}

/* A HOST step: the goal and its destination leave the region with one
 * variable per unbound cell they mention (a call variable stands for
 * itself); the frame keeps those cells, and the host passes the variables
 * back with each answer, so the cursor holds no pointer into the host's
 * store. */
static __attribute__((noinline)) OemRun oem_host_with_continuation(
                            CettaOpenEquationCursor *cursor,
                            const OemCont *resume,
                            Atom *goal, Atom *destination,
                            const PettaPlanNode *plan,
                            CettaOpenEquationHostMode mode,
                            bool own_recovers, bool recovers) {
    oem_forget_answer_vars(cursor);
    if (!goal || !destination)
        return OEM_RUN_HANDOFF;
    bool terminal = !resume ? cursor->frame_len == 0u
        : oem_host_returns_only(cursor, resume->program, resume->pc,
                                resume->parent);
    if (terminal && resume && !oem_host_return_stores(
            cursor, resume->program, resume->pc, resume->locals,
            resume->parent))
        return OEM_RUN_HANDOFF;
    if (oem_debug_host_goals())
        oem_debug_host_goal(oem_deref(cursor, goal), mode);
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
    /* Keep the same fresh names as the outgoing goal, including cells that
     * only the caller's result or refined arguments expose. */
    for (uint32_t index = 0u;
         terminal && exported_destination && index < cursor->query_var_count;
         index++) {
        cursor->query_values[index] = oem_export(
            cursor, cursor->query_cells[index]);
        if (!cursor->query_values[index])
            exported_destination = NULL;
    }
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
    if (!oem_push_frame(cursor, resume ? resume->program : cursor->program,
                        OEM_HOST_FRAME, NULL, resume,
                        resume ? resume->depth : 0u))
        return OEM_RUN_HANDOFF;
    cursor->frames[cursor->frame_len - 1u].host = kept;
    cursor->host_goal = exported_goal;
    cursor->host_destination = exported_destination;
    cursor->host_var_count = count;
    cursor->host_plan = plan;
    cursor->host_mode = mode;
    cursor->host_recovers = recovers;
    cursor->host_own_recovers = own_recovers;
    cursor->host_terminal = terminal;
    cursor->stats.host_goals++;
    return OEM_RUN_HOST;
}

static OemRun oem_host_goal(CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, uint32_t resume_pc,
    Atom **locals, uint32_t local_count, const OemCont *cont, uint32_t depth,
    Atom *goal, Atom *destination, const PettaPlanNode *plan,
    CettaOpenEquationHostMode mode) {
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    if (!resume)
        return OEM_RUN_HANDOFF;
    *resume = (OemCont){
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
        .local_count = local_count, .pc = resume_pc, .depth = depth,
    };
    return oem_host_with_continuation(cursor, resume, goal, destination,
        plan, mode, false, oem_recovering(cursor, cont));
}

/* A tail call fills its caller's destination and resumes that caller. Its
 * own exposed output can be unused, notably in an if or case branch. */
static __attribute__((cold, noinline)) OemRun oem_host_tail_goal(
    CettaOpenEquationCursor *cursor,
    const OemCont *cont, Atom *goal, const PettaPlanNode *plan,
    CettaOpenEquationHostMode mode, bool own_recovers) {
    Atom *destination = cursor->destination;
    OemCont *resume = NULL;
    if (cont) {
        if (cont->store &&
            !cont->locals[cont->program->templates[cont->pattern].slot]) {
            Atom *cell = oem_new_cell(cursor);
            if (!cell || !oem_store_slot(cursor, cont->locals,
                    cont->program->templates[cont->pattern].slot, cell))
                return OEM_RUN_HANDOFF;
        }
        destination = oem_instantiate(cursor, cont->program, cont->locals,
                                      cont->pattern);
        resume = arena_alloc(&cursor->region, sizeof(*resume));
        if (!resume)
            return OEM_RUN_HANDOFF;
        *resume = *cont;
        own_recovers |= cont->recovers;
    }
    return oem_host_with_continuation(cursor, resume, goal, destination, plan,
        mode, own_recovers, own_recovers || oem_recovering(cursor, cont));
}

/* A template's unset slots take fresh cells, so it instantiates whole: the
 * variables a host goal leaves the region through. */
static bool oem_fill_cells(CettaOpenEquationCursor *cursor,
                           const CettaOpenEquationProgram *program,
                           Atom **locals, uint32_t template_index,
                           uint32_t depth) {
    const OemTemplate *item = &program->templates[template_index];
    if (depth > OEM_MAX_DEPTH)
        return false;
    if (item->kind == OEM_T_SLOT) {
        if (locals[item->slot])
            return true;
        Atom *cell = oem_new_cell(cursor);
        return cell && oem_store_slot(cursor, locals, item->slot, cell);
    }
    if (item->kind != OEM_T_BUILD)
        return true;
    for (uint32_t index = 0u; index < item->count; index++) {
        if (!oem_fill_cells(cursor, program, locals,
                            program->template_children[item->first + index],
                            depth + 1u))
            return false;
    }
    return true;
}

/* The match leg's own source goes to the host, which evaluates each row's
 * template instance whole, as the language does; its answers fill the
 * leg's output, and the region resumes after the template's code (or, in a
 * tail position, returns to its caller). */
static OemRun oem_match_fallback_host(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, uint32_t local_count, const OemCont *cont, uint32_t depth,
    const OemStep *step) {
    const uint32_t source = program->match_fallbacks[step->relation - 1u].source;
    const uint32_t exposed = program->match_fallbacks[step->relation - 1u].exposed;
    const bool tail = program->match_fallbacks[step->relation - 1u].tail;
    if (!oem_fill_cells(cursor, program, locals, source, 0u))
        return OEM_RUN_HANDOFF;
    Atom *goal = oem_instantiate(cursor, program, locals, source);
    if (!goal)
        return OEM_RUN_HANDOFF;
    if (tail)
        return oem_host_tail_goal(cursor, cont, goal, NULL,
                                  CETTA_OPEN_EQUATION_HOST_SOLVE, false);
    if (!oem_fill_cells(cursor, program, locals, exposed, 0u))
        return OEM_RUN_HANDOFF;
    Atom *destination = oem_instantiate(cursor, program, locals, exposed);
    if (!destination)
        return OEM_RUN_HANDOFF;
    return oem_host_goal(cursor, program, step->target, locals, local_count,
                         cont, depth, goal, destination, NULL,
                         CETTA_OPEN_EQUATION_HOST_SOLVE);
}

static const OemStep *oem_entry_tail(
    const CettaOpenEquationProgram *program, const OemStep *step) {
    return step->target && step->target <= program->step_len &&
           program->steps[step->target - 1u].kind == OEM_S_TAIL
        ? &program->steps[step->target - 1u] : NULL;
}

/* Transfer a live call whose current authority no longer admits its body.
 * An ordinary call has run its source protocol; a functional occurrence in
 * a head has not, and retains the occurrence's full dispatch. */
static __attribute__((cold, noinline)) OemRun oem_live_call_host(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step, Atom *const *args) {
    const OemRelation *callee = &program->relations[step->relation];
    Atom *goal = atom_expr_builder_begin(&cursor->region,
                                         step->arg_count + 1u);
    if (!goal)
        return OEM_RUN_HANDOFF;
    goal->expr.elems[0] = atom_symbol_id(&cursor->region, callee->head);
    for (uint32_t index = 0u; index < step->arg_count; index++)
        goal->expr.elems[index + 1u] = args[index];
    goal = goal->expr.elems[0]
        ? atom_expr_builder_finish(&cursor->region, goal) : NULL;
    CettaOpenEquationHostMode mode = !step->checked_body
        ? CETTA_OPEN_EQUATION_HOST_SOLVE
        : callee->body_is_data ? CETTA_OPEN_EQUATION_HOST_READY_DATA
                              : CETTA_OPEN_EQUATION_HOST_READY_CALL;
    if (step->kind == OEM_S_TAIL)
        return oem_host_tail_goal(cursor, cont, goal, NULL, mode,
                                  step->recovers);
    Atom *destination = NULL;
    OemCont *resume = NULL;
    bool own_recovers = step->recovers;
    if (step->kind == OEM_S_CALL) {
        if (step->store && !locals[program->templates[step->pattern].slot]) {
            Atom *cell = oem_new_cell(cursor);
            if (!cell || !oem_store_slot(cursor, locals,
                    program->templates[step->pattern].slot, cell))
                return OEM_RUN_HANDOFF;
        }
        destination = oem_instantiate(cursor, program, locals, step->pattern);
        resume = arena_alloc(&cursor->region, sizeof(*resume));
        if (!resume)
            return OEM_RUN_HANDOFF;
        *resume = (OemCont){
            .parent = cont, .receiver_parent = oem_receiver_cont(cont),
            .program = program, .locals = locals,
            .local_count = local_count, .pc = pc + 1u, .depth = depth,
        };
    }
    return oem_host_with_continuation(cursor, resume, goal, destination, NULL,
        mode,
        own_recovers, own_recovers || oem_recovering(cursor, cont));
}

static CettaOpenEquationHostMode oem_host_step_mode(const OemStep *step,
                                                    const PettaPlanNode *plan) {
    if (step->target == OEM_HOST_TYPE_ACCEPT)
        return CETTA_OPEN_EQUATION_HOST_TYPE_ACCEPT;
    if (step->target == OEM_HOST_TYPE_MATCH)
        return CETTA_OPEN_EQUATION_HOST_TYPE_MATCH;
    if (step->target == OEM_HOST_COUNTED)
        return CETTA_OPEN_EQUATION_HOST_COUNTED;
    if (step->target == OEM_HOST_OCCURRENCE)
        return CETTA_OPEN_EQUATION_HOST_SOLVE;
    return plan ? CETTA_OPEN_EQUATION_HOST_SOLVE
                : CETTA_OPEN_EQUATION_HOST_APPLY;
}

static __attribute__((noinline)) OemRun oem_host_step(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step) {
    Atom *goal = oem_instantiate(cursor, program, locals, step->value);
    Atom *destination = oem_instantiate(cursor, program, locals, step->pattern);
    bool type_guard = step->target == OEM_HOST_TYPE_ACCEPT ||
                      step->target == OEM_HOST_TYPE_MATCH;
    const PettaPlanNode *plan = type_guard
        ? NULL : program->host_plans[step->relation];
    /* A dynamic application has no static plan. A written call over
     * computed values keeps its static plan with value children. */
    CettaOpenEquationHostMode mode = oem_host_step_mode(step, plan);
    return oem_host_goal(cursor, program, pc + 1u, locals, local_count, cont, depth,
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
    return oem_host_goal(cursor, program, pc + 1u, locals, local_count, cont, depth,
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
    if (cursor->trial_active ||
        cursor->frame_len > oem_slot_birth(header)) {
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

typedef enum {
    OEM_GROUND_MATCH_DECLINE = 0,
    OEM_GROUND_MATCH_FAIL,
    OEM_GROUND_MATCH_OK,
    OEM_GROUND_MATCH_ERROR,
} OemGroundMatch;

/* Match a compiled first-order pattern against a ground algebraic value.
 * Slots bind directly to the value's immutable subterms: groundness proves
 * the occurs check, while repeated slots compare the value already bound.
 * Open local values retain the complete unifier. Ground logical-list
 * patterns use the same head/suffix view as the complete unifier. This is
 * the constructor decision-tree fragment used by strict functional pattern
 * matching, inside the relational machine. */
static OemGroundMatch oem_match_ground_template(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        uint32_t template_index, Atom *term) {
    enum { OEM_GROUND_MATCH_STACK = 16u };
    struct {
        uint32_t template_index;
        Atom *term;
    } stack[OEM_GROUND_MATCH_STACK];
    uint32_t length = 1u;
    const bool open_cons_built = petta_semantics_open_cons_built();
    stack[0].template_index = template_index;
    stack[0].term = term;
    while (length) {
        uint32_t at = --length;
        /* The caller dereferenced the root and established groundness.
         * Every descendant is therefore immutable data rather than a
         * cursor cell; only a previously held slot may still need deref. */
        term = stack[at].term;
        const OemTemplate *item =
            &program->templates[stack[at].template_index];
        if (item->kind == OEM_T_LITERAL) {
            if (open_cons_built &&
                (atom_structural_may_have_list_carrier(item->literal) ||
                 atom_structural_may_have_list_carrier(term)))
                return OEM_GROUND_MATCH_DECLINE;
            if (!atom_eq(item->literal, term))
                return OEM_GROUND_MATCH_FAIL;
            continue;
        }
        if (item->kind == OEM_T_SLOT) {
            Atom *held = locals[item->slot];
            if (!held) {
                if (!oem_store_slot(cursor, locals, item->slot, term))
                    return OEM_GROUND_MATCH_ERROR;
                continue;
            }
            held = oem_deref(cursor, held);
            uint32_t cell = 0u;
            if (oem_is_cell(cursor, held, &cell)) {
                if (!oem_bind(cursor, cell, term))
                    return OEM_GROUND_MATCH_ERROR;
                continue;
            }
            if (held->kind == ATOM_VAR || atom_has_vars(held) ||
                (open_cons_built &&
                 (atom_structural_may_have_list_carrier(held) ||
                  atom_structural_may_have_list_carrier(term))))
                return OEM_GROUND_MATCH_DECLINE;
            if (!atom_eq(held, term))
                return OEM_GROUND_MATCH_FAIL;
            continue;
        }
        if (item->kind == OEM_T_PATTERN_LIST) {
            Atom *first = NULL;
            Atom *rest = NULL;
            if (oem_is_list_cell(term) ||
                petta_semantics_is_cons_constraint(term)) {
                first = term->expr.elems[1];
                rest = term->expr.elems[2];
            } else {
                Atom *const *elements = NULL;
                CettaExprLen count = 0u;
                if (!atom_sequence_view(term, &elements, &count) ||
                    count == 0u)
                    return OEM_GROUND_MATCH_FAIL;
                first = elements[0];
                rest = atom_is_list(term)
                    ? atom_sequence_like(&cursor->region, term,
                                         elements + 1u, count - 1u)
                    : atom_expr_suffix(&cursor->region, term, 1u);
                if (!rest)
                    return OEM_GROUND_MATCH_ERROR;
            }
            if (OEM_GROUND_MATCH_STACK - length < 2u)
                return OEM_GROUND_MATCH_DECLINE;
            stack[length].template_index =
                program->template_children[item->first + 1u];
            stack[length++].term = rest;
            stack[length].template_index =
                program->template_children[item->first];
            stack[length++].term = first;
            continue;
        }
        if (item->kind != OEM_T_BUILD || oem_is_list_cell(term))
            return OEM_GROUND_MATCH_DECLINE;
        if (term->kind != ATOM_EXPR || term->expr.len != item->count ||
            oem_value_representation(term) != item->representation)
            return OEM_GROUND_MATCH_FAIL;
        if (item->count > OEM_GROUND_MATCH_STACK - length)
            return OEM_GROUND_MATCH_DECLINE;
        for (uint32_t child = item->count; child-- > 0u;) {
            stack[length].template_index =
                program->template_children[item->first + child];
            stack[length++].term = term->expr.elems[child];
        }
    }
    return OEM_GROUND_MATCH_OK;
}

static OemUnify oem_meet(CettaOpenEquationCursor *cursor,
                         const CettaOpenEquationProgram *program,
                         Atom **locals, const OemStep *step, Atom *value) {
    if (step->store) {
        uint32_t slot = program->templates[step->pattern].slot;
        if (step->store == OEM_STORE_FIRST || !locals[slot])
            return oem_store_slot(cursor, locals, slot, value)
                ? OEM_UNIFY_OK : OEM_UNIFY_ERROR;
    }
    return oem_unify_template(cursor, program, locals, step->pattern, value, 0u);
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
    if (head->kind != ATOM_EXPR || head->expr.len == 0u) {
        uint32_t cell = 0u;
        return head->kind == ATOM_VAR && cursor->runtime.open_head_is_data &&
            oem_is_cell(cursor, head, &cell);
    }
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

/* The operands of the type-pure operation `head` are values, ground
 * but for a structural test's, run as the host's machine runs it
 * (oem_applied_pure); DECLINED when it is not such an application or the direct
 * run leaves it undecided. */
static __attribute__((noinline)) OemRun oem_applied_pure_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step, Atom *head, Atom *const *operands, uint32_t arity) {
    if (!oem_applied_pure(cursor, program, head->sym_id, arity))
        return OEM_RUN_DECLINED;
    Atom *size = NULL;
    if (arity == 1u && oem_sequence_size_value(
            cursor, head->sym_id, operands[0], &size)) {
        if (!size)
            return OEM_RUN_HANDOFF;
        OemUnify unified = oem_meet(cursor, program, locals, step, size);
        return unified == OEM_UNIFY_ERROR ? OEM_RUN_HANDOFF
            : unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
    }
    Atom *args[OEM_PURE_INLINE_ARGS];
    for (uint32_t index = 0u; index < arity; index++) {
        Atom *arg = oem_resolve_mode(cursor, operands[index],
                                     true);
        if (!arg)
            return OEM_RUN_HANDOFF;
        if (atom_has_vars(arg) && !petta_semantics_structural_test(head->sym_id))
            return OEM_RUN_DECLINED;
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

/* Complete the machine's unforgeable partial carrier over already-evaluated
 * arguments.  The canonical PeTTa machine performs the same concatenation
 * before dispatch; the caller still admits the completed callable through
 * its named-relation or lexical-value contract. */
static Atom *oem_expand_partial_application(
        CettaOpenEquationCursor *cursor, Atom *application,
        Atom *base, Atom *bound) {
    if (!cursor || !application || !base || !bound ||
        application->kind != ATOM_EXPR || application->expr.len == 0u ||
        bound->kind != ATOM_EXPR)
        return NULL;
    CettaExprLen extra = application->expr.len - 1u;
    if (bound->expr.len > UINT64_MAX - extra - 1u)
        return NULL;
    CettaExprLen combined = bound->expr.len + extra + 1u;
    Atom *expanded = atom_expr_builder_begin(&cursor->region, combined);
    if (!expanded)
        return NULL;
    expanded->expr.elems[0] = base;
    if (bound->expr.len)
        memcpy(expanded->expr.elems + 1u, bound->expr.elems,
               sizeof(*expanded->expr.elems) * (size_t)bound->expr.len);
    if (extra)
        memcpy(expanded->expr.elems + 1u + bound->expr.len,
               application->expr.elems + 1u,
               sizeof(*expanded->expr.elems) * (size_t)extra);
    return atom_expr_builder_finish(&cursor->region, expanded);
}

static const CettaOpenEquationProgram *oem_callable_program(
    CettaOpenEquationCursor *cursor, Space *space, Atom *head, uint32_t arity);

/* Certify the admission checks without making an operand copy. Static
 * headers cover constructor resources; singleton support names the only
 * live binding that can introduce another. Unknown support keeps the full
 * resolver. Every binding read is current, including after rollback. */
static bool oem_callable_argument_guard(CettaOpenEquationCursor *cursor,
                                         Atom *argument) {
    argument = oem_deref(cursor, argument);
    if (argument->kind == ATOM_EXPR && argument->expr.len != 0u &&
        atom_is_symbol_id(oem_deref(cursor, argument->expr.elems[0]),
                          g_builtin_syms.error))
        return false;
    for (;;) {
        if (atom_has_thread_local_resource(argument))
            return false;
        if (!atom_has_vars(argument))
            return true;
        VarId support = atom_single_variable_id(argument);
        if (var_id_is_none(support))
            return false;
        if (!cetta_frame_identity_eq(var_epoch_suffix(support), cursor->epoch))
            return true;
        uint32_t cell = (uint32_t)(var_base_id(support) - 1u);
        if (cell >= cursor->cell_len)
            return false;
        Atom *binding = cursor->cells[cell];
        if (!binding)
            return true;
        argument = oem_deref(cursor, binding);
    }
}

/* Arguments have already been evaluated at APPLY. Choose a lexical executor
 * before materializing its arguments: a refused route has no adapter demand.
 * Both routes retain error/resource checks; the relational entry then consumes
 * the ready values in their original cursor store. */
/* A scalar prepared worker lends completed data or a terminal control.
 * Data goes to the caller's destination; raised control uses publication. */
static OemRun oem_prepared_call_result(CettaOpenEquationCursor *cursor,
        CettaCallOutcome returned, Atom **value, Atom **value_out) {
    if (!cetta_call_outcome_shape_valid(returned))
        return oem_service_fault(cursor);
    switch (returned.kind) {
    case CETTA_CALL_VALUE:
        if (returned.result_form != CETTA_CALL_RESULT_COMPLETED_VALUE)
            return oem_service_fault(cursor);
        *value = returned.term;
        return OEM_RUN_CALLED;
    case CETTA_CALL_FAILURE:
        return OEM_RUN_FAILED;
    case CETTA_CALL_INTERRUPTED:
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT;
        return OEM_RUN_HANDOFF;
    case CETTA_CALL_RAISED:
        return oem_publish_raise(cursor, returned.term, value_out);
    case CETTA_CALL_SUSPENDED:
        return oem_service_fault(cursor);
    }
    return oem_service_fault(cursor);
}

static OemRun oem_entered_call_result(CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, uint32_t pc, Atom **locals,
    uint32_t local_count, const OemCont *cont, uint32_t depth,
    const OemStep *step, CettaCallOutcome outcome, Atom **value_out);

/* One callee return is copied before its loan is released. Only an
 * unfinished callee escapes to the caller, so only it detaches its inputs. */
static OemRun oem_pull_callable_answer(CettaOpenEquationCursor *cursor,
        uint32_t frame_index, CettaCallOutcome *outcome) {
    OemFrame *frame = &cursor->frames[frame_index];
    CettaPreparedPureAnswerCursor *callee = frame->prepared_answers;
    CettaOwnedReturn returned = {0};
    CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
    CettaOwnedExecutionStep step = cursor->runtime.next_callable_answer(
        cursor->runtime.context, frame->cont->program->space, callee,
        &returned, &completion);
    if (step == CETTA_OWNED_EXECUTION_COMPLETE && completion == CETTA_EVAL_COMPLETE) {
        oem_pop_frame(cursor);
        return OEM_RUN_FAILED;
    }
    if (step != CETTA_OWNED_EXECUTION_RETURN) {
        if (!frame->next && !cetta_prepared_pure_answer_cursor_native_started(callee) &&
            cetta_prepared_pure_answer_cursor_handoff_reason(callee) ==
                CETTA_PREPARED_PURE_HANDOFF_UNSUPPORTED) {
            oem_pop_frame(cursor);
            return OEM_RUN_DECLINED;
        }
        cursor->handoff = completion == CETTA_EVAL_INCOMPLETE_CANCELLED
            ? CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT
            : completion == CETTA_EVAL_INCOMPLETE_INVALIDATED
                ? CETTA_OPEN_EQUATION_HANDOFF_INVALIDATED
                : completion == CETTA_EVAL_INCOMPLETE_CAPACITY
                    ? CETTA_OPEN_EQUATION_HANDOFF_CAPACITY
                    : CETTA_OPEN_EQUATION_HANDOFF_SERVICE_ERROR;
        return OEM_RUN_FAULT;
    }
    if (!cetta_call_outcome_shape_valid(returned.outcome) || returned.environment ||
        (returned.outcome.kind == CETTA_CALL_VALUE &&
         (returned.outcome.result_form != CETTA_CALL_RESULT_COMPLETED_VALUE ||
          atom_has_vars(returned.outcome.term) ||
          atom_has_thread_local_resource(returned.outcome.term))))
        return oem_service_fault(cursor);
    *outcome = returned.outcome;
    if (outcome->term) {
        outcome->term = cetta_prepared_pure_answer_cursor_copy_value(
            callee, &cursor->region, outcome->term);
        if (!outcome->term)
            goto capacity;
    }
    if (outcome->delayed)
        return oem_service_fault(cursor);
    frame = &cursor->frames[frame_index];
    frame->next++;
    if (returned.final || outcome->kind != CETTA_CALL_VALUE) {
        oem_pop_frame(cursor);
    } else {
        /* The frame's logical snapshot stays fixed. Its storage mark keeps
         * this closed callee's continuation below caller rollback until the
         * parent relocates the arena or retires the frame. */
        if (cetta_prepared_pure_answer_cursor_uses_arena(callee, &cursor->region))
            frame->mark = arena_mark(&cursor->region);
        cetta_prepared_pure_answer_cursor_suspend(callee);
    }
    return OEM_RUN_CALLED;
capacity:
    cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
    return OEM_RUN_FAULT;
}

static bool oem_prepared_answer_admitted(void *context, const Atom *shape) {
    CettaOpenEquationCursor *cursor = context;
    if (!cursor->frame_len)
        return true;
    OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
    if (frame->relation != OEM_PREPARED_FRAME || !frame->cont)
        return true;
    const OemCont *resume = frame->cont;
    const OemTemplate *pattern = &resume->program->templates[resume->pattern];
    if (pattern->kind != OEM_T_SLOT || !resume->locals[pattern->slot])
        return true;
    Atom *held = oem_deref(cursor, resume->locals[pattern->slot]);
    if (atom_structural_may_have_list_carrier(held) ||
        oem_row_may_unify(cursor, held, (Atom *)shape, 0u))
        return true;
    if (frame->next != UINT32_MAX)
        frame->next++;
    return false;
}

static OemRun oem_start_callable_answers(CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, uint32_t pc, Atom **locals,
        uint32_t local_count, const OemCont *cont, uint32_t depth,
        const OemStep *step, void *prepared, Atom *const *arguments,
        uint32_t arity, Atom **value_out) {
    if (!cursor->runtime.open_callable_answers || !cursor->runtime.next_callable_answer)
        return OEM_RUN_DECLINED;
    const OemTemplate *destination = &program->templates[step->pattern];
    CettaPreparedPureAnswerCursor *callee = cursor->runtime.open_callable_answers(
        cursor->runtime.context, prepared, &cursor->region, arguments, arity);
    if (!callee)
        return OEM_RUN_DECLINED;
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    if (!resume)
        goto before_entry_capacity;
    if (step->store && !locals[destination->slot]) {
        Atom *cell = oem_new_cell(cursor);
        if (!cell || !oem_store_slot(cursor, locals, destination->slot, cell))
            goto before_entry_capacity;
    }
    *resume = (OemCont){.parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .pc = pc + 1u, .locals = locals,
        .local_count = local_count, .pattern = step->pattern,
        .depth = depth, .recovers = step->recovers};
    if (!oem_push_frame(cursor, NULL, OEM_PREPARED_FRAME, NULL, resume, depth + 1u))
        goto before_entry_capacity;
    uint32_t index = cursor->frame_len - 1u;
    cursor->frames[index].prepared_answers = callee;
    cursor->external_frames++;
    uint32_t cell = 0u;
    bool fresh = destination->kind == OEM_T_SLOT &&
        (!locals[destination->slot] || oem_is_cell(cursor,
            oem_deref(cursor, locals[destination->slot]), &cell));
    if (!fresh && !cetta_prepared_pure_answer_cursor_set_admission(
            callee, oem_prepared_answer_admitted, cursor))
        return oem_service_fault(cursor);
    CettaCallOutcome returned = {0};
    OemRun run = oem_pull_callable_answer(cursor, index, &returned);
    if (run != OEM_RUN_CALLED)
        return run;
    return oem_entered_call_result(cursor, program, pc, locals, local_count,
        cont, depth, step, returned, value_out);
before_entry_capacity:
    cetta_prepared_pure_answer_cursor_close(callee);
    cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
    return OEM_RUN_HANDOFF;
}

static OemRun oem_lexical_callable_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, uint32_t pc, Atom **locals,
        uint32_t local_count, const OemCont *cont, uint32_t depth,
        const OemStep *step, Atom *head, Atom *const *operands,
        uint32_t arity, const CettaOpenEquationProgram **target_out,
        Atom **value_out) {
    enum { OEM_LEXICAL_INLINE_ARGS = 64u };
    *target_out = NULL;
    if (!operands ||
        arity == 0u || arity > OEM_LEXICAL_INLINE_ARGS)
        return OEM_RUN_DECLINED;
    int64_t identity = 0;
    CettaExprLen callable_arity = 0u;
    if (!atom_petta_callable_identity(head, &identity))
        return OEM_RUN_DECLINED;
    OemPureCallable *entry = NULL;
    bool pure_available =
        !cursor->runtime.activation_budget && !cursor->runtime.depth_bound &&
        cursor->runtime.prepare_pure_callable &&
        cursor->runtime.execute_pure_callable &&
        cursor->runtime.release_pure_callable;
    if (pure_available) {
        for (uint32_t index = 0u; index < cursor->pure_callable_len; index++) {
            OemPureCallable *candidate = &cursor->pure_callables[index];
            if (candidate->identity == identity && candidate->arity == arity) {
                entry = candidate;
                break;
            }
        }
    }
    /* An entry is published only after exact saturation was checked.
     * Nominal callable identity retains that immutable parameter sequence. */
    if (entry)
        callable_arity = entry->arity;
    else if (!petta_semantics_lambda_arity(head, &callable_arity))
        return OEM_RUN_DECLINED;
    if ((CettaExprLen)arity > callable_arity)
        return OEM_RUN_DECLINED;
    bool partial = (CettaExprLen)arity < callable_arity;
    if (!partial && pure_available) {
        if (!entry) {
            if (!oem_reserve(
                    (void **)&cursor->pure_callables,
                    &cursor->pure_callable_cap,
                    cursor->pure_callable_len + 1u,
                    sizeof(*cursor->pure_callables)))
                return OEM_RUN_HANDOFF;
            entry = &cursor->pure_callables[cursor->pure_callable_len++];
            *entry = (OemPureCallable){
                .identity = identity,
                .arity = arity,
                .prepared = cursor->runtime.prepare_pure_callable(
                    cursor->runtime.context, program->space, &cursor->names,
                    head, arity),
            };
            entry->answers_only = entry->prepared && cursor->runtime.callable_answers_only &&
                cursor->runtime.callable_answers_only(cursor->runtime.context, entry->prepared);
        }
    }
    const CettaOpenEquationProgram *target = NULL;
    if (!partial && (!entry || !entry->prepared)) {
        target = oem_callable_program(cursor, program->space, head, arity);
        if (!target)
            return OEM_RUN_DECLINED;
    }

    Atom *arguments[OEM_LEXICAL_INLINE_ARGS];
    for (uint32_t index = 0u; index < arity; index++) {
        Atom *argument = operands[index];
        if (target && oem_callable_argument_guard(cursor, argument))
            continue;
        arguments[index] = oem_resolve_mode(cursor, argument, true);
        if (!arguments[index])
            return OEM_RUN_HANDOFF;
        if (atom_is_error(arguments[index]) ||
            atom_has_thread_local_resource(arguments[index]))
            return OEM_RUN_DECLINED;
    }
    if (partial) {
        Atom *value = petta_semantics_partial_value(
            &cursor->region, head, arguments, arity);
        if (!value)
            return OEM_RUN_HANDOFF;
        OemUnify unified = oem_meet(cursor, program, locals, step, value);
        if (unified == OEM_UNIFY_ERROR)
            return OEM_RUN_HANDOFF;
        if (unified == OEM_UNIFY_OK)
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_OPEN_EQUATION_LAMBDA_PARTIAL);
        return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
    }
    if (target) {
        *target_out = target;
        return OEM_RUN_DECLINED;
    }
    CettaCallOutcome returned;
    bool executed = false;
    if (!entry->answers_only) {
        returned = (CettaCallOutcome){0};
        executed = cursor->runtime.execute_pure_callable(
            cursor->runtime.context, entry->prepared, &cursor->region,
            arguments, arity, &returned);
    }
    if (!executed) {
        OemRun answers = oem_start_callable_answers(cursor, program, pc, locals,
            local_count, cont, depth, step, entry->prepared, arguments, arity, value_out);
        if (answers != OEM_RUN_DECLINED)
            return answers;
        *target_out = oem_callable_program(cursor, program->space, head, arity);
        return OEM_RUN_DECLINED;
    }
    OemRun completed = oem_entered_call_result(cursor, program, pc, locals,
        local_count, cont, depth, step, returned, value_out);
    if (completed == OEM_RUN_CALLED)
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PURE_CALLABLE);
    return completed;
}

/* An exact lexical entry, reused only under the source and host authority
 * that compiled it. Older entries remain owned while their continuations
 * can still run; the host leases them when a continuation leaves the cursor. */
static const CettaOpenEquationProgram *oem_callable_program(
    CettaOpenEquationCursor *cursor, Space *space, Atom *head, uint32_t arity) {
    int64_t identity = 0;
    CettaExprLen full_arity = 0u;
    if (!cursor->runtime.compile_callable || cursor->runtime.activation_budget ||
        cursor->runtime.depth_bound ||
        !atom_petta_callable_identity(head, &identity) ||
        !petta_semantics_lambda_arity(head, &full_arity) || full_arity != arity)
        return NULL;
    SpaceProgramToken now = space_program_token(space);
    for (uint32_t index = 0u; index < cursor->callable_len; index++) {
        OemCallable *entry = &cursor->callables[index];
        if (entry->identity == identity && entry->arity == arity &&
            entry->authority_epoch == cursor->authority_epoch &&
            space_program_token_eq(entry->source, now))
            return entry->program;
    }
    if (!oem_reserve((void **)&cursor->callables, &cursor->callable_cap,
                     cursor->callable_len + 1u, sizeof(*cursor->callables)))
        return NULL;
    CettaOpenEquationProgram *prepared = cursor->runtime.compile_callable(
        cursor->runtime.context, space, head, arity);
    if (!space_program_token_is_current(now)) {
        cetta_open_equation_program_release(prepared);
        return NULL;
    }
    cursor->callables[cursor->callable_len++] = (OemCallable){
        .identity = identity, .arity = arity, .source = now,
        .authority_epoch = cursor->authority_epoch, .program = prepared,
    };
    return prepared;
}

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
                             uint32_t depth, const OemStep *step, Atom **value_out) {
    const OemTemplate *goal = &program->templates[step->value];
    if (goal->kind != OEM_T_BUILD || goal->count == 0u)
        return OEM_RUN_DECLINED;
    Atom *head = oem_instantiate(cursor, program, locals,
                                 program->template_children[goal->first]);
    if (!head)
        return OEM_RUN_HANDOFF;
    head = oem_deref(cursor, head);
    Atom *data = NULL;
    Atom *partial_base = NULL;
    Atom *partial_bound = NULL;
    bool expanded_partial = false;
    if (petta_semantics_partial_view(
            head, &partial_base, &partial_bound)) {
        partial_base = oem_deref(cursor, partial_base);
        int64_t identity = 0;
        if (partial_base->kind != ATOM_SYMBOL &&
            !atom_petta_callable_identity(partial_base, &identity))
            return OEM_RUN_DECLINED;
        /* Larger lexical entries have no pure-plan route. Refuse before
         * materializing the completed vector when their body is not admitted. */
        if (partial_base->kind != ATOM_SYMBOL && partial_bound->kind == ATOM_EXPR) {
            uint32_t supplied = goal->count - 1u;
            if (partial_bound->expr.len > 64u ||
                supplied > 64u - partial_bound->expr.len)
                return OEM_RUN_DECLINED;
            uint32_t count = (uint32_t)partial_bound->expr.len + supplied;
            if (count > 2u && !oem_callable_program(
                    cursor, program->space, partial_base, count))
                return OEM_RUN_DECLINED;
        }
        data = oem_instantiate(cursor, program, locals, step->value);
        data = data ? oem_expand_partial_application(
                          cursor, data, partial_base, partial_bound)
                    : NULL;
        if (!data)
            return OEM_RUN_HANDOFF;
        head = partial_base;
        expanded_partial = true;
    }
    bool symbol = head->kind == ATOM_SYMBOL;
    uint32_t arity = data ? (uint32_t)data->expr.len - 1u : goal->count - 1u;
    enum { OEM_APPLY_INLINE_ARGS = 64u };
    Atom *inline_elements[OEM_APPLY_INLINE_ARGS + 1u];
    Atom **elements = data ? data->expr.elems
        : arity <= OEM_APPLY_INLINE_ARGS ? inline_elements
        : arena_alloc(&cursor->region, sizeof(*elements) * goal->count);
    if (!elements)
        return OEM_RUN_HANDOFF;
    if (!data) {
        /* Direct calls read their program-owned operands. Materialize syntax
         * only for a host inquiry or an application that is itself data. */
        elements[0] = head;
        for (uint32_t index = 0u; index < arity; index++) {
            Atom *operand = oem_instantiate(cursor, program, locals,
                program->template_children[goal->first + index + 1u]);
            if (!operand)
                return OEM_RUN_HANDOFF;
            elements[index + 1u] = oem_deref(cursor, operand);
        }
    }
    Atom *const *operands = elements + 1u;
    const CettaOpenEquationProgram *target = NULL;
    if (!symbol) {
        OemRun lexical = oem_lexical_callable_step(
            cursor, program, pc, locals, local_count, cont, depth,
            step, head, operands, arity, &target, value_out);
        if (lexical != OEM_RUN_DECLINED)
            return lexical;
        if (!target && !oem_application_inert(cursor, head))
            return OEM_RUN_DECLINED;
    }
    if (symbol && step->target == OEM_HOST_APPLY && cursor->runtime.current)
        target =
            oem_enter_evaluated(cursor, program->space, head->sym_id, arity);
    if (symbol && !target && arity == 2u) {
        /* A grounded operation over plain scalars, which the host applies
         * as its ready call: the scalar evaluator's exact result, unless the
         * program defines equations of the head, which the host's call
         * would enter instead. */
        Atom *args[2] = {
            oem_resolve_mode(cursor, operands[0], true),
            oem_resolve_mode(cursor, operands[1], true),
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
    if (symbol && !target) {
        OemRun run = oem_applied_pure_step(cursor, program, locals, step,
                                           head, operands, arity);
        if (run != OEM_RUN_DECLINED)
            return run;
    }
    if (symbol && !target) {
        if (!data)
            data = atom_expr(&cursor->region, elements, arity + 1u);
        if (!data)
            return OEM_RUN_HANDOFF;
        if (!oem_symbol_application_inert(cursor, program, data))
            return OEM_RUN_DECLINED;
    }
    if (target) {
        Atom **args = arena_alloc(&cursor->region,
                                  sizeof(*args) * (arity ? arity : 1u));
        OemCont *record = arena_alloc(&cursor->region, sizeof(*record));
        if (!args || !record)
            return OEM_RUN_HANDOFF;
        for (uint32_t index = 0u; index < arity; index++)
            args[index] = operands[index];
        /* The step's output slot, stored at its first occurrence, takes a
         * cell for the callee to fill, as it does when the host fills it. */
        if (step->store && !locals[program->templates[step->pattern].slot]) {
            Atom *cell = oem_new_cell(cursor);
            if (!cell ||
                !oem_store_slot(cursor, locals,
                                program->templates[step->pattern].slot,
                                cell))
                return OEM_RUN_HANDOFF;
        }
        *record = (OemCont){
            .parent = cont, .receiver_parent = oem_receiver_cont(cont),
            .program = program, .locals = locals,
            .local_count = local_count, .pc = pc + 1u,
            .pattern = step->pattern, .depth = depth,
            .recovers = step->recovers,
        };
        if (step->recovers)
            cursor->recovering = true;
        bool entered = oem_push_frame(
            cursor, target, 0u, args, record, depth + 1u);
        if (entered && expanded_partial)
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PARTIAL_ENTER);
        return entered ? OEM_RUN_ENTERED : OEM_RUN_HANDOFF;
    }
    if (!data)
        data = atom_expr(&cursor->region, elements, arity + 1u);
    OemUnify built = data
        ? oem_meet(cursor, program, locals, step, data)
        : OEM_UNIFY_ERROR;
    if (built == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return built == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* PeTTa's `sealed vars goal` is a CBPV boundary: both arguments are raw
 * values, listed variables are freshened in the goal, and the resulting
 * computation runs. Keep preparation and the enclosing continuation in the
 * region; only the copied computation crosses to the host. */
static OemRun oem_sealed_step(CettaOpenEquationCursor *cursor,
                              const CettaOpenEquationProgram *program,
                              uint32_t pc, Atom **locals,
                              uint32_t local_count, const OemCont *cont,
                              uint32_t depth, const OemStep *step) {
    const OemTemplate *source = &program->templates[step->value];
    if (source->kind != OEM_T_BUILD || source->count != 3u)
        return OEM_RUN_HANDOFF;
    Atom *listed = oem_instantiate(
        cursor, program, locals,
        program->template_children[source->first + 1u]);
    Atom *goal = oem_instantiate(
        cursor, program, locals,
        program->template_children[source->first + 2u]);
    listed = listed ? oem_resolve_operand(cursor, listed) : NULL;
    goal = goal ? oem_resolve_operand(cursor, goal) : NULL;
    Atom *copy = listed && goal
        ? rename_vars_only(&cursor->region, goal, listed) : NULL;
    if (!copy)
        return OEM_RUN_HANDOFF;
    if (step->store && !locals[program->templates[step->pattern].slot]) {
        Atom *cell = oem_new_cell(cursor);
        if (!cell || !oem_store_slot(
                cursor, locals, program->templates[step->pattern].slot, cell))
            return OEM_RUN_HANDOFF;
    }
    Atom *destination = oem_instantiate(
        cursor, program, locals, step->pattern);
    if (!destination)
        return OEM_RUN_HANDOFF;
    const PettaPlanNode *source_plan = program->host_plans[step->relation];
    const PettaPlanNode *goal_plan = petta_plan_child(source_plan, 2u);
    OemRun run = oem_host_goal(
        cursor, program, pc + 1u, locals, local_count, cont, depth,
        copy, destination, goal_plan, CETTA_OPEN_EQUATION_HOST_SOLVE);
    if (run == OEM_RUN_HOST)
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_SEALED_CONTINUATION);
    return run;
}

/* An `add-atom`: the host admits the payload as its machine's ready path
 * does, and its result meets the step's destination; an error it returns
 * is raised, as the machine raises it.  The host admits a payload with an
 * unbound cell where the set keeps the row as its alpha key, which no
 * naming of the cells changes.  A space the reference does not name, or a
 * call the host declines, is the host's ordinary path.  The call is built in the scratch arena, since the space
 * keeps its own copy; the region, which its region parts come from, is
 * the source the host may memoize them by. */
static OemRun oem_admit_step(CettaOpenEquationCursor *cursor,
                             const CettaOpenEquationProgram *program,
                             Atom **locals, const OemStep *step,
                             Atom **value_out) {
    if (!cursor->runtime.admit_atom)
        return OEM_RUN_DECLINED;
    Arena *scratch = oem_scratch(cursor);
    ArenaMark mark = arena_mark(scratch);
    Atom *call = oem_instantiate_in(cursor, scratch, program, locals,
                                    step->value);
    call = call ? oem_resolve_in(cursor, call, true, scratch, NULL, false)
                : NULL;
    OemRun run = OEM_RUN_DECLINED;
    Atom *result = NULL;
    if (!call) {
        run = OEM_RUN_HANDOFF;
    } else if (call->kind == ATOM_EXPR && call->expr.len == 3u) {
        Atom *reference = call->expr.elems[1];
        Space *target = cursor->runtime.resolve_space
            ? cursor->runtime.resolve_space(cursor->runtime.context,
                                            program->space, &cursor->region,
                                            reference)
            : NULL;
        if (!target && atom_is_symbol_id(reference, g_builtin_syms.self))
            target = program->space;
        if (target && cursor->runtime.admit_atom(
                cursor->runtime.context, program->space, target,
                &cursor->region, call, &result)) {
            cursor->effected = true;
            run = !result ? oem_service_fault(cursor) : atom_is_error(result)
                ? oem_publish_raise(cursor, result, value_out)
                : OEM_RUN_CALLED;
        }
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

/* PeTTa's named state over values: the host reads or changes the state as
 * its machine does, and its answer meets the step's destination.  What the
 * host declines (an unset name, which raises; a value with an unbound
 * cell) is the host's.  A change is an effect, which a bounded run leaves
 * to the unbounded run that follows, as it leaves `add-atom`. */
static __attribute__((noinline)) OemRun oem_state_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step) {
    if (!cursor->runtime.named_state)
        return OEM_RUN_DECLINED;
    Atom *call = oem_instantiate(cursor, program, locals, step->value);
    call = call ? oem_resolve_mode(cursor, call, true) : NULL;
    if (!call)
        return OEM_RUN_HANDOFF;
    if (call->kind == ATOM_EXPR && call->expr.len == 3u &&
        (cursor->runtime.activation_budget || cursor->runtime.depth_bound)) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return OEM_RUN_HANDOFF;
    }
    Atom *result = NULL;
    if (!cursor->runtime.named_state(cursor->runtime.context, program->space,
                                     &cursor->region, call, &result))
        return OEM_RUN_DECLINED;
    if (call->kind == ATOM_EXPR && call->expr.len == 3u)
        cursor->effected = true;
    if (!result)
        return oem_service_fault(cursor);
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
/* The compiler has selected the operation and prepared its one argument.
 * Runtime binding and representation checks still decide whether this
 * occurrence can be read here. No result evaluation is licensed by this view. */
static __attribute__((noinline)) OemRun oem_sequence_projection_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step, const OemTemplate *goal) {
    if (goal->kind != OEM_T_BUILD || goal->count != 2u)
        return OEM_RUN_DECLINED;
    Atom *list = oem_instantiate(cursor, program, locals,
        program->template_children[goal->first + 1u]);
    list = list ? oem_resolve_mode(cursor, list, true) : NULL;
    if (!list)
        return OEM_RUN_HANDOFF;
    if (petta_semantics_is_open_cons_value(list)) {
        OemUnify unified = oem_meet(
            cursor, program, locals, step,
            list->expr.elems[
                step->target == OEM_HOST_SEQUENCE_HEAD ? 1u : 2u]);
        if (unified == OEM_UNIFY_ERROR)
            return OEM_RUN_HANDOFF;
        if (unified == OEM_UNIFY_OK)
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_OPEN_EQUATION_OPEN_CONS_PROJECTION);
        return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
    }
    if (list->kind == ATOM_VAR ||
        (petta_semantics_open_cons_built() &&
         atom_structural_may_have_list_carrier(list)) ||
        (list->kind == ATOM_EXPR && !oem_plain_list(list)))
        return OEM_RUN_DECLINED;
    CettaSequenceProjectionCall call = {
        .projection = step->target == OEM_HOST_SEQUENCE_HEAD
            ? CETTA_SEQUENCE_PROJECTION_HEAD : CETTA_SEQUENCE_PROJECTION_TAIL,
        .policy = cursor->runtime.raises_empty_car
            ? CETTA_SEQUENCE_PETTA_RAISE_EMPTY_HEAD : CETTA_SEQUENCE_PETTA_VALUE,
    };
    CettaSequenceProjectionView view = cetta_sequence_projection_view(
        call, list, list->kind == ATOM_EXPR,
        list->kind == ATOM_EXPR ? list->expr.elems : NULL,
        list->kind == ATOM_EXPR ? list->expr.len : 0u);
    /* oem_plain_list excludes LIST and every other internal tag. */
    view.representation = CETTA_SEQUENCE_ORDINARY_EXPRESSION;
    if (view.result == CETTA_SEQUENCE_PROJECT_RAISE)
        return OEM_RUN_DECLINED;
    Atom *result = cetta_sequence_projection_materialize(&cursor->region, &view);
    if (!result)
        return OEM_RUN_HANDOFF;
    OemUnify unified = oem_meet(cursor, program, locals, step, result);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

static __attribute__((noinline)) OemRun oem_list_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step, const OemTemplate *goal) {
    Atom *args[2];
    uint32_t count = goal->count - 1u;
    if (goal->kind != OEM_T_BUILD || count == 0u || count > 2u)
        return OEM_RUN_DECLINED;
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
            return OEM_RUN_DECLINED;
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
                return OEM_RUN_DECLINED;
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
            return OEM_RUN_DECLINED;
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
    bool append = form == PETTA_FORM_APPEND ||
        step->op == g_builtin_syms.union_atom;
    if (!oem_plain_list(list) || (append && !oem_plain_list(args[0])))
        return OEM_RUN_DECLINED;
    if (append) {
        CettaExprLen left = args[0]->expr.len;
        CettaExprLen length = left + list->expr.len;
        if (length < left || !cetta_expr_len_mul_fits_size(length,
                                                           sizeof(Atom *)))
            return OEM_RUN_DECLINED;
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
            return OEM_RUN_DECLINED;
        result = atom_int(&cursor->region, (int64_t)list->expr.len);
    } else if (form == PETTA_FORM_LIST_TO_SET ||
               step->op == g_builtin_syms.unique_atom) {
        result = petta_semantics_list_to_set(&cursor->region, list);
    } else if (form == PETTA_FORM_ALPHA_UNIQUE) {
        result = petta_semantics_alpha_unique(&cursor->region, list);
    } else if (form == PETTA_FORM_EXCLUDE_ITEM) {
        result = petta_semantics_exclude_item(&cursor->region, args[0], list);
    } else {
        return OEM_RUN_DECLINED;
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

/* An existence observer, `(== () (collapse (once (match S P T))))` with a
 * template T the observation ignores (an atomic constant, or a value):
 * whether the space S has a row matching P, as the search machine answers
 * it where the space proves that ground matching is structural membership
 * (space_match_exists_ground_exact).  A template that is no value when the
 * step runs (with variables, the empty expression, Empty, an error), a named
 * space with a schema, a symbol naming no space, a pattern with variables
 * and a space that cannot prove it are the host's, which answers the goal
 * whole. */
static __attribute__((noinline)) OemRun oem_exists_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step) {
    /* The spelling the compiler read (OemStep.existence): the side holding
     * the reification, a count of it, and once. */
    uint8_t spelling = step->existence;
    uint32_t side = oem_template_child(
        program, step->value, spelling & OEM_EXISTS_SIDE2 ? 2u : 1u);
    if (spelling & OEM_EXISTS_COUNTED)
        side = oem_template_child(program, side, 1u);
    uint32_t match = oem_template_child(program, side, 1u);
    if (spelling & OEM_EXISTS_ONCE)
        match = oem_template_child(program, match, 1u);
    bool negated = spelling & OEM_EXISTS_NEGATED;
    uint32_t template_index = oem_template_child(program, match, 3u);
    const OemTemplate *template_source = &program->templates[template_index];
    if (template_source->kind != OEM_T_LITERAL ||
        !oem_existence_constant_template(template_source->literal)) {
        Atom *template = oem_instantiate(cursor, program, locals,
                                         template_index);
        if (!template)
            return OEM_RUN_HANDOFF;
        template = oem_resolve_mode(cursor, template, true);
        if (!template)
            return OEM_RUN_HANDOFF;
        if (atom_has_vars(template) ||
            (template->kind == ATOM_EXPR && template->expr.len == 0u) ||
            atom_is_empty(template) || atom_is_error(template))
            return OEM_RUN_DECLINED;
    }
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
            return OEM_RUN_DECLINED;
    }
    if (!space || atom_has_vars(pattern))
        return OEM_RUN_DECLINED;
    bool applicable = false;
    bool found = space_match_exists_ground_exact(space, pattern, &applicable);
    if (!applicable)
        return OEM_RUN_DECLINED;
    Atom *truth = oem_truth(cursor, negated ? found : !found);
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
 * error, or no result, declines to the authoritative host operation. */
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
    bool minmax = op == g_builtin_syms.petta_min ||
                  op == g_builtin_syms.petta_max;
    if (minmax && count == 2u) {
        Atom *comparison = atom_symbol_id(
            &cursor->region, op == g_builtin_syms.petta_min
                ? g_builtin_syms.op_le : g_builtin_syms.op_ge);
        CettaPlainScalar ordered;
        if (!comparison ||
            !oem_scalar_operation(comparison, args, count, &ordered) ||
            ordered.kind != CETTA_PLAIN_SCALAR_BOOL)
            return OEM_RUN_DECLINED;
        /* PeTTa defines min/max by comparing the two ready values and
         * returning one of them. Retain that operand's exact representation
         * (including mixed Int/Float ties), rather than rematerializing it. */
        result = ordered.as.boolean ? args[0] : args[1];
    } else if (count == 2u && oem_int_value(args[0], &left) &&
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
            return OEM_RUN_DECLINED;
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
    if (unified == OEM_UNIFY_OK && minmax)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OPEN_EQUATION_MINMAX);
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
        return OEM_RUN_DECLINED;
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
        return OEM_RUN_DECLINED;
    if (!type)
        return OEM_RUN_HANDOFF;
    OemUnify unified = oem_meet(cursor, program, locals, step, type);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* Boolean unification retains a successful refinement and rolls back a
 * mismatch before returning false: a trial (oem_trial_begin).  No search
 * choice or cut delimiter is installed by it. */
static OemUnify oem_unify_trial(CettaOpenEquationCursor *cursor,
                                Atom *left, Atom *right) {
    OemTrial trial;
    oem_trial_begin(cursor, &trial);
    OemUnify result = oem_unify(cursor, left, right);
    oem_trial_end(cursor, &trial, result);
    return result;
}

static __attribute__((noinline)) OemRun oem_observation_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step, const OemTemplate *goal) {
    uint32_t count = step->op == g_builtin_syms.equals ? 2u : 1u;
    if (goal->kind != OEM_T_BUILD || goal->count != count + 1u)
        return OEM_RUN_DECLINED;
    Atom *args[2];
    for (uint32_t index = 0u; index < count; index++) {
        args[index] = oem_instantiate(cursor, program, locals,
            program->template_children[goal->first + index + 1u]);
        if (!args[index])
            return OEM_RUN_HANDOFF;
    }
    bool answer = false;
    if (step->op == g_builtin_syms.equals) {
        OemUnify unified = oem_unify_trial(cursor, args[0], args[1]);
        if (unified == OEM_UNIFY_ERROR)
            return OEM_RUN_HANDOFF;
        answer = unified == OEM_UNIFY_OK;
    } else {
        Atom *value = oem_resolve_mode(cursor, args[0], true);
        if (!value)
            return OEM_RUN_HANDOFF;
        switch (petta_semantics_form(step->op)) {
        case PETTA_FORM_IS_VAR:
            answer = value->kind == ATOM_VAR;
            break;
        case PETTA_FORM_IS_EXPR:
            answer = petta_semantics_is_closed_list(value);
            break;
        case PETTA_FORM_IS_GROUND:
            answer = !atom_has_vars(value);
            break;
        case PETTA_FORM_IS_SPACE:
            answer = (value->kind == ATOM_SYMBOL &&
                      symbol_bytes(g_symbols, value->sym_id)[0] == '&') ||
                (value->kind == ATOM_GROUNDED &&
                 value->ground.gkind == GV_SPACE && value->ground.ptr);
            break;
        default:
            return OEM_RUN_DECLINED;
        }
    }
    Atom *truth = oem_truth(cursor, answer);
    if (!truth)
        return OEM_RUN_HANDOFF;
    OemUnify unified = oem_meet(cursor, program, locals, step, truth);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* SWI's is-member/3 returns one true occurrence per unifying member, or one
 * false when none unifies. Ground inputs cannot acquire bindings. The zero-
 * or-one fragment therefore has one Boolean answer; repeated matches retain
 * the host route so their observable multiplicity is not collapsed. */
static __attribute__((noinline)) OemRun oem_member_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step, const OemTemplate *goal) {
    if (goal->kind != OEM_T_BUILD || goal->count != 3u)
        return OEM_RUN_DECLINED;
    Atom *probe = oem_instantiate(cursor, program, locals,
        program->template_children[goal->first + 1u]);
    Atom *list = oem_instantiate(cursor, program, locals,
        program->template_children[goal->first + 2u]);
    probe = probe ? oem_resolve_mode(cursor, probe, true) : NULL;
    list = list ? oem_resolve_mode(cursor, list, true) : NULL;
    if (!probe || !list)
        return OEM_RUN_HANDOFF;
    if (atom_has_vars(probe) || atom_has_vars(list))
        return OEM_RUN_DECLINED;
    if (petta_semantics_open_cons_built() &&
        atom_structural_may_have_list_carrier(list)) {
        list = petta_semantics_materialize_closed_logical_list(
            &cursor->region, list);
        if (!list)
            return OEM_RUN_DECLINED;
    }
    if (!oem_plain_list(list))
        return OEM_RUN_DECLINED;
    uint32_t matches = 0u;
    for (CettaExprIndex index = 0u; index < list->expr.len; index++) {
        /* Ground unification is structural equality and cannot write the
         * trail. Avoid constructing a rollback trial for each list item. */
        if (atom_eq(probe, list->expr.elems[index]) && ++matches > 1u)
            return OEM_RUN_DECLINED;
    }
    Atom *truth = oem_truth(cursor, matches == 1u);
    if (!truth)
        return OEM_RUN_HANDOFF;
    OemUnify unified = oem_meet(cursor, program, locals, step, truth);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    if (unified == OEM_UNIFY_OK)
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_MEMBER);
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* A native already executed. Export its actual return graph and continuation
 * together; the transport pair is never interpreted as authored syntax.
 * Delayed roots share the same fresh-variable and ownership export session. */
static OemRun oem_native_return_host(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step, CettaCallOutcome outcome) {
    if (step->store && !locals[program->templates[step->pattern].slot]) {
        Atom *cell = oem_new_cell(cursor);
        if (!cell || !oem_store_slot(cursor, locals,
                program->templates[step->pattern].slot, cell)) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            return OEM_RUN_FAULT;
        }
    }
    Atom *term = outcome.term ? outcome.term : atom_unit(&cursor->region);
    Atom *delayed = outcome.delayed
        ? outcome.delayed : atom_unit(&cursor->region);
    Atom *packet = term && delayed
        ? atom_expr2(&cursor->region, term, delayed) : NULL;
    Atom *destination = packet
        ? oem_instantiate(cursor, program, locals, step->pattern) : NULL;
    if (!destination) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
        return OEM_RUN_FAULT;
    }
    OemRun run = oem_host_goal(cursor, program, pc + 1u, locals, local_count,
        cont, depth, packet, destination, NULL,
        CETTA_OPEN_EQUATION_HOST_NATIVE_RETURN);
    if (run != OEM_RUN_HOST) {
        if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_NONE)
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
        return OEM_RUN_FAULT;
    }
    cursor->host_return = outcome;
    cursor->host_return.term = outcome.term
        ? cursor->host_goal->expr.elems[0] : NULL;
    cursor->host_return.delayed = outcome.delayed
        ? cursor->host_goal->expr.elems[1] : NULL;
    return run;
}

static bool oem_foreign_result_variable(const Atom *atom, void *context) {
    if (atom->kind != ATOM_VAR)
        return false;
    CettaOpenEquationCursor *cursor = context;
    uint32_t index = 0u;
    return !oem_is_cell(cursor, atom, &index) || index >= cursor->cell_len;
}

/* Native operations and lexical native tails return through one boundary.
 * Completed data may retain this cursor's authoritative cells. All remaining
 * completion belongs to the host over the actual packet and saved caller. */
static OemRun oem_entered_call_result(CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, uint32_t pc, Atom **locals,
    uint32_t local_count, const OemCont *cont, uint32_t depth,
    const OemStep *step, CettaCallOutcome outcome, Atom **value_out) {
    if (!cetta_call_outcome_shape_valid(outcome))
        return oem_service_fault(cursor);
    if (outcome.kind == CETTA_CALL_VALUE &&
        cetta_call_result_is_frontier(outcome.result_form))
        for (CettaExprIndex index = 0u; index < outcome.term->expr.len; index++)
            if (!outcome.term->expr.elems[index])
                return oem_service_fault(cursor);
    if (outcome.kind == CETTA_CALL_SUSPENDED || outcome.kind == CETTA_CALL_INTERRUPTED ||
        (outcome.kind == CETTA_CALL_VALUE &&
         (outcome.result_form != CETTA_CALL_RESULT_COMPLETED_VALUE ||
          (atom_has_vars(outcome.term) && atom_graph_any(outcome.term,
              oem_foreign_result_variable, cursor)) ||
          atom_has_thread_local_resource(outcome.term))))
        return oem_native_return_host(cursor, program, pc, locals, local_count,
                                      cont, depth, step, outcome);
    Atom *value = NULL;
    OemRun completed = oem_prepared_call_result(cursor, outcome, &value, value_out);
    if (completed != OEM_RUN_CALLED)
        return completed;
    bool truth = false;
    if (petta_semantics_truth_value(value, &truth))
        value = oem_truth(cursor, truth);
    if (!value)
        return oem_service_fault(cursor);
    OemUnify unified = oem_meet(cursor, program, locals, step, value);
    return unified == OEM_UNIFY_ERROR ? OEM_RUN_HANDOFF
        : unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
}

/* Resolve value operands in this branch, then loan their span directly to
 * the shared native entry. After entry, every result belongs to this call:
 * failure cannot select authored fallback, and a residual is not replayed. */
static OemRun oem_native_step(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step, Atom **value_out) {
    const OemTemplate *goal = &program->templates[step->value];
    if (!cursor->runtime.invoke_native || cursor->runtime.activation_budget ||
        cursor->runtime.depth_bound || goal->kind != OEM_T_BUILD ||
        goal->count == 0u || goal->count - 1u > OEM_PURE_INLINE_ARGS)
        return OEM_RUN_DECLINED;
    uint32_t arity = goal->count - 1u;
    Atom *head = oem_instantiate(cursor, program, locals,
        program->template_children[goal->first]);
    head = head ? oem_deref(cursor, head) : NULL;
    if (!head || head->kind != ATOM_SYMBOL ||
        !oem_native_operation(head->sym_id, arity))
        return OEM_RUN_DECLINED;
    Atom *arguments[OEM_PURE_INLINE_ARGS];
    for (uint32_t index = 0u; index < arity; index++) {
        Atom *value = oem_instantiate(cursor, program, locals,
            program->template_children[goal->first + index + 1u]);
        value = value ? oem_resolve_operand(cursor, value) : NULL;
        if (!value)
            return OEM_RUN_HANDOFF;
        if (atom_has_vars(value) || atom_has_registry_refs(value))
            return OEM_RUN_DECLINED;
        arguments[index] = oem_grounded_operand(cursor, value);
        if (!arguments[index])
            return OEM_RUN_DECLINED;
    }
    CettaInvocation invocation = {
        .head = binding_value_from_atom(head),
        .supplied = {.atoms = arguments, .count = arity},
    };
    CettaCallOutcome outcome;
    if (!cursor->runtime.invoke_native(cursor->runtime.context, program->space,
            &cursor->region, &invocation, &outcome))
        return OEM_RUN_DECLINED;
    cursor->stats.native_calls++;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OPEN_EQUATION_NATIVE_CALL);
    return oem_entered_call_result(cursor, program, pc, locals, local_count,
                                   cont, depth, step, outcome, value_out);
}

/* The region decides a HOST step its fast path covers. Values outside
 * each native domain retain the host's prepared leaf and dialect policy. */
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
    if (step->target == OEM_HOST_STATE)
        return oem_state_step(cursor, program, locals, step);
    const OemTemplate *goal = &program->templates[step->value];
    if (step->target == OEM_HOST_OBSERVE)
        return oem_observation_step(cursor, program, locals, step, goal);
    if (step->target == OEM_HOST_MEMBER)
        return oem_member_step(cursor, program, locals, step, goal);
    if (step->target == OEM_HOST_SEQUENCE_HEAD ||
        step->target == OEM_HOST_SEQUENCE_TAIL)
        return oem_sequence_projection_step(cursor, program, locals, step, goal);
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
            return OEM_RUN_DECLINED;
        Atom *list = oem_instantiate(
            cursor, program, locals,
            program->template_children[goal->first + 1u]);
        list = list ? oem_resolve_mode(cursor, list, true) : NULL;
        if (!list)
            return OEM_RUN_HANDOFF;
        if (atom_has_vars(list))
            return OEM_RUN_DECLINED;
        bool type_error = false;
        Atom *sorted = petta_semantics_sort_value(
            &cursor->region, list, step->op == g_builtin_syms.sort_atom,
            &type_error);
        if (!sorted)
            return type_error ? OEM_RUN_DECLINED : OEM_RUN_HANDOFF;
        OemUnify unified = oem_meet(cursor, program, locals, step, sorted);
        if (unified == OEM_UNIFY_ERROR)
            return OEM_RUN_HANDOFF;
        return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
    }
    if (goal->kind != OEM_T_BUILD || goal->count == 0u ||
        goal->count - 1u > OEM_PURE_INLINE_ARGS)
        return OEM_RUN_DECLINED;
    uint32_t count = goal->count - 1u;
    Atom *args[OEM_PURE_INLINE_ARGS];
    for (uint32_t index = 0u; index < count; index++) {
        Atom *arg = oem_instantiate(
            cursor, program, locals,
            program->template_children[goal->first + 1u + index]);
        Atom *size = NULL;
        if (count == 1u && oem_sequence_size_value(cursor, step->op, arg, &size)) {
            if (!size)
                return OEM_RUN_HANDOFF;
            OemUnify unified = oem_meet(cursor, program, locals, step, size);
            return unified == OEM_UNIFY_ERROR ? OEM_RUN_HANDOFF
                : unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
        }
        arg = arg ? oem_resolve_mode(cursor, arg, true) : NULL;
        if (!arg)
            return OEM_RUN_HANDOFF;
        if (atom_has_vars(arg) && !petta_semantics_structural_test(step->op))
            return OEM_RUN_DECLINED;
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

/* A private callable carrier is a value at this boundary. Its expression
 * shape becomes executable only when it heads an outer application. */
static OemRun oem_callable_value_step(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *program, Atom **locals,
        const OemStep *step) {
    const OemTemplate *term = step->value < program->template_len
        ? &program->templates[step->value] : NULL;
    if (!term ||
        (term->representation != PETTA_VALUE_REGISTERED_CALLABLE &&
         term->representation != PETTA_VALUE_COMPOUND))
        return OEM_RUN_DECLINED;
    Atom *value = oem_instantiate(cursor, program, locals, step->value);
    value = value ? oem_deref(cursor, value) : NULL;
    if (!value)
        return OEM_RUN_HANDOFF;
    if (!petta_semantics_runtime_callable_value(value))
        return OEM_RUN_DECLINED;
    OemUnify unified = oem_meet(cursor, program, locals, step, value);
    if (unified == OEM_UNIFY_ERROR)
        return OEM_RUN_HANDOFF;
    if (unified == OEM_UNIFY_OK)
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CALLABLE_VALUE);
    return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
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
    if (cursor->runtime.relation_admitted &&
        !cursor->runtime.relation_admitted(
            cursor->runtime.context, space, head, arity)) {
        return oem_enter_current_kind(cursor, space, head, arity, false, true);
    }
    for (uint32_t index = 0u; index < cursor->absent_len; index++) {
        const OemAbsentHead *absent = &cursor->absent[index];
        if (absent->head == head && absent->arity == arity &&
            absent->token.space == space &&
            absent->epoch == cursor->authority_epoch &&
            space_program_token_is_current(absent->token))
            return NULL;
    }
    const CettaOpenEquationProgram *program =
        oem_enter_current(cursor, space, head, arity, false);
    if (program)
        return program;
    /* An earlier verdict for the head is stale: neither its epoch nor its
     * program token comes back.  The new one replaces it, so the list keeps
     * one entry per head however often the authorities or the program
     * change. */
    OemAbsentHead verdict = {
        .head = head, .arity = arity, .epoch = cursor->authority_epoch,
        .token = space_program_token(space),
    };
    for (uint32_t index = 0u; index < cursor->absent_len; index++) {
        OemAbsentHead *absent = &cursor->absent[index];
        if (absent->head == head && absent->arity == arity &&
            absent->token.space == space) {
            *absent = verdict;
            return NULL;
        }
    }
    if (oem_reserve((void **)&cursor->absent, &cursor->absent_cap,
                    cursor->absent_len + 1u, sizeof(*cursor->absent)))
        cursor->absent[cursor->absent_len++] = verdict;
    return NULL;
}

/* Whether a template could build an expression of LEN elements headed by
 * one of HEADS.  False only when it certainly does not, whatever its locals
 * hold: a literal, or a built expression whose length or literal head
 * differs.  A slot or a list, which only the run decides, could. */
static bool oem_template_may_be(const CettaOpenEquationProgram *program,
                                uint32_t index, uint32_t len,
                                const SymbolId *heads, size_t head_count) {
    const OemTemplate *item = &program->templates[index];
    Atom *head = NULL;
    if (item->kind == OEM_T_LITERAL) {
        if (!item->literal || item->literal->kind != ATOM_EXPR ||
            item->literal->expr.len != len)
            return false;
        head = item->literal->expr.elems[0];
    } else if (item->kind == OEM_T_BUILD) {
        if (item->count != len)
            return false;
        const OemTemplate *first =
            &program->templates[program->template_children[item->first]];
        if (first->kind == OEM_T_SLOT)
            return true;
        /* A built or listed head is an expression, never a symbol. */
        if (first->kind != OEM_T_LITERAL)
            return false;
        head = first->literal;
    } else {
        return true;
    }
    for (size_t i = 0u; i < head_count; i++) {
        if (atom_is_symbol_id(head, heads[i]))
            return true;
    }
    return false;
}

/* Whether a template could build a call of a function the program's space
 * defines by one equation, which the host may unfold into its body. */
static bool oem_template_may_be_call(const CettaOpenEquationProgram *program,
                                     uint32_t index) {
    const OemTemplate *item = &program->templates[index];
    Atom *head = NULL;
    if (item->kind == OEM_T_LITERAL) {
        if (!item->literal || item->literal->kind != ATOM_EXPR ||
            item->literal->expr.len == 0u)
            return false;
        head = item->literal->expr.elems[0];
    } else if (item->kind == OEM_T_BUILD && item->count != 0u) {
        const OemTemplate *first =
            &program->templates[program->template_children[item->first]];
        if (first->kind != OEM_T_LITERAL)
            return false;
        head = first->literal;
    }
    if (!head || head->kind != ATOM_SYMBOL || !program->space ||
        !space_equations_may_match_known_head(program->space, head->sym_id))
        return false;
    SpaceEquationCursor cursor;
    SpaceEquationOccurrenceId id;
    size_t equations = 0u;
    if (!space_equation_cursor_init(program->space, head->sym_id, &cursor))
        return false;
    for (;;) {
        SpaceEquationCursorStep next = space_equation_cursor_next(&cursor, &id);
        if (next == SPACE_EQUATION_CURSOR_END)
            break;
        if (next != SPACE_EQUATION_CURSOR_ITEM || ++equations > 1u)
            return false;
    }
    return equations == 1u;
}

/* Whether a collection could be one the host answers natively, read from
 * its template before anything is built.  The host answers a body that is
 * a match, or a let over a superposed list whose body makes effects:
 * add-atom or remove-atom, or a let or let* binding them.  A collection
 * that certainly builds any other body never reaches it. */
static bool oem_native_collect_may_admit(
    const CettaOpenEquationProgram *program, uint32_t control_index) {
    const OemTemplate *control = &program->templates[control_index];
    if (control->kind != OEM_T_BUILD)
        return true;
    if (control->count != 2u)
        return false;
    uint32_t body_index = program->template_children[control->first + 1u];
    SymbolId bodies[2] = {g_builtin_syms.match, g_builtin_syms.let};
    if (!oem_template_may_be(program, body_index, 4u, bodies, 2u))
        return oem_template_may_be_call(program, body_index);
    const OemTemplate *body = &program->templates[body_index];
    if (body->kind != OEM_T_BUILD)
        return true;
    const OemTemplate *head =
        &program->templates[program->template_children[body->first]];
    if (head->kind != OEM_T_LITERAL ||
        !atom_is_symbol_id(head->literal, g_builtin_syms.let))
        return true;
    SymbolId lists[1] = {g_builtin_syms.superpose};
    SymbolId effects[2] = {g_builtin_syms.add_atom, g_builtin_syms.remove_atom};
    SymbolId lets[1] = {g_builtin_syms.let};
    SymbolId let_stars[1] = {g_builtin_syms.let_star};
    uint32_t list = program->template_children[body->first + 2u];
    uint32_t made = program->template_children[body->first + 3u];
    return oem_template_may_be(program, list, 2u, lists, 1u) &&
        (oem_template_may_be(program, made, 3u, effects, 2u) ||
         oem_template_may_be(program, made, 4u, lets, 1u) ||
         oem_template_may_be(program, made, 3u, let_stars, 1u));
}

/* The native collections a step asked its host for and the host declined,
 * per thread, by the step and its program: after two declines running the
 * step waits 1, 2, 4, ... up to 256 executions before asking again, and an
 * answer resets it.  Two steps meeting in one entry cost an ask, never an
 * answer: a declined collection is the step's own relation's. */
#define OEM_NATIVE_DECLINE_MEMO 64u
#define OEM_NATIVE_DECLINE_WAIT 256u

typedef struct {
    const OemStep *step;
    const CettaOpenEquationProgram *program;
    uint16_t declines;
    uint16_t wait;
    uint16_t waited;
} OemNativeDecline;

static _Thread_local OemNativeDecline
    oem_native_declines[OEM_NATIVE_DECLINE_MEMO];

static OemNativeDecline *oem_native_decline_entry(
    const CettaOpenEquationProgram *program, const OemStep *step) {
    uintptr_t key = ((uintptr_t)step >> 4) ^ ((uintptr_t)program >> 6);
    OemNativeDecline *entry =
        &oem_native_declines[key % OEM_NATIVE_DECLINE_MEMO];
    if (entry->step != step || entry->program != program)
        *entry = (OemNativeDecline){.step = step, .program = program};
    return entry;
}

/* A collection whose body is a match the host answers natively, in one
 * join loop: the collection expression, instantiated from the locals, goes
 * to the host, and the collection it answers meets the step's output.
 * DECLINED leaves the collection to its local relation, and a step the
 * host keeps declining waits before it asks again
 * (oem_native_decline_entry). */
static __attribute__((noinline)) OemRun oem_native_collect_step(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, const OemStep *step) {
    if (!cursor->runtime.native_collect ||
        cursor->runtime.activation_budget || cursor->runtime.depth_bound ||
        cursor->runtime.raises_he_errors || cursor->runtime.withdraws_on_error ||
        program->controls[step->control].limit != 0u ||
        step->relation >= program->host_plan_len ||
        !oem_native_collect_may_admit(program, step->value))
        return OEM_RUN_DECLINED;
    OemNativeDecline *declined = oem_native_decline_entry(program, step);
    if (declined->waited < declined->wait) {
        declined->waited++;
        return OEM_RUN_DECLINED;
    }
    Atom *control = oem_instantiate(cursor, program, locals, step->value);
    control = control ? oem_resolve_mode(cursor, control, true) : NULL;
    if (!control)
        return OEM_RUN_HANDOFF;
    if (control->kind != ATOM_EXPR || control->expr.len != 2u)
        return OEM_RUN_DECLINED;
    /* A match, or a let over a superposed list, which the host reads as a
     * match over the list's elements. */
    Atom *body = control->expr.elems[1];
    if (body->kind != ATOM_EXPR || body->expr.len == 0u ||
        body->expr.elems[0]->kind != ATOM_SYMBOL)
        return OEM_RUN_DECLINED;
    Atom *value = NULL;
    CettaNativeQueryStatus status = cursor->runtime.native_collect(
        cursor->runtime.context, program->space, &cursor->region, body,
        petta_plan_child(program->host_plans[step->relation], 1u), false,
        false, &value);
    /* The entry is read again: the host may have run regions whose steps
     * took it. */
    declined = oem_native_decline_entry(program, step);
    if (status == CETTA_NATIVE_QUERY_DECLINED) {
        declined->waited = 0u;
        if (declined->declines < 2u)
            declined->declines++;
        if (declined->declines == 2u)
            declined->wait = declined->wait == 0u ? 1u
                : declined->wait >= OEM_NATIVE_DECLINE_WAIT / 2u
                    ? OEM_NATIVE_DECLINE_WAIT : declined->wait * 2u;
        return OEM_RUN_DECLINED;
    }
    *declined = (OemNativeDecline){.step = step, .program = program};
    if (status != CETTA_NATIVE_QUERY_DONE || !value) {
        if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_NONE)
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
        return OEM_RUN_FAULT;
    }
    OemUnify unified = oem_meet(cursor, program, locals, step, value);
    return unified == OEM_UNIFY_ERROR ? OEM_RUN_HANDOFF
        : unified == OEM_UNIFY_OK ? OEM_RUN_CALLED : OEM_RUN_FAILED;
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
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
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
        return oem_push_frame(cursor, program, OEM_ONCE_FRAME, NULL, after,
                              depth) &&
                oem_push_frame(cursor, program, control->local, args, after,
                               depth + 1u)
            ? OEM_RUN_CALLED : OEM_RUN_HANDOFF;
    }
    OemCont *answer = arena_alloc(&cursor->region, sizeof(*answer));
    if (!answer)
        return OEM_RUN_HANDOFF;
    *answer = (OemCont){
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
        .local_count = local_count, .pc = control->resume,
        .pattern = control->yield, .depth = depth,
    };
    if (!cursor->collected_ready) {
        arena_init(&cursor->collected);
        arena_set_runtime_kind(&cursor->collected,
                               CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        arena_set_ledger_tag(&cursor->collected, CETTA_ARENA_LEDGER_CURSOR);
        arena_set_block_growth(&cursor->collected, OEM_FIRST_BLOCK_BYTES);
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
    if (copy && box->promoted) {
        Atom *components = atom_expr(&cursor->collected, NULL, 0u);
        Atom *joint[] = {copy, components};
        copy = components ? atom_expr(&cursor->collected, joint, 2u) : NULL;
    }
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
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
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
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
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
 * stable ground answer, borrowed, and every other host answer copied with its
 * variables mapped to the goal's cells, the call's, or fresh ones. */
typedef enum {
    OEM_IMPORT_CALL,
    OEM_IMPORT_ROW,
    OEM_IMPORT_COLLECTED,
    OEM_IMPORT_HOST,
    /* Intrinsic answers preserve incoming cells and freshen only the
     * service's newly quantified type variables. */
    OEM_IMPORT_TYPE,
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
/* An import shares `atom`: note the host storage the cursor now borrows
 * from.  An answer-arena atom may name stable atoms too; a stable atom names
 * only stable and longer-lived ones. */
static inline void oem_note_borrow(CettaOpenEquationCursor *cursor,
                                   const Atom *atom) {
    uint32_t identity = atom->arena_id;
    if (cursor->answer_arena && identity == cursor->answer_arena->identity) {
        cursor->borrows_answer_arena = true;
        cursor->borrows_stable = true;
    } else if (cursor->runtime.stable &&
               identity == cursor->runtime.stable->identity) {
        cursor->borrows_stable = true;
    }
}

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
    case OEM_IMPORT_TYPE:
        if (atom->kind == ATOM_VAR) {
            uint32_t cell;
            return oem_is_cell(cursor, atom, &cell)
                ? atom : oem_fresh_var(cursor, atom->var_id);
        }
        break;
    case OEM_IMPORT_COLLECTED:
        if (atom->arena_id != cursor->collected.identity) {
            oem_note_borrow(cursor, atom);
            return atom;
        }
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
        if (cursor->runtime.stable && !atom_has_vars(atom) &&
            atom->arena_id == cursor->runtime.stable->identity) {
            oem_note_borrow(cursor, atom);
            return atom;
        }
        if (atom->kind != ATOM_EXPR || !atom_has_vars(atom))
            return oem_import_copy(cursor, import, atom);
        *descend = true;
        return NULL;
    }
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom)) {
        oem_note_borrow(cursor, atom);
        return atom;
    }
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

static bool oem_record_read_names(CettaOpenEquationProgram *program, Atom *root) {
    if (!root)
        return false;
    Atom *inline_stack[32];
    Atom **stack = inline_stack;
    size_t len = 0u, capacity = 32u;
    OemAtomMap visited;
    oem_atom_map_init(&visited);
    bool ok = true;
    stack[len++] = root;
    while (len && ok) {
        Atom *atom = stack[--len];
        if (atom->kind == ATOM_SYMBOL) {
            ok = oem_reserve((void **)&program->read_heads,
                             &program->read_head_cap, program->read_head_len + 1u,
                             sizeof(*program->read_heads));
            if (ok)
                program->read_heads[program->read_head_len++] = atom->sym_id;
            continue;
        }
        if (atom->kind != ATOM_EXPR || oem_atom_map_find(&visited, atom))
            continue;
        if (!oem_atom_map_begin(&visited, atom) ||
            !cetta_expr_len_mul_fits_size(atom->expr.len, sizeof(*stack)) ||
            (size_t)atom->expr.len > SIZE_MAX - len) {
            ok = false;
            break;
        }
        size_t needed = len + (size_t)atom->expr.len;
        if (needed > capacity) {
            size_t grown_capacity = capacity;
            while (grown_capacity < needed && grown_capacity <= SIZE_MAX / 2u)
                grown_capacity *= 2u;
            if (grown_capacity < needed ||
                grown_capacity > SIZE_MAX / sizeof(*stack)) {
                ok = false;
                break;
            }
            Atom **grown = malloc(grown_capacity * sizeof(*grown));
            if (!grown) {
                ok = false;
                break;
            }
            memcpy(grown, stack, len * sizeof(*stack));
            if (stack != inline_stack)
                free(stack);
            stack = grown;
            capacity = grown_capacity;
        }
        for (CettaExprIndex child = atom->expr.len; child-- > 0u;)
            stack[len++] = atom->expr.elems[child];
    }
    if (stack != inline_stack)
        free(stack);
    oem_atom_map_free(&visited);
    return ok;
}

/* Check each represented node once. List tags are accepted only as the
 * head of a complete cons or proper typed list. A rest pattern and every
 * bare or embedded internal tag remain outside the region's interpretation. */
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
        CettaExprIndex first =
            petta_semantics_is_open_cons_value(atom) || atom_is_list(atom) ? 1u : 0u;
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
    /* HE completes the collection's producer: an error beside a success is
     * dropped, and errors alone are its answers. */
    bool errors = box->count == 0u && box->error_count > 0u;
    uint32_t count = errors ? box->error_count : box->count;
    /* The list is built in place: its answers go straight into its own
     * element vector. */
    Atom *list = atom_expr_builder_begin(&cursor->region, count);
    bool ok = list != NULL;
    uint32_t index = count;
    for (OemCollectItem *item = errors ? box->errors : box->last;
         ok && item; item = item->prev) {
        cursor->fresh_var_len = 0u;
        list->expr.elems[--index] = oem_import_collected(cursor, item->value);
        ok = list->expr.elems[index] != NULL;
    }
    list = ok ? atom_expr_builder_finish(&cursor->region, list) : NULL;
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
        return OEM_RUN_DECLINED;
    Atom *list = oem_instantiate(cursor, program, locals,
                                 program->template_children[goal->first + 1u]);
    if (!list)
        return OEM_RUN_HANDOFF;
    list = oem_deref(cursor, list);
    if (!oem_plain_list(list) || petta_semantics_is_open_cons_value(list))
        return OEM_RUN_DECLINED;
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
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .depth = depth,
    };
    return oem_push_frame(cursor, program, OEM_ELEMENTS_FRAME, args, resume,
                          depth)
        ? OEM_RUN_CALLED : OEM_RUN_HANDOFF;
}

static inline __attribute__((always_inline)) OemRun oem_enter(CettaOpenEquationCursor *cursor,
                        const CettaOpenEquationProgram *version,
                        uint32_t relation_index, Atom **args,
                        const OemCont *cont, uint32_t depth,
                        OemEntry *entry, Atom **value_out, const bool he);

/* ── Entry facts of trusted versions ─────────────────────────────────── */

/* No variable is reachable: a bound cell is followed, any other variable,
 * a cell of this cursor still unbound included, fails. */
static bool oem_value_ground(const CettaOpenEquationCursor *cursor,
                             Atom *value, uint32_t depth) {
    value = oem_deref(cursor, value);
    if (value->kind == ATOM_VAR || depth > OEM_MAX_DEPTH)
        return false;
    if (value->kind != ATOM_EXPR || !atom_has_vars(value))
        return true;
    for (CettaExprIndex i = 0u; i < value->expr.len; i++) {
        if (!oem_value_ground(cursor, value->expr.elems[i], depth + 1u))
            return false;
    }
    return true;
}

/* Every symbol reachable through bound cells is uninterpreted. */
static bool oem_value_uninterpreted(CettaOpenEquationCursor *cursor,
                                    Space *space, Atom *value,
                                    uint32_t depth) {
    value = oem_deref(cursor, value);
    if (depth > OEM_MAX_DEPTH)
        return false;
    switch (value->kind) {
    case ATOM_VAR:
    case ATOM_GROUNDED:
        return true;
    case ATOM_SYMBOL:
        return petta_contract_memo_symbol(cursor->contract_memo, space,
                                          value->sym_id);
    case ATOM_EXPR:
        for (CettaExprIndex i = 0u; i < value->expr.len; i++) {
            if (!oem_value_uninterpreted(cursor, space, value->expr.elems[i],
                                         depth + 1u))
                return false;
        }
        return true;
    default:
        return false;
    }
}

/* Whether a checked call of `relation`, its protocol complete, may enter
 * its trusted version with these arguments. */
static bool oem_entry_facts_hold(CettaOpenEquationCursor *cursor,
                                 const CettaOpenEquationProgram *program,
                                 uint32_t relation, Atom *const *args) {
    const OemRelation *entry = &program->relations[relation];
    if (!entry->has_trusted)
        return false;
    if (!cursor->contract_memo) {
        cursor->contract_memo = petta_contract_memo_new();
        if (!cursor->contract_memo)
            return false;
    }
    bool admitted = true;
    uint32_t i = 0u;
    for (; admitted && i < entry->version.arity; i++) {
        switch (entry->version.facts[i]) {
        case PETTA_CONTRACT_ENTRY_ANY:
            break;
        case PETTA_CONTRACT_ENTRY_GROUND:
            admitted = oem_value_ground(cursor, args[i], 0u);
            break;
        case PETTA_CONTRACT_ENTRY_UNINTERPRETED:
            admitted = oem_value_uninterpreted(cursor, program->space, args[i],
                                               0u);
            break;
        case PETTA_CONTRACT_ENTRY_CONSTANT:
            admitted = atom_is_symbol_id(oem_deref(cursor, args[i]),
                                         entry->version.constants[i]);
            break;
        case PETTA_CONTRACT_ENTRY_UNINTERPRETED_SPACE: {
            /* The space as the version's match will resolve it; a schema
             * or a name for no space sends that match to equation search. */
            Atom *value = oem_deref(cursor, args[i]);
            Space *data = cursor->runtime.resolve_space
                ? cursor->runtime.resolve_space(cursor->runtime.context,
                                                program->space,
                                                &cursor->region, value)
                : NULL;
            admitted = data && data != program->space &&
                !(value->kind == ATOM_SYMBOL &&
                  oem_space_has_schema(cursor, program->space, value)) &&
                petta_contract_memo_space(cursor->contract_memo,
                                          program->space, data);
            break;
        }
        }
    }
    /* Each space the version's equations match by name, as that match will
     * resolve it, has only uninterpreted rows now. */
    for (uint32_t k = 0u; admitted && k < entry->version.space_count; k++) {
        Atom *name = entry->version_spaces[k];
        Space *data = name && cursor->runtime.resolve_space
            ? cursor->runtime.resolve_space(cursor->runtime.context,
                                            program->space, &cursor->region,
                                            name)
            : NULL;
        admitted = data && data != program->space &&
            !oem_space_has_schema(cursor, program->space, name) &&
            petta_contract_memo_space(cursor->contract_memo, program->space,
                                      data);
        if (!admitted)
            i = entry->version.arity + 1u + k;
    }
    cetta_runtime_stats_inc(admitted
        ? CETTA_RUNTIME_COUNTER_PETTA_CONTRACT_VERSION_ENTRY
        : CETTA_RUNTIME_COUNTER_PETTA_CONTRACT_VERSION_REFUSED);
    /* CETTA_OPEN_EQUATIONS_DEBUG=host also names each refused version entry. */
    if (!admitted && oem_debug_host_goals())
        fprintf(stderr, "open-equations version-refused %s/%u: argument %u\n",
                symbol_bytes(g_symbols, entry->head), entry->arity, i - 1u);
    return admitted;
}

/* A pure complete guard has the same ordered choice protocol as elements.
 * The service already used the incoming requirement and decided whether
 * exact exhaustion permits metatype fallback; no fallback choice remains
 * for a later consumer failure to reopen.  A contract's check commits to
 * the first type its formal meets; a value with none goes to the host's
 * check, which raises, so the region raises no contract's error itself. */
static __attribute__((noinline)) OemRun oem_type_guard_step(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step) {
    if (!cursor->runtime.type_guard)
        return OEM_RUN_DECLINED;
    Atom *value = oem_instantiate(cursor, program, locals, step->value);
    Atom *formal = oem_instantiate(cursor, program, locals, step->pattern);
    value = value ? oem_resolve_mode(cursor, value, true) : NULL;
    Atom *required = formal ? oem_resolve_mode(cursor, formal, true) : NULL;
    if (!value || !required)
        return OEM_RUN_HANDOFF;
    Atom **types = NULL;
    uint32_t count = 0u;
    CettaOpenTypeResult result = cursor->runtime.type_guard(
        cursor->runtime.type_context, program->space, &cursor->region, value,
        required, step->type_dead != 0u, &types, &count);
    if (result == CETTA_OPEN_TYPE_DEFER) {
        free(types);
        return OEM_RUN_DECLINED;
    }
    if (result == CETTA_OPEN_TYPE_CAPACITY || result == CETTA_OPEN_TYPE_FAULT ||
        result == CETTA_OPEN_TYPE_STACK) {
        free(types);
        cursor->handoff = result == CETTA_OPEN_TYPE_CAPACITY
            ? CETTA_OPEN_EQUATION_HANDOFF_CAPACITY
            : result == CETTA_OPEN_TYPE_STACK
                ? CETTA_OPEN_EQUATION_HANDOFF_STACK
            : CETTA_OPEN_EQUATION_HANDOFF_SERVICE_ERROR;
        return OEM_RUN_FAULT;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OPEN_EQUATION_DIRECT_TYPE_GUARD);
    if (result == CETTA_OPEN_TYPE_PROVED) {
        free(types);
        return OEM_RUN_CALLED;
    }
    bool contract = cursor->runtime.call_contracts &&
        step->target == OEM_HOST_TYPE_ACCEPT;
    if (count == 0u) {
        free(types);
        return contract ? OEM_RUN_DECLINED : OEM_RUN_FAILED;
    }
    Atom *vector = atom_expr(&cursor->region, types, count);
    free(types);
    OemImport import;
    oem_import_init(&import, OEM_IMPORT_TYPE);
    cursor->fresh_var_len = 0u;
    Atom *list = vector ? oem_import_walk(cursor, &import, vector) : NULL;
    oem_import_free(&import);
    if (!list)
        return OEM_RUN_HANDOFF;
    if (contract) {
        for (uint32_t index = 0u; index < count; index++) {
            OemUnify unified =
                oem_unify_trial(cursor, formal, list->expr.elems[index]);
            if (unified != OEM_UNIFY_FAIL)
                return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED
                                               : OEM_RUN_HANDOFF;
        }
        return OEM_RUN_DECLINED;
    }
    if (count == 1u) {
        OemUnify unified = oem_unify(cursor, formal, list->expr.elems[0]);
        return unified == OEM_UNIFY_OK ? OEM_RUN_CALLED
            : unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED : OEM_RUN_HANDOFF;
    }
    Atom **args = arena_alloc(&cursor->region, sizeof(*args) * 2u);
    OemCont *resume = arena_alloc(&cursor->region, sizeof(*resume));
    if (!args || !resume)
        return OEM_RUN_HANDOFF;
    args[0] = formal;
    args[1] = list;
    *resume = (OemCont){
        .parent = cont, .receiver_parent = oem_receiver_cont(cont),
        .program = program, .locals = locals,
        .local_count = local_count, .pc = pc + 1u, .depth = depth,
    };
    return oem_push_frame(cursor, program, OEM_ELEMENTS_FRAME, args, resume, depth)
        ? OEM_RUN_ENTERED : OEM_RUN_HANDOFF;
}

/* Whether the cursor's loop has work due before it enters another frame:
 * the activation budget is spent, an interrupt poll is next, the region
 * has grown to its collection threshold, or the versions to their sweep.
 * A call then goes through a frame, and the loop does that work first. */
static inline bool oem_loop_due(const CettaOpenEquationCursor *cursor) {
    return (cursor->runtime.activation_budget &&
            cursor->stats.activations >=
                cursor->runtime.activation_budget) ||
        (cursor->runtime.interrupt && cursor->poll >= 255u) ||
        arena_accounted_live_bytes(&cursor->region) >= cursor->collect_after ||
        cursor->version_len >= cursor->version_sweep_at;
}

/* Alternatives are a call of their local relation (oem_local_step), and
 * enter it as the body's calls enter theirs (oem_enter): selection runs on
 * the shared variables in place, and a frame is pushed only when an
 * alternative after the selected one remains.  So a typed call whose
 * function has one signature, the call of most declared functions, makes
 * no frame.  On OEM_RUN_CALLED, `entry` is the activation to continue. */
static __attribute__((noinline)) OemRun oem_alternatives_enter(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, const OemStep *step, OemEntry *entry, Atom **value_out,
    const bool he) {
    uint32_t arg_count = step->arg_count;
    uint32_t arg_need = arg_count ? arg_count : 1u;
    if (arg_need > cursor->reg_cap &&
        !oem_reserve((void **)&cursor->regs, &cursor->reg_cap, arg_need,
                     sizeof(*cursor->regs)))
        return OEM_RUN_HANDOFF;
    Atom **args = cursor->regs;
    for (uint32_t index = 0u; index < arg_count; index++) {
        args[index] = oem_instantiate(
            cursor, program, locals,
            program->step_args[step->first_arg + index]);
        if (!args[index])
            return OEM_RUN_HANDOFF;
    }
    /* In a tail position the alternatives' answers meet the caller's own
     * continuation, as in oem_local_step. */
    const OemCont *next = cont;
    if (!step->tail) {
        OemCont *after = oem_region_alloc(cursor, sizeof(*after));
        if (!after)
            return OEM_RUN_HANDOFF;
        *after = (OemCont){
            .parent = cont, .receiver_parent = oem_receiver_cont(cont),
            .program = program, .locals = locals,
            .local_count = local_count, .pc = pc + 1u,
            .pattern = step->pattern, .depth = depth,
        };
        next = after;
    }
    if (cursor->runtime.interrupt)
        cursor->poll++;
    return oem_enter(cursor, program, program->controls[step->control].local,
                     args, next, depth + 1u, entry, value_out, he);
}

/* A changed authority runs its original source occurrence before any
 * argument producer. Keep this infrequent host exit outside the body loop. */
static bool oem_fill_source_slots(CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, Atom **locals, uint32_t index) {
    const OemTemplate *item = &program->templates[index];
    if (item->kind == OEM_T_SLOT) {
        if (locals[item->slot])
            return true;
        Atom *cell = oem_new_cell(cursor);
        return cell && oem_store_slot(cursor, locals, item->slot, cell);
    }
    if (item->kind == OEM_T_BUILD || oem_template_is_list(item)) {
        for (uint32_t child = 0u; child < item->count; child++) {
            if (!oem_fill_source_slots(cursor, program, locals,
                    program->template_children[item->first + child]))
                return false;
        }
    }
    return true;
}

static __attribute__((cold, noinline)) OemRun oem_changed_entry_host(
    CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, Atom **locals,
    uint32_t local_count, const OemCont *cont, uint32_t depth,
    const OemStep *step) {
    if (!oem_fill_source_slots(cursor, program, locals, step->value))
        return OEM_RUN_HANDOFF;
    Atom *goal = oem_instantiate(cursor, program, locals, step->value);
    const OemStep *tail = oem_entry_tail(program, step);
    if (tail)
        return oem_host_tail_goal(cursor, cont, goal,
            program->host_plans[step->relation],
            CETTA_OPEN_EQUATION_HOST_SOLVE, tail->recovers);
    if (step->store && !locals[program->templates[step->pattern].slot]) {
        Atom *cell = oem_new_cell(cursor);
        if (!cell || !oem_store_slot(cursor, locals,
                program->templates[step->pattern].slot, cell))
            return OEM_RUN_HANDOFF;
    }
    Atom *destination = oem_instantiate(
        cursor, program, locals, step->pattern);
    return oem_host_goal(cursor, program, step->target, locals,
        local_count, cont, depth, goal, destination,
        program->host_plans[step->relation],
        CETTA_OPEN_EQUATION_HOST_SOLVE);
}

static inline __attribute__((always_inline)) OemRun oem_run_body_mode(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, Atom **value_out, const bool he) {
    for (;;) {
        if (pc >= program->step_len)
            return OEM_RUN_HANDOFF;
        const OemStep *step = &program->steps[pc];
        bool call_current;
        switch ((OemStepKind)step->kind) {
        case OEM_S_BUILTIN_PRIM:
        case OEM_S_BUILTIN_TEST:
            if (!oem_version_current(cursor, program))
                return oem_changed_entry_host(cursor, program, locals,
                    local_count, cont, depth, step);
            pc += step->ready;
            step = &program->steps[pc];
            goto scalar_step;
        case OEM_S_BUILTIN_FAIL:
            return oem_version_current(cursor, program) ? OEM_RUN_FAILED
                : oem_changed_entry_host(cursor, program, locals,
                    local_count, cont, depth, step);
        case OEM_S_BUILTIN_HOST:
            if (!oem_version_current(cursor, program))
                return oem_changed_entry_host(cursor, program, locals,
                    local_count, cont, depth, step);
            pc += step->ready;
            step = &program->steps[pc];
            goto host_step;
        case OEM_S_BUILTIN_BIND:
            if (!oem_version_current(cursor, program))
                return oem_changed_entry_host(cursor, program, locals,
                    local_count, cont, depth, step);
            pc += step->ready;
            step = &program->steps[pc];
            goto bind_step;
        case OEM_S_BUILTIN_CALL:
            if (!oem_version_current(cursor, program))
                return oem_changed_entry_host(cursor, program, locals,
                    local_count, cont, depth, step);
            pc += step->ready;
            step = &program->steps[pc];
            call_current = true;
            goto call_step;
        case OEM_S_BUILTIN_ENTRY:
            if (oem_version_current(cursor, program)) {
                pc += step->ready;
                continue;
            } else {
                return oem_changed_entry_host(cursor, program, locals,
                    local_count, cont, depth, step);
            }
        case OEM_S_ENTRY:
            /* A declaration can change the argument demands. Validate
             * before any producer runs, not when the old CALL is reached.
             * Selecting the unchanged untyped protocol also licenses its
             * ready body after a mutation performed by an argument. */
            if (oem_version_current(cursor, program) ||
                (!step->type_dead &&
                 (!cursor->runtime.relation_admitted ||
                  cursor->runtime.relation_admitted(cursor->runtime.context,
                      program->space, step->op, step->arg_count)))) {
                pc++;
                continue;
            }
            return oem_changed_entry_host(cursor, program, locals,
                local_count, cont, depth, step);
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
        case OEM_S_TAIL:
            /* Reifying these templates only builds data. The entry's
             * version check also licenses this call when no producer lies
             * between them; an argument goal ends that license. */
            call_current = oem_version_current(cursor, program);
        call_step: {
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
            if (step->checked_body == 2u)
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_OPEN_EQUATION_IDENTITY_TYPED_CALL);
            const OemCont *next = cont;
            if (step->kind == OEM_S_CALL) {
                OemCont *record = oem_region_alloc(cursor, sizeof(*record));
                if (!record)
                    return OEM_RUN_HANDOFF;
                *record = (OemCont){
                    .parent = cont, .receiver_parent = oem_receiver_cont(cont),
                    .program = program, .locals = locals,
                    .local_count = local_count, .pc = pc + 1u,
                    .pattern = step->pattern, .depth = depth,
                    .recovers = step->recovers, .store = step->store != 0u,
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
            /* A checked call whose protocol completed enters the trusted
             * version when its arguments meet the version's entry facts. */
            if (call_current && step->checked_body &&
                program->relations[relation].has_trusted &&
                oem_entry_facts_hold(cursor, program, relation, args))
                relation = program->relations[relation].trusted;
            if (!call_current && !program->relations[relation].local) {
                const OemRelation *callee = &program->relations[relation];
                if (!step->checked_body && cursor->runtime.relation_admitted &&
                    !cursor->runtime.relation_admitted(cursor->runtime.context,
                        program->space, callee->head, callee->arity)) {
                    return oem_live_call_host(cursor, program, pc, locals,
                        local_count, cont, depth, step, args);
                }
                target = oem_enter_current(cursor, program->space,
                    callee->head, callee->arity, callee->body_is_data);
                if (!target) {
                    return oem_live_call_host(cursor, program, pc, locals,
                        local_count, cont, depth, step, args);
                }
                relation = 0u;
            }
            /* The call selects its equation and enters it here, unless the
             * loop has work due first; then a frame holds the call. */
            if (!oem_loop_due(cursor)) {
                if (cursor->runtime.interrupt)
                    cursor->poll++;
                OemEntry callee = {0};
                OemRun run = oem_enter(cursor, target, relation, args, next,
                                       depth + 1u, &callee, value_out, he);
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
        case OEM_S_TEST:
        scalar_step: {
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
                                                 left, right, &value, &truth,
                                                 value_out, he);
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
            OemRun run = he
                ? oem_match_step_he(cursor, program, pc, locals, local_count,
                                    cont, depth, step)
                : oem_match_step_plain(cursor, program, pc, locals,
                                       local_count, cont, depth, step);
            if (run != OEM_RUN_FOLDED)
                return run;
            pc++;
            continue;
        }
        case OEM_S_HOST:
        host_step:
            if (step->target == OEM_HOST_TYPE_ACCEPT ||
                step->target == OEM_HOST_TYPE_MATCH) {
                OemRun run = oem_type_guard_step(cursor, program, pc, locals,
                    local_count, cont, depth, step);
                if (run == OEM_RUN_CALLED) {
                    pc++;
                    continue;
                }
                if (run == OEM_RUN_ENTERED)
                    return OEM_RUN_CALLED;
                if (run != OEM_RUN_DECLINED)
                    return run;
            }
            if ((step->target == OEM_HOST_COLLECT ||
                 step->target == OEM_HOST_ALTERNATIVES ||
                 step->target == OEM_HOST_ONCE) &&
                !program->relations[
                    program->controls[step->control].local].failed) {
                const OemRelation *local = &program->relations[
                    program->controls[step->control].local];
                const OemLocalSource *source =
                    &program->sources[local->first_source];
                bool current = !source->typed ||
                    oem_version_current(cursor, program);
                if (current) {
                    if (source->typed)
                        cetta_runtime_stats_inc(
                            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_TYPED_CALL);
                    if (step->target == OEM_HOST_ALTERNATIVES &&
                        !oem_loop_due(cursor)) {
                        OemEntry callee = {0};
                        OemRun run = oem_alternatives_enter(
                            cursor, program, pc, locals, local_count, cont,
                            depth, step, &callee, value_out, he);
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
                    if (step->target == OEM_HOST_COLLECT) {
                        OemRun native = oem_native_collect_step(
                            cursor, program, locals, step);
                        if (native == OEM_RUN_CALLED) {
                            pc++;
                            continue;
                        }
                        if (native != OEM_RUN_DECLINED)
                            return oem_raised(cursor, cont, native);
                    }
                    return oem_local_step(cursor, program, pc, locals,
                                          local_count, cont, depth, step);
                }
            }
            if (step->target == OEM_HOST_ELEMENTS) {
                OemRun run = oem_elements_step(cursor, program, pc, locals,
                                               local_count, cont, depth,
                                               step);
                if (run != OEM_RUN_DECLINED)
                    return run;
            }
            if (step->target == OEM_HOST_SEALED)
                return oem_sealed_step(cursor, program, pc, locals,
                                       local_count, cont, depth, step);
            if (step->target >= OEM_HOST_PURE) {
                OemRun run = step->target == OEM_HOST_NATIVE
                    ? oem_native_step(cursor, program, pc, locals, local_count,
                                       cont, depth, step, value_out)
                    : step->target == OEM_HOST_APPLY ||
                            step->target == OEM_HOST_DATA_HEAD
                        ? oem_apply_step(cursor, program, pc, locals,
                                         local_count, cont, depth, step, value_out)
                        : oem_fast_host_step(cursor, program, locals, step,
                                             value_out);
                if (run == OEM_RUN_CALLED) {
                    pc++;
                    continue;
                }
                if (run == OEM_RUN_ENTERED)
                    return OEM_RUN_CALLED;
                bool declined = run == OEM_RUN_DECLINED;
                if (!declined) {
                    OemRun completed = oem_raised(cursor, cont, run);
                    if (step->target == OEM_HOST_NATIVE &&
                        completed == OEM_RUN_HANDOFF) {
                        if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_NONE)
                            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
                        return OEM_RUN_FAULT;
                    }
                    return completed;
                }
                /* The host fills a slot this step would have stored: it
                 * takes a cell here, at its first occurrence. */
                if (step->store &&
                    !locals[program->templates[step->pattern].slot]) {
                    Atom *cell = oem_new_cell(cursor);
                    if (!cell ||
                        !oem_store_slot(cursor, locals,
                                        program->templates[step->pattern].slot,
                                        cell))
                        return OEM_RUN_HANDOFF;
                }
            }
            {
                OemRun run = oem_callable_value_step(
                    cursor, program, locals, step);
                if (run == OEM_RUN_CALLED) {
                    pc++;
                    continue;
                }
                if (run != OEM_RUN_DECLINED)
                    return oem_raised(cursor, cont, run);
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
            Atom *key = oem_instantiate(cursor, program, locals, step->value);
            if (!key)
                return OEM_RUN_HANDOFF;
            /* The arms are selected by their first tests, as a call's
             * equations are.  An arm the key refutes cannot unify: the case
             * goes on to the next arm with nothing bound, as a failed
             * attempt would. */
            Atom *view = oem_deref(cursor, key);
            if ((step->arm & OEM_ARM_TESTS) &&
                oem_arm_refutes(program, step->pattern, view) &&
                oem_closed_root(view)) {
                pc = step->target;
                continue;
            }
            if (!atom_has_vars(view)) {
                OemTrial ground_trial;
                oem_trial_begin(cursor, &ground_trial);
                OemGroundMatch matched = oem_match_ground_template(
                    cursor, program, locals, step->pattern, view);
                oem_trial_end(cursor, &ground_trial,
                              matched == OEM_GROUND_MATCH_OK
                                  ? OEM_UNIFY_OK : OEM_UNIFY_FAIL);
                if (matched != OEM_GROUND_MATCH_DECLINE) {
                    if (matched == OEM_GROUND_MATCH_ERROR)
                        return OEM_RUN_HANDOFF;
                    cetta_runtime_stats_inc(
                        CETTA_RUNTIME_COUNTER_OPEN_EQUATION_GROUND_CASE_MATCH);
                    pc = matched == OEM_GROUND_MATCH_OK ? pc + 1u
                                                        : step->target;
                    continue;
                }
            }
            /* An arm whose later arms the key all refutes is the case's
             * last: a mismatch fails the case, and the backtracking that
             * follows undoes what the attempt bound, so it is no trial. */
            if (oem_later_arms_refuted(program, step, view)) {
                OemUnify unified = oem_unify_template(cursor, program, locals,
                                                      step->pattern, key, 0u);
                if (unified == OEM_UNIFY_ERROR)
                    return OEM_RUN_HANDOFF;
                if (unified == OEM_UNIFY_FAIL)
                    return OEM_RUN_FAILED;
                pc++;
                continue;
            }
            /* An arm a later one may still take over is a trial, undone by
             * marks when the key does not unify. */
            OemTrial trial;
            oem_trial_begin(cursor, &trial);
            OemUnify unified = oem_unify_template(cursor, program, locals,
                                                  step->pattern, key, 0u);
            oem_trial_end(cursor, &trial, unified);
            if (unified == OEM_UNIFY_ERROR)
                return OEM_RUN_HANDOFF;
            pc = unified == OEM_UNIFY_OK ? pc + 1u : step->target;
            continue;
        }
        case OEM_S_BIND:
        bind_step: {
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

/* The body runner for a cursor under HE's rules, and for any other. */
static __attribute__((noinline)) OemRun oem_run_body_plain(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, Atom **value_out) {
    return oem_run_body_mode(cursor, program, pc, locals, local_count, cont,
                             depth, value_out, false);
}

static __attribute__((noinline)) OemRun oem_run_body_he(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    uint32_t pc, Atom **locals, uint32_t local_count, const OemCont *cont,
    uint32_t depth, Atom **value_out) {
    return oem_run_body_mode(cursor, program, pc, locals, local_count, cont,
                             depth, value_out, true);
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
            if (!open && oem_refutes_literal(term, op->literal))
                return false;
            continue;
        }
        if (op->kind == OEM_M_LIST) {
            /* A nonempty list; its rest is left open, and so is the whole
             * of a cell the operation reads as spelled. */
            Atom *const *elements = NULL;
            CettaExprLen length = 0u;
            if (!open && (!atom_sequence_view(term, &elements, &length) || length == 0u))
                return false;
            regs[op->operand] =
                open || (op->spelled &&
                         petta_semantics_is_cons_constraint(term))
                    ? NULL : elements[0];
            regs[op->operand + 1u] = NULL;
            continue;
        }
        if (!open && oem_refutes_expression(
                         term, op->length,
                         oem_value_representation(op->literal)))
            return false;
        for (uint32_t child = 0u; child < op->length; child++)
            regs[op->operand + child] = open ? NULL : term->expr.elems[child];
    }
    if (eq->has_entry_guard && !cursor->runtime.unmatched_call_is_value &&
        regs[eq->entry_guard_reg]) {
        Atom *term = oem_deref(cursor, regs[eq->entry_guard_reg]);
        uint32_t cell = 0u;
        if (!oem_is_cell(cursor, term, &cell) && !oem_is_list_cell(term) &&
            (term->kind != ATOM_EXPR || term->expr.len == 0u) &&
            oem_version_current(cursor, program)) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_OPEN_EQUATION_ENTRY_GUARD_REFUTED);
            return false;
        }
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
                       oem_decomposition_compatible(term, op->literal) &&
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
            } else {
                Atom *const *elements = NULL;
                CettaExprLen length = 0u;
                if (!atom_sequence_view(term, &elements, &length) || length == 0u) {
                    cursor->stats.head_failures++;
                    return oem_count_attempt(cursor, OEM_ATTEMPT_MISMATCH,
                                             binds_at_start, OEM_RUN_FAILED);
                }
                /* A flat list's rest is its suffix, sharing its storage. */
                Atom *rest = atom_is_list(term)
                    ? atom_sequence_like(&cursor->region, term, elements + 1u, length - 1u)
                    : atom_expr_suffix(&cursor->region, term, 1u);
                if (!rest)
                    return OEM_RUN_HANDOFF;
                regs[op->operand] = elements[0];
                regs[op->operand + 1u] = rest;
            }
            break;
        }
        }
    }
    /* The head has taken the arguments; what follows meets the caller's
     * destination, which only prunes this equation's output. */
    cursor->params_matched = true;
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
        arena_accounted_live_bytes(stable) != 0u) {
        arena_set_older_generation(&cursor->region, stable);
        /* Copies into the region may now share stable atoms as settled. */
        cursor->borrows_stable = true;
    }
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
    /* The stress sizes are in force (a test). */
    bool stress;
} OemCollectSizes;

static const OemCollectSizes *oem_collect_sizes(void) {
    static OemCollectSizes sizes;
    static bool ready;
    if (!ready) {
        const char *stress = getenv("CETTA_OEM_COLLECT_STRESS");
        sizes = stress && stress[0] && strcmp(stress, "0") != 0
            ? (OemCollectSizes){
                  .first = 16u * 1024u, .steady = 16u * 1024u,
                  .major = 64u * 1024u, .stress = true}
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
    /* Atoms of the storage `detach_identity` names, and export copies there,
     * that this collection found the cursor holding. */
    uint64_t detach_copied;
    /* Under the collection stress test, the storage identities of the
     * programs the cursor keeps: no term may hold an atom of one, since a
     * program's storage goes with the program (oem_shared_leaf). */
    uint32_t *program_storage;
    uint32_t program_storage_len;
} OemCollect;

/* A table size, a power of two of at least 16, that holds `hint`
 * entries at the table's load bound. */
static size_t oem_collect_table_size(size_t hint) {
    size_t cap = 16u;
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

/* Whether an export's stable copy, which serves as an expression's copy,
 * outlives this collection.  A detaching collection runs because the host is
 * about to release `detach_identity`: a copy there would forward the cursor
 * into storage about to be freed, so the expression is traced and copied
 * like any other. */
static inline bool oem_export_copy_outlives(const OemCollect *collect,
                                            const Atom *copy) {
    return collect->detach_identity == 0u ||
        copy->arena_id != collect->detach_identity;
}

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
                if (atom->name_key && atom->name_key != &oem_exported_mark) {
                    if (atom->name_key == &oem_traced_mark ||
                        oem_export_copy_outlives(collect, atom->name_key))
                        continue;
                    /* An export's copy in the storage being released. */
                    collect->detach_copied++;
                }
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
                bytes += atom_string_len(atom) + 1u;
            if (atom->arena_id == collect->from_identity)
                collect->reached_young += bytes;
            else
                collect->reached_old += bytes;
            continue;
        }
        if (atom->name_key && atom->name_key != &oem_exported_mark) {
            if (atom->name_key == &oem_traced_mark ||
                oem_export_copy_outlives(collect, atom->name_key))
                continue;
            /* An export's copy in the storage being released. */
            collect->detach_copied++;
        }
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

/* Under the collection stress test, the storage identities of the programs
 * the cursor keeps, which the trace checks no term reaches. */
static void oem_collect_note_program_storage(
    const CettaOpenEquationCursor *cursor, OemCollect *collect) {
    collect->program_storage =
        calloc((size_t)cursor->version_len + 1u,
               sizeof(*collect->program_storage));
    if (!collect->program_storage)
        return;
    if (cursor->program && cursor->program->atoms_ready)
        collect->program_storage[collect->program_storage_len++] =
            cursor->program->atoms.identity;
    for (uint32_t index = 0u; index < cursor->version_len; index++) {
        if (cursor->versions[index]->atoms_ready)
            collect->program_storage[collect->program_storage_len++] =
                cursor->versions[index]->atoms.identity;
    }
}

static inline void oem_collect_check_program_storage(
    const OemCollect *collect, const Atom *atom) {
    for (uint32_t index = 0u; index < collect->program_storage_len; index++) {
        if (atom->arena_id == collect->program_storage[index]) {
            fprintf(stderr, "fatal: an open-equation term holds an atom of "
                            "a compiled program's storage\n");
            abort();
        }
    }
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
            collect->detach_copied++;
        } else if (!oem_collect_from(collect, atom)) {
            oem_collect_check_program_storage(collect, atom);
            result = atom;
        } else if (((atom->kind == ATOM_EXPR && atom->expr.len > 0u) ||
                    oem_immutable_string(atom)) &&
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
        copied->receiver_parent = oem_receiver_cont(parent);
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
    free(collect->program_storage);
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
    /* Opaque callee roots cannot follow the parent's copying collector.
     * Transfer every live child before either generation or host storage
     * moves. Failed transfer keeps the actual child and its residual. */
    for (uint32_t index = 0u; index < cursor->frame_len && cursor->external_frames; index++) {
        OemFrame *frame = &cursor->frames[index];
        if (frame->relation == OEM_PREPARED_FRAME &&
            (!cetta_prepared_pure_answer_cursor_detach(frame->prepared_answers) ||
             cetta_prepared_pure_answer_cursor_uses_arena(frame->prepared_answers, &cursor->region)))
            return false;
    }
    oem_forget_ground_facts(cursor);
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
    if (oem_collect_sizes()->stress)
        oem_collect_note_program_storage(cursor, &collect);
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
         * holds their block; lazy rows hold none. */
        if (ok && oem_frame_is_match(frame) && !frame->lazy_rows) {
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
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_OPEN_EQUATION_REGION_AT_COLLECTION_BYTES_PEAK,
        arena_accounted_live_bytes(&cursor->region));
    if (ok && major && collect.record &&
        collect.reached_old >= old_live - old_live / 4u) {
        oem_collect_decline(&collect, before, kept_before, args, rows, hosts,
                            conts, cells);
        uint32_t shift = cursor->major_declines < OEM_MAJOR_DECLINE_SHIFT_MAX
            ? cursor->major_declines + 1u : OEM_MAJOR_DECLINE_SHIFT_MAX;
        cursor->major_after = old_live > SIZE_MAX >> shift
            ? SIZE_MAX : old_live << shift;
        cursor->major_declines++;
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_MAJOR_COLLECTIONS_DECLINED);
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
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_MINOR_COLLECTIONS_DECLINED);
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
        arena_set_ledger_tag(&fresh, CETTA_ARENA_LEDGER_CURSOR);
        arena_set_block_growth(&fresh, OEM_FIRST_BLOCK_BYTES);
        arena_set_hashcons(&fresh, NULL);
        /* The terms a collection moves keep symbols of their own: every
         * later step reads them, and the table's shared atoms there cost
         * those steps more than they save. */
        arena_set_own_symbols(&fresh, true);
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
         * only out of a generation this collection collects.  Lazy rows
         * stay where they are. */
        if (ok && oem_frame_is_match(frame) && !frame->lazy_rows) {
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
    free(collect.program_storage);
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
        if (oem_frame_is_match(frame) && !frame->lazy_rows)
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
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_PROMOTED_BYTES, promoted);
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
        cetta_runtime_stats_update_max(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_MAJOR_SURVIVOR_BYTES_PEAK,
            survivors);
        size_t major_min = oem_collect_sizes()->major;
        cursor->major_after = survivors > major_min / 2u
            ? 2u * survivors : major_min;
        cursor->major_declines = 0u;
    }
    cursor->stats.collections++;
    cetta_runtime_stats_inc(major
        ? CETTA_RUNTIME_COUNTER_OPEN_EQUATION_MAJOR_COLLECTIONS
        : CETTA_RUNTIME_COUNTER_OPEN_EQUATION_MINOR_COLLECTIONS);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_OPEN_EQUATION_OLD_BYTES_PEAK,
        cursor->old_ready ? arena_accounted_live_bytes(&cursor->old) : 0u);
    cursor->detach_copied += collect.detach_copied;
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
        if (op->kind == OEM_M_ATOM)
            return oem_refutes_literal(term, op->literal);
        if (op->kind == OEM_M_LIST)
            return term->kind != ATOM_EXPR || term->expr.len == 0u;
        return oem_refutes_expression(term, op->length,
                                      oem_value_representation(op->literal));
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

typedef enum {
    OEM_ALGEBRAIC_NOT_APPLICABLE = 0,
    OEM_ALGEBRAIC_FAILED,
    OEM_ALGEBRAIC_READY,
    OEM_ALGEBRAIC_HANDOFF,
} OemAlgebraicRun;

/* Commit the interrupt polls of an admitted loop.  Speculative recognition
 * never advances the live poll counter: an open value that falls back to
 * relational execution must see exactly the incoming scheduler state. */
static bool oem_algebraic_poll(
        CettaOpenEquationCursor *cursor, uint64_t steps) {
    if (!cursor->runtime.interrupt)
        return true;
    uint32_t poll = cursor->poll;
    while (steps > 0u) {
        uint32_t room = 256u - poll;
        if (steps < room) {
            cursor->poll = poll + (uint32_t)steps;
            return true;
        }
        steps -= room;
        poll = 0u;
        if (cursor->runtime.interrupt(cursor->runtime.context)) {
            cursor->poll = 0u;
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT;
            return false;
        }
    }
    cursor->poll = poll;
    return true;
}

/* Run a source-certified algebraic length as the least-work loop for its
 * closed constructor chain.  Open inputs retain relational execution. */
static OemAlgebraicRun oem_algebraic_length(
        CettaOpenEquationCursor *cursor, const OemRelation *relation,
        Atom *input, Atom **value_out) {
    uint64_t count = 0u;
    Atom *value = oem_deref(cursor, input);
    for (;;) {
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, value, &cell) || value->kind == ATOM_VAR)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        if (atom_is_symbol_id(value, relation->algebraic_nil))
            break;
        if (value->kind != ATOM_EXPR || value->expr.len != 3u ||
            !atom_is_symbol_id(value->expr.elems[0],
                               relation->algebraic_cons))
            return oem_algebraic_poll(cursor, count)
                ? OEM_ALGEBRAIC_FAILED : OEM_ALGEBRAIC_HANDOFF;
        if (count == (uint64_t)INT64_MAX)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        count++;
        value = oem_deref(cursor, value->expr.elems[2]);
    }
    if (!oem_algebraic_poll(cursor, count))
        return OEM_ALGEBRAIC_HANDOFF;
    *value_out = atom_int(&cursor->region, (int64_t)count);
    return *value_out ? OEM_ALGEBRAIC_READY : OEM_ALGEBRAIC_HANDOFF;
}

static OemAlgebraicRun oem_algebraic_append(
        CettaOpenEquationCursor *cursor, const OemRelation *relation,
        Atom *left, Atom *right, Atom **value_out) {
    uint32_t length = 0u;
    Atom *value = oem_deref(cursor, left);
    for (;;) {
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, value, &cell) || value->kind == ATOM_VAR)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        if (atom_is_symbol_id(value, relation->algebraic_nil))
            break;
        if (value->kind != ATOM_EXPR || value->expr.len != 3u ||
            !atom_is_symbol_id(value->expr.elems[0],
                               relation->algebraic_cons))
            return oem_algebraic_poll(cursor, length)
                ? OEM_ALGEBRAIC_FAILED : OEM_ALGEBRAIC_HANDOFF;
        if (length == UINT32_MAX)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        if (!oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         length + 1u, sizeof(*cursor->walk)))
            return OEM_ALGEBRAIC_HANDOFF;
        cursor->walk[length++] = value->expr.elems[1];
        value = oem_deref(cursor, value->expr.elems[2]);
    }
    if (!oem_algebraic_poll(cursor, length))
        return OEM_ALGEBRAIC_HANDOFF;
    Atom *cons = atom_symbol_id(&cursor->region, relation->algebraic_cons);
    value = oem_deref(cursor, right);
    if (!cons || !value)
        return OEM_ALGEBRAIC_HANDOFF;
    while (length > 0u) {
        value = atom_expr3(
            &cursor->region, cons, cursor->walk[--length], value);
        if (!value)
            return OEM_ALGEBRAIC_HANDOFF;
    }
    *value_out = value;
    return OEM_ALGEBRAIC_READY;
}

static OemAlgebraicRun oem_algebraic_project_map(
        CettaOpenEquationCursor *cursor, const OemRelation *relation,
        Atom *input, Atom **value_out) {
    uint32_t length = 0u;
    Atom *value = oem_deref(cursor, input);
    for (;;) {
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, value, &cell) || value->kind == ATOM_VAR)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        if (atom_is_symbol_id(value, relation->algebraic_nil))
            break;
        if (value->kind != ATOM_EXPR || value->expr.len != 3u ||
            !atom_is_symbol_id(value->expr.elems[0],
                               relation->algebraic_cons))
            return oem_algebraic_poll(cursor, length)
                ? OEM_ALGEBRAIC_FAILED : OEM_ALGEBRAIC_HANDOFF;
        Atom *item = oem_deref(cursor, value->expr.elems[1]);
        uint32_t item_cell = 0u;
        if (!item || oem_is_cell(cursor, item, &item_cell) ||
            item->kind == ATOM_VAR)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        if (item->kind != ATOM_EXPR ||
            item->expr.len != relation->algebraic_item_arity ||
            !atom_is_symbol_id(item->expr.elems[0],
                               relation->algebraic_item))
            return oem_algebraic_poll(cursor, length)
                ? OEM_ALGEBRAIC_FAILED : OEM_ALGEBRAIC_HANDOFF;
        if (length == UINT32_MAX ||
            !oem_reserve((void **)&cursor->walk, &cursor->walk_cap,
                         length + 1u, sizeof(*cursor->walk)))
            return OEM_ALGEBRAIC_HANDOFF;
        cursor->walk[length++] =
            item->expr.elems[relation->algebraic_projection];
        value = oem_deref(cursor, value->expr.elems[2]);
    }
    if (!oem_algebraic_poll(cursor, length))
        return OEM_ALGEBRAIC_HANDOFF;
    Atom *cons = atom_symbol_id(&cursor->region, relation->algebraic_cons);
    if (!cons)
        return OEM_ALGEBRAIC_HANDOFF;
    while (length > 0u) {
        value = atom_expr3(
            &cursor->region, cons, cursor->walk[--length], value);
        if (!value)
            return OEM_ALGEBRAIC_HANDOFF;
    }
    *value_out = value;
    return OEM_ALGEBRAIC_READY;
}

static OemAlgebraicRun oem_algebraic_contains(
        CettaOpenEquationCursor *cursor, const OemRelation *relation,
        Atom *input, Atom **value_out) {
    uint64_t inspected = 0u;
    Atom *value = oem_deref(cursor, input);
    for (;;) {
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, value, &cell) || value->kind == ATOM_VAR)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        if (atom_is_symbol_id(value, relation->algebraic_nil)) {
            if (!oem_algebraic_poll(cursor, inspected))
                return OEM_ALGEBRAIC_HANDOFF;
            *value_out = atom_symbol_id(
                &cursor->region, relation->algebraic_false);
            return *value_out ? OEM_ALGEBRAIC_READY
                              : OEM_ALGEBRAIC_HANDOFF;
        }
        if (value->kind != ATOM_EXPR || value->expr.len != 3u ||
            !atom_is_symbol_id(value->expr.elems[0],
                               relation->algebraic_cons))
            return oem_algebraic_poll(cursor, inspected)
                ? OEM_ALGEBRAIC_FAILED : OEM_ALGEBRAIC_HANDOFF;
        Atom *item = oem_deref(cursor, value->expr.elems[1]);
        uint32_t item_cell = 0u;
        if (!item || oem_is_cell(cursor, item, &item_cell) ||
            item->kind == ATOM_VAR)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        inspected++;
        if (item->kind == ATOM_EXPR &&
            item->expr.len == relation->algebraic_item_arity &&
            atom_is_symbol_id(item->expr.elems[0],
                              relation->algebraic_item)) {
            if (!oem_algebraic_poll(cursor, inspected))
                return OEM_ALGEBRAIC_HANDOFF;
            *value_out = atom_symbol_id(
                &cursor->region, relation->algebraic_true);
            return *value_out ? OEM_ALGEBRAIC_READY
                              : OEM_ALGEBRAIC_HANDOFF;
        }
        value = oem_deref(cursor, value->expr.elems[2]);
    }
}

static OemAlgebraicRun oem_algebraic_drop(CettaOpenEquationCursor *cursor,
                                          const OemRelation *relation,
                                          Atom *amount, Atom *input,
                                          Atom **value_out) {
    amount = oem_deref(cursor, amount);
    if (amount->kind != ATOM_GROUNDED || amount->ground.gkind != GV_INT ||
        amount->ground.ival < 0)
        return OEM_ALGEBRAIC_NOT_APPLICABLE;
    uint64_t remaining = (uint64_t)amount->ground.ival, work = 0u;
    Atom *value = oem_deref(cursor, input);
    while (remaining > 0u) {
        uint32_t cell = 0u;
        if (oem_is_cell(cursor, value, &cell) || value->kind == ATOM_VAR)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        if (atom_is_symbol_id(value, relation->algebraic_nil))
            break;
        if (value->kind != ATOM_EXPR || value->expr.len != 3u ||
            !atom_is_symbol_id(value->expr.elems[0],
                               relation->algebraic_cons) ||
            oem_value_representation(value) != PETTA_VALUE_ORDINARY)
            return OEM_ALGEBRAIC_NOT_APPLICABLE;
        value = oem_deref(cursor, value->expr.elems[2]);
        remaining--;
        if (++work == 256u) {
            if (!oem_algebraic_poll(cursor, work))
                return OEM_ALGEBRAIC_HANDOFF;
            work = 0u;
        }
    }
    if (atom_is_error(value) || atom_has_registry_refs(value) ||
        atom_has_thread_local_resource(value))
        return OEM_ALGEBRAIC_NOT_APPLICABLE;
    if (!oem_algebraic_poll(cursor, work))
        return OEM_ALGEBRAIC_HANDOFF;
    *value_out = value;
    return OEM_ALGEBRAIC_READY;
}

static __attribute__((noinline)) OemAlgebraicRun
oem_scalar_sum(CettaOpenEquationCursor *cursor, const OemScalarProgram *program,
               Atom *input, Atom **value_out) {
    typedef struct {
        Atom *value;
        uint32_t function;
    } Pending;
    Pending small[64];
    Pending *work = small;
    uint32_t capacity = 64u, length = 1u;
    work[0] = (Pending){input, 0u};
    uint64_t total = 0u, inspected = 0u;
    OemAlgebraicRun result = OEM_ALGEBRAIC_NOT_APPLICABLE;
    while (length) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OPEN_EQUATION_ALGEBRAIC_SCALAR_NODE);
        Pending pending = work[--length];
        Atom *value = oem_deref(cursor, pending.value);
        if (value->kind == ATOM_VAR ||
            oem_value_representation(value) != PETTA_VALUE_ORDINARY ||
            (value->kind == ATOM_GROUNDED &&
             (!(value->flags & ATOM_FLAG_HASHCONS_ELIGIBLE) ||
              atom_structural_may_have_internal_tag(value))))
            goto done;
        const OemScalarFunction *function =
            &program->functions[pending.function];
        const OemScalarArm *arm = NULL;
        for (uint32_t a = 0; a < function->count; a++) {
            const OemScalarArm *candidate = &function->arms[a];
            if (candidate->wildcard ||
                (candidate->leaf
                     ? atom_is_symbol_id(value, candidate->tag)
                     : value->kind == ATOM_EXPR &&
                           value->expr.len == candidate->fields + 1u &&
                           atom_is_symbol_id(value->expr.elems[0],
                                             candidate->tag))) {
                arm = candidate;
                break;
            }
        }
        if (!arm || arm->value.base > (uint64_t)INT64_MAX - total ||
            arm->value.count > UINT32_MAX - length)
            goto done;
        total += arm->value.base;
        uint32_t needed = length + arm->value.count;
        if (needed > capacity) {
            uint32_t grown = capacity;
            while (grown < needed && grown <= UINT32_MAX / 2u)
                grown *= 2u;
            if (grown < needed ||
                !cetta_expr_len_mul_fits_size(grown, sizeof(*work)))
                goto done;
            Pending *next = malloc(sizeof(*next) * grown);
            if (!next)
                goto done;
            memcpy(next, work, sizeof(*next) * length);
            if (work != small)
                free(work);
            work = next;
            capacity = grown;
        }
        for (uint32_t call = arm->value.count; call-- > 0u;) {
            const OemScalarCall *child = &arm->value.calls[call];
            work[length++] = (Pending){value->expr.elems[child->field + 1u],
                                       child->function};
        }
        if (++inspected == 256u) {
            if (!oem_algebraic_poll(cursor, inspected)) {
                result = OEM_ALGEBRAIC_HANDOFF;
                goto done;
            }
            inspected = 0;
        }
    }
    if (!oem_algebraic_poll(cursor, inspected)) {
        result = OEM_ALGEBRAIC_HANDOFF;
        goto done;
    }
    *value_out = atom_int(&cursor->region, (int64_t)total);
    result = *value_out ? OEM_ALGEBRAIC_READY : OEM_ALGEBRAIC_HANDOFF;
done:
    if (work != small)
        free(work);
    return result;
}

static OemAlgebraicRun oem_algebraic_run(
        CettaOpenEquationCursor *cursor, const OemRelation *relation,
        Atom **args, Atom **value_out) {
    OemAlgebraicRun run;
    CettaRuntimeCounter receipt;
    switch ((OemAlgebraicFold)relation->algebraic_fold) {
    case OEM_ALGEBRAIC_FOLD_SCALAR_SUM:
        run = oem_scalar_sum(cursor, relation->scalar_sum, args[0], value_out);
        receipt = CETTA_RUNTIME_COUNTER_OPEN_EQUATION_ALGEBRAIC_SCALAR_SUM;
        break;
    case OEM_ALGEBRAIC_FOLD_DROP:
        run = oem_algebraic_drop(cursor, relation, args[0], args[1], value_out);
        receipt = CETTA_RUNTIME_COUNTER_OPEN_EQUATION_ALGEBRAIC_DROP;
        break;
    case OEM_ALGEBRAIC_FOLD_LENGTH:
        run = oem_algebraic_length(cursor, relation, args[0], value_out);
        receipt = CETTA_RUNTIME_COUNTER_OPEN_EQUATION_ALGEBRAIC_LENGTH;
        break;
    case OEM_ALGEBRAIC_FOLD_APPEND:
        run = oem_algebraic_append(
            cursor, relation, args[0], args[1], value_out);
        receipt = CETTA_RUNTIME_COUNTER_OPEN_EQUATION_ALGEBRAIC_APPEND;
        break;
    case OEM_ALGEBRAIC_FOLD_PROJECT_MAP:
        run = oem_algebraic_project_map(cursor, relation, args[0], value_out);
        receipt = CETTA_RUNTIME_COUNTER_OPEN_EQUATION_ALGEBRAIC_PROJECT_MAP;
        break;
    case OEM_ALGEBRAIC_FOLD_CONTAINS:
        run = oem_algebraic_contains(cursor, relation, args[0], value_out);
        receipt = CETTA_RUNTIME_COUNTER_OPEN_EQUATION_ALGEBRAIC_CONTAINS;
        break;
    default:
        return OEM_ALGEBRAIC_NOT_APPLICABLE;
    }
    if (run == OEM_ALGEBRAIC_READY)
        cetta_runtime_stats_inc(receipt);
    return run;
}

/* Return a directly computed relation value through the same destination
 * and continuation contract as an equation's RET step. */
static OemRun oem_direct_relation_value(
        CettaOpenEquationCursor *cursor, Atom *value,
        const OemCont *cont, OemEntry *entry, Atom **value_out) {
    OemUnify unified = OEM_UNIFY_OK;
    if (!cont) {
        if (!value_out)
            return OEM_RUN_HANDOFF;
        unified = oem_unify(cursor, cursor->destination, value);
        if (unified != OEM_UNIFY_OK)
            return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED
                                             : OEM_RUN_HANDOFF;
        return oem_publish(cursor, cursor->destination, value_out);
    }
    OemStep destination = {.pattern = cont->pattern,
        .store = cont->store ? OEM_STORE_IF_EMPTY : 0u};
    unified = oem_meet(cursor, cont->program, cont->locals, &destination, value);
    if (unified != OEM_UNIFY_OK)
        return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED : OEM_RUN_HANDOFF;
    *entry = (OemEntry){
        .program = cont->program, .pc = cont->pc, .locals = cont->locals,
        .local_count = cont->local_count, .cont = cont->parent,
        .depth = cont->depth,
    };
    return OEM_RUN_CALLED;
}

/* HE's value of a call whose equations' heads all refused its arguments:
 * the call itself.  It meets the caller's destination as a return's output
 * does (OEM_S_RET), and the caller resumes.  The call of the cursor's own
 * entry publishes it as the answer when `value_out` is given; without it
 * that value goes back to the host. */
static OemRun oem_unmatched_value(CettaOpenEquationCursor *cursor,
                                  const OemRelation *relation, Atom **args,
                                  const OemCont *cont, OemEntry *entry,
                                  Atom **value_out) {
    uint32_t arity = relation->arity;
    Atom **elements = arena_alloc(&cursor->region,
                                  sizeof(*elements) * ((size_t)arity + 1u));
    if (!elements)
        return OEM_RUN_HANDOFF;
    elements[0] = atom_symbol_id(&cursor->region, relation->head);
    if (!elements[0])
        return OEM_RUN_HANDOFF;
    for (uint32_t index = 0u; index < arity; index++)
        elements[index + 1u] = args[index];
    Atom *call = atom_expr(&cursor->region, elements, arity + 1u);
    if (!call)
        return OEM_RUN_HANDOFF;
    OemUnify unified = OEM_UNIFY_OK;
    if (!cont) {
        if (!value_out)
            return OEM_RUN_HANDOFF;
        unified = oem_unify(cursor, cursor->destination, call);
        if (unified != OEM_UNIFY_OK)
            return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED : OEM_RUN_HANDOFF;
        return oem_publish(cursor, cursor->destination, value_out);
    }
    OemStep destination = {.pattern = cont->pattern,
        .store = cont->store ? OEM_STORE_IF_EMPTY : 0u};
    unified = oem_meet(cursor, cont->program, cont->locals, &destination, call);
    if (unified != OEM_UNIFY_OK)
        return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED : OEM_RUN_HANDOFF;
    cursor->stats.unmatched_values++;
    *entry = (OemEntry){
        .program = cont->program, .pc = cont->pc, .locals = cont->locals,
        .local_count = cont->local_count, .cont = cont->parent,
        .depth = cont->depth,
    };
    return OEM_RUN_CALLED;
}

/* The newest frame's call, every equation of which refused its arguments:
 * the frame goes, and the call is its own value. */
static OemRun oem_frame_unmatched_value(CettaOpenEquationCursor *cursor,
                                        const OemRelation *relation,
                                        OemEntry *entry, Atom **value_out) {
    OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
    Atom **args = frame->args;
    const OemCont *cont = frame->cont;
    oem_pop_frame(cursor);
    return oem_unmatched_value(cursor, relation, args, cont, entry, value_out);
}

/* Whether a call's destination is a cell no term has bound: the cursor's
 * own, or the output hole the caller's continuation passes.  A demanded
 * destination meets each equation's exposed output before its body runs,
 * which prunes equations a value computed first would run. */
static inline bool oem_destination_open(
        const CettaOpenEquationCursor *cursor, const OemCont *cont) {
    Atom *destination = cursor->destination;
    if (cont) {
        const OemTemplate *pattern = &cont->program->templates[cont->pattern];
        if (pattern->kind != OEM_T_SLOT)
            return false;
        destination = cont->locals[pattern->slot];
        if (!destination)
            return true;
    }
    uint32_t index = 0u;
    return destination &&
        oem_is_cell(cursor, oem_deref(cursor, destination), &index);
}

/* A named recursive relation enters the callee's prepared owner for closed
 * inputs. Refusal leaves this ready call to the region's equations without
 * changing admission for another input, caller or cursor. */
static __attribute__((noinline)) OemRun oem_pure_relation_value(
        CettaOpenEquationCursor *cursor,
        const CettaOpenEquationProgram *version, uint32_t relation_index,
        Atom **args, const OemCont *cont, OemEntry *entry, Atom **value_out) {
    const OemRelation *relation = &version->relations[relation_index];
    uint32_t arity = relation->arity;
    Atom *arguments[OEM_PURE_RELATION_INLINE_ARGS];
    bool closed = arity <= OEM_PURE_RELATION_INLINE_ARGS;
    for (uint32_t index = 0u; closed && index < arity; index++) {
        arguments[index] = args[index]
            ? oem_resolve_operand(cursor, args[index]) : NULL;
        closed = arguments[index] && !atom_has_vars(arguments[index]) &&
            !atom_has_registry_refs(arguments[index]);
    }
    CettaCallOutcome returned;
    bool handled = closed && cursor->runtime.execute_pure_relation(
              cursor->runtime.context, version->space, &cursor->region,
              relation->head, arguments, arity, &returned);
    if (!handled)
        return OEM_RUN_DECLINED;
    Atom *value = NULL;
    OemRun completed = oem_prepared_call_result(cursor, returned, &value, value_out);
    return completed == OEM_RUN_CALLED
        ? oem_direct_relation_value(cursor, value, cont, entry, value_out)
        : completed;
}

/* A call's entry from the body that makes it.  Selection runs on the
 * call's arguments as it would on a frame just pushed, in the same state:
 * the first equation it admits is activated here, and a frame is pushed,
 * before the activation, only when an equation after that one remains to
 * be tried.  The frame holds a copy of the arguments, the candidates and
 * the next alternative, as the loop leaves a frame it has entered.  So a
 * deterministic call makes no frame, where one pushed at the call would be
 * popped before its only activation. */
static inline __attribute__((always_inline)) OemRun oem_enter(CettaOpenEquationCursor *cursor,
                        const CettaOpenEquationProgram *version,
                        uint32_t relation_index, Atom **args,
                        const OemCont *cont, uint32_t depth,
                        OemEntry *entry, Atom **value_out, const bool he) {
    if (cursor->runtime.depth_bound && !version->depth_pruning_safe) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_UNSUPPORTED;
        return OEM_RUN_HANDOFF;
    }
    const OemRelation *relation = &version->relations[relation_index];
    if (relation->algebraic_fold != OEM_ALGEBRAIC_FOLD_NONE &&
        !cursor->runtime.activation_budget && !cursor->runtime.depth_bound &&
        oem_version_current(cursor, version)) {
        Atom *value = NULL;
        OemAlgebraicRun algebraic = oem_algebraic_run(
            cursor, relation, args, &value);
        if (algebraic == OEM_ALGEBRAIC_READY)
            return oem_direct_relation_value(
                cursor, value, cont, entry, value_out);
        if (algebraic == OEM_ALGEBRAIC_FAILED)
            return OEM_RUN_FAILED;
        if (algebraic == OEM_ALGEBRAIC_HANDOFF)
            return OEM_RUN_HANDOFF;
    }
    if (!he &&
        relation->pure_entry_candidate &&
        cursor->runtime.execute_pure_relation &&
        !cursor->runtime.activation_budget && !cursor->runtime.depth_bound &&
        oem_version_current(cursor, version) &&
        (!cursor->runtime.pure_relation_declined ||
         !cursor->runtime.pure_relation_declined(cursor->runtime.context,
             version->space, relation->head, relation->arity)) &&
        oem_destination_open(cursor, cont)) {
        OemRun run = oem_pure_relation_value(cursor, version, relation_index,
                                             args, cont, entry, value_out);
        if (run != OEM_RUN_DECLINED)
            return run;
    }
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
    bool value_rule = he &&
        !relation->local;
    if (next >= count)
        return value_rule
            ? oem_unmatched_value(cursor, relation, args, cont, entry,
                                  value_out)
            : OEM_RUN_FAILED;
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
    uint32_t frame_index = cursor->frame_len;
    if (own_frame || value_rule) {
        /* The arguments outlive the registers: the frame's alternatives,
         * or the call's own value, read them after the activation. */
        uint32_t arity = relation->arity;
        held = oem_region_alloc(cursor,
                                sizeof(*held) * (arity ? arity : 1u));
        if (!held)
            return OEM_RUN_HANDOFF;
        memcpy(held, args, sizeof(*held) * arity);
    }
    if (own_frame) {
        if (!oem_push_frame(cursor, version, relation_index, held, cont,
                            depth))
            return OEM_RUN_HANDOFF;
        OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
        frame->candidates = candidates;
        frame->next = next;
    }
    cursor->params_matched = false;
    OemRun run = oem_activate(cursor, version, equation, held,
                              relation->arity, cont, own_frame, entry);
    if (value_rule) {
        if (cursor->params_matched) {
            if (own_frame && frame_index < cursor->frame_len)
                cursor->frames[frame_index].head_matched = true;
        } else if (!own_frame && run == OEM_RUN_FAILED) {
            return oem_unmatched_value(cursor, relation, held, cont, entry,
                                       value_out);
        }
    }
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
static inline __attribute__((always_inline)) bool oem_settle(
    CettaOpenEquationCursor *cursor, uint32_t base) {
    while (cursor->frame_len > base) {
        OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
        oem_restore(cursor, frame);
        if (__builtin_expect(oem_frame_is_match(frame), 0)) {
            if (frame->lazy_rows || frame->next < frame->rows->len)
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

/* ---- versions ------------------------------------------------------------ */

/* A set of pointers, open-addressed over a power-of-two table. */
typedef struct {
    const void **slots;
    uint32_t cap;
    uint32_t len;
} OemPointerSet;

/* Add `key`: true when it was absent.  `*failed` is set when memory ran
 * out, and the set is then incomplete. */
static bool oem_pointer_set_add(OemPointerSet *set, const void *key,
                                bool *failed) {
    if (((size_t)set->len + 1u) * 2u > set->cap) {
        uint32_t cap = set->cap ? set->cap * 2u : 64u;
        const void **slots = calloc(cap, sizeof(*slots));
        if (!slots) {
            *failed = true;
            return false;
        }
        for (uint32_t index = 0u; index < set->cap; index++) {
            const void *moved = set->slots[index];
            if (!moved)
                continue;
            size_t at = oem_collect_hash(moved, cap);
            while (slots[at])
                at = (at + 1u) & (cap - 1u);
            slots[at] = moved;
        }
        free(set->slots);
        set->slots = slots;
        set->cap = cap;
    }
    size_t at = oem_collect_hash(key, set->cap);
    while (set->slots[at]) {
        if (set->slots[at] == key)
            return false;
        at = (at + 1u) & (set->cap - 1u);
    }
    set->slots[at] = key;
    set->len++;
    return true;
}

static bool oem_pointer_set_has(const OemPointerSet *set, const void *key) {
    if (!set->cap)
        return false;
    for (size_t at = oem_collect_hash(key, set->cap); set->slots[at];
         at = (at + 1u) & (set->cap - 1u)) {
        if (set->slots[at] == key)
            return true;
    }
    return false;
}

enum { OEM_VERSION_SWEEP_FLOOR = 16u };

/* The fewest versions a sweep waits for; under the collection stress test,
 * one, so every new version is followed by a sweep. */
static uint32_t oem_version_sweep_floor(void) {
    return oem_collect_sizes()->stress ? 1u : OEM_VERSION_SWEEP_FLOOR;
}

/* The programs a continuation chain runs, up to the first continuation an
 * earlier chain met. */
static void oem_mark_cont_programs(const OemCont *cont, OemPointerSet *conts,
                                   OemPointerSet *live, bool *failed,
                                   size_t *walked) {
    for (; cont && !*failed; cont = cont->parent) {
        if (!oem_pointer_set_add(conts, cont, failed))
            return;
        (*walked)++;
        if (cont->program)
            (void)oem_pointer_set_add(live, cont->program, failed);
    }
}

/* Release the program versions the cursor no longer needs: those no live
 * frame or continuation runs and that are not the newest version of their
 * relation, which the next call of it likely enters again.  A frame holds
 * its program and a continuation its program; nothing else in the cursor
 * names a version, since no term holds a program's storage
 * (oem_shared_leaf).  Run where the frames are the whole state, as a
 * collection is.  The next sweep comes once the versions grow by what this
 * one kept, or by a share of what it walked, so sweeping costs a bounded
 * amount per version entered. */
static void oem_sweep_versions(CettaOpenEquationCursor *cursor) {
    OemPointerSet live = {0};
    OemPointerSet conts = {0};
    OemPointerSet relations = {0};
    bool failed = false;
    size_t walked = cursor->frame_len;
    for (uint32_t index = 0u; index < cursor->frame_len && !failed; index++) {
        const OemFrame *frame = &cursor->frames[index];
        if (!oem_frame_is_match(frame) && !oem_frame_is_host(frame) &&
            frame->relation != OEM_COLLECT_FRAME &&
            frame->relation != OEM_PREPARED_FRAME && frame->program)
            (void)oem_pointer_set_add(&live, frame->program, &failed);
        oem_mark_cont_programs(frame->cont, &conts, &live, &failed, &walked);
    }
    oem_mark_cont_programs(cursor->collection_after, &conts, &live, &failed,
                           &walked);
    if (!failed) {
        /* Newest first: the first entry met for a relation is its newest. */
        for (uint32_t index = cursor->version_len; index-- > 0u;) {
            const CettaOpenEquationProgram *version = cursor->versions[index];
            const OemRelation *relation = &version->relations[0];
            /* The relation's key in one word: head, arity and entry kind,
             * offset so it is never null. */
            uint64_t key = ((uint64_t)relation->head << 32u) |
                ((uint64_t)(relation->arity & 0x3FFFFFFFu) << 2u) |
                ((uint64_t)version->entry_kind & 3u);
            bool newest = oem_pointer_set_add(
                &relations, (const void *)(uintptr_t)(key + 1u), &failed);
            if (failed)
                break;
            if (newest || oem_pointer_set_has(&live, version))
                continue;
            cetta_open_equation_program_release(cursor->versions[index]);
            cursor->versions[index] = NULL;
        }
    }
    /* Nothing is released past an incomplete mark or table; the entries
     * released before a table ran out close up with the rest. */
    uint32_t write = 0u;
    for (uint32_t index = 0u; index < cursor->version_len; index++) {
        if (!cursor->versions[index])
            continue;
        cursor->versions[write] = cursor->versions[index];
        cursor->version_epochs[write] = cursor->version_epochs[index];
        write++;
    }
    if (cursor->ledger_counted && write != cursor->version_len)
        cetta_runtime_stats_gauge_add(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_VERSIONS_LIVE,
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_VERSIONS_LIVE_PEAK,
            -(int64_t)(cursor->version_len - write));
    cursor->version_len = write;
    uint32_t kept = write;
    cursor->checked_program = NULL;
    free(live.slots);
    free(conts.slots);
    free(relations.slots);
    size_t step = oem_version_sweep_floor();
    if (!oem_collect_sizes()->stress) {
        if (kept > step)
            step = kept;
        if (walked / 64u > step)
            step = walked / 64u;
    }
    size_t next = (size_t)kept + step;
    cursor->version_sweep_at = next > UINT32_MAX ? UINT32_MAX
                                                 : (uint32_t)next;
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
    if (!cursor || !arena)
        return false;
    bool answers = arena == cursor->answer_arena;
    bool stable = arena == cursor->runtime.stable;
    bool unborrowed = (answers && !cursor->borrows_answer_arena) ||
        (stable && !cursor->borrows_stable);
    /* Under the collection stress test the skip is checked: the detach runs
     * anyway, and must find nothing of that storage to copy. */
    if (unborrowed && !oem_collect_sizes()->stress)
        return true;
    uint64_t copied = cursor->detach_copied;
    if (!oem_collect_generation(cursor, arena->identity, true))
        return false;
    if (unborrowed && cursor->detach_copied != copied) {
        fprintf(stderr, "fatal: an open-equation cursor held storage it did "
                        "not record borrowing\n");
        abort();
    }
    if (answers)
        cursor->borrows_answer_arena = false;
    if (stable)
        cursor->borrows_stable = false;
    return true;
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
            if (var_id_eq(cursor->fresh_vars[index].id, id))
                return cursor->fresh_vars[index].cell;
        }
    } else {
        /* The index proposes, the list confirms (VariableIndex.lookup):
         * entries left by an earlier copy are never confirmed. */
        uint32_t position =
            cetta_var_index_propose(&cursor->fresh_index, id);
        if (position < len && var_id_eq(cursor->fresh_vars[position].id, id))
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
    if (term->kind != ATOM_EXPR || term->expr.len != row->expr.len ||
        !oem_decomposition_compatible(term, row))
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
    if (row->kind != ATOM_EXPR || row->expr.len != item->count ||
        oem_value_representation(row) != item->representation)
        return OEM_UNIFY_FAIL;
    for (uint32_t index = 0u; index < item->count; index++) {
        uint32_t child_index = program->template_children[item->first + index];
        const OemTemplate *child = &program->templates[child_index];
        /* Literal and slot leaves already have the operand the row unifier
         * reads. Preserve its freshening, occurs and trail discipline. */
        OemUnify unified = child->kind == OEM_T_LITERAL
            ? oem_unify_row(cursor, child->literal, row->expr.elems[index], depth + 1u)
            : child->kind == OEM_T_SLOT
            ? oem_unify_row(cursor, locals[child->slot], row->expr.elems[index], depth + 1u)
            : oem_unify_template_row(cursor, program, locals, child_index,
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

/* ── Stored rows read in place ──────────────────────────────────────────
 * A row a lazy match offers is its id in the space's universe, and its
 * parts are compared where they are stored.  A cell that takes a ground
 * part gets it as a view built in the region; one that takes a part holding
 * a variable gets a fresh copy built there, with a fresh cell for each of
 * the row's variables, since a row is universally quantified and each match
 * of it gets its own.  Backtracking and the region's collections reclaim
 * both like any other term the match builds.  An atom (a symbol, a number,
 * a string) is the universe's own atom of it, one per distinct atom.  A
 * part this reader does not take apart (a list or a cell holding a
 * variable, a value with its own representation) is read as the universe's
 * atom of it, as before. */

/* How the reader takes the stored part with header `hdr`: apart, as a
 * ground part or as one holding a variable, or whole, as the universe's
 * atom of it. */
typedef enum {
    OEM_STORED_WHOLE = 0,
    OEM_STORED_GROUND,
    OEM_STORED_OPEN,
} OemStoredShape;

static inline OemStoredShape oem_stored_shape(const TermUniverse *universe,
                                              AtomId id,
                                              const CettaTermHdr *hdr,
                                              uint32_t depth) {
    if (!hdr || depth > OEM_MAX_DEPTH)
        return OEM_STORED_WHOLE;
    if ((hdr->aux32 & CETTA_TERM_HDR_HAS_VARS) == 0u)
        return tu_plain_ground(universe, id, hdr) ? OEM_STORED_GROUND
                                                  : OEM_STORED_WHOLE;
    return hdr->tag == ATOM_VAR ||
            (hdr->tag == ATOM_EXPR && tu_plain_expr(universe, id, hdr))
        ? OEM_STORED_OPEN : OEM_STORED_WHOLE;
}

/* The atom a cell takes for the ground stored part `id`: the universe's
 * own, decoded and kept, as a decoded row's part was.  A value built from it
 * shares it, so comparing that value with the stored part later meets the
 * same atom (oem_unify_stored) rather than walking a copy. */
static inline Atom *oem_stored_view(CettaOpenEquationCursor *cursor,
                                    const TermUniverse *universe, AtomId id) {
    (void)cursor;
    return term_universe_get_atom(universe, id);
}

/* A fresh copy, in the region, of the stored part `id`, as oem_import_fresh
 * copies a row's atom: a variable is its fresh cell, a ground part its
 * view, an expression holding a variable built over its parts' copies.
 * NULL for a part holding a variable that this reader does not take apart;
 * the caller reads the row's atom. */
static Atom *oem_import_stored(CettaOpenEquationCursor *cursor,
                               const TermUniverse *universe, AtomId id,
                               uint32_t depth) {
    const CettaTermHdr *hdr = tu_hdr_stored(universe, id);
    if (!hdr || depth > OEM_MAX_DEPTH)
        return NULL;
    if ((hdr->aux32 & CETTA_TERM_HDR_HAS_VARS) == 0u)
        return oem_stored_view(cursor, universe, id);
    if (hdr->tag == ATOM_VAR)
        return oem_fresh_var(cursor, tu_var_id(universe, id));
    if (hdr->tag != ATOM_EXPR || !tu_plain_expr(universe, id, hdr))
        return NULL;
    CettaExprLen len = (CettaExprLen)(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK);
    Atom *inline_elems[16];
    Atom **elems = len <= 16u
        ? inline_elems
        : arena_alloc(&cursor->region, sizeof(*elems) * (size_t)len);
    if (!elems)
        return NULL;
    for (CettaExprIndex index = 0u; index < len; index++) {
        elems[index] = oem_import_stored(
            cursor, universe, tu_child_from_header(universe, hdr, index),
            depth + 1u);
        if (!elems[index])
            return NULL;
    }
    return atom_expr(&cursor->region, elems, len);
}

/* Whether the symbol TERM meets the stored part `part`, read at DEPTH,
 * where the part is stored: when the part is a symbol within the depth the
 * reader takes parts apart, *OUT is whether their ids agree, as
 * oem_unify_stored compares them; otherwise false, and the call decides. */
static inline bool oem_stored_symbol_meets(const TermUniverse *universe,
                                           const Atom *term, AtomId part,
                                           uint32_t depth, OemUnify *out) {
    if (term->kind != ATOM_SYMBOL || depth > OEM_MAX_DEPTH)
        return false;
    const CettaTermHdr *hdr = tu_hdr_stored(universe, part);
    if (!hdr || hdr->tag != ATOM_SYMBOL)
        return false;
    *out = hdr->sym_or_head == term->sym_id ? OEM_UNIFY_OK : OEM_UNIFY_FAIL;
    return true;
}

/* A region term unified with the stored part `row`, as oem_unify_row
 * unifies it with the part's atom. */
static OemUnify oem_unify_stored(CettaOpenEquationCursor *cursor, Atom *term,
                                 const TermUniverse *universe, AtomId row,
                                 uint32_t depth) {
    const CettaTermHdr *hdr = tu_hdr_stored(universe, row);
    OemStoredShape shape = oem_stored_shape(universe, row, hdr, depth);
    if (shape == OEM_STORED_WHOLE) {
        Atom *atom = term_universe_get_atom(universe, row);
        return atom ? oem_unify_row(cursor, term, atom, depth)
                    : OEM_UNIFY_ERROR;
    }
    if (shape == OEM_STORED_OPEN && hdr->tag == ATOM_VAR) {
        Atom *cell = oem_fresh_var(cursor, tu_var_id(universe, row));
        return cell ? oem_unify(cursor, term, cell) : OEM_UNIFY_ERROR;
    }
    Atom *value = oem_deref(cursor, term);
    /* A value that is the part's own kept atom is the part.  Only an
     * expression of the universe's arena can be one, so other values skip
     * reading the part's slot. */
    if (shape == OEM_STORED_GROUND && value->kind == ATOM_EXPR &&
        universe->persistent_arena &&
        (value->arena_id == 0u ||
         value->arena_id == universe->persistent_arena->identity) &&
        value == term_universe_decoded_atom_at(universe, row))
        return OEM_UNIFY_OK;
    uint32_t cell = 0u;
    if (shape == OEM_STORED_OPEN) {
        if (oem_is_cell(cursor, value, &cell) || oem_is_list_cell(value)) {
            Atom *copy = oem_import_stored(cursor, universe, row, depth);
            if (!copy) {
                Atom *atom = term_universe_get_atom(universe, row);
                return atom ? oem_unify_row(cursor, term, atom, depth)
                            : OEM_UNIFY_ERROR;
            }
            return oem_unify(cursor, value, copy);
        }
        if (value->kind != ATOM_EXPR ||
            value->expr.len !=
                (CettaExprLen)(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) ||
            oem_value_representation(value) != PETTA_VALUE_ORDINARY)
            return value->kind == ATOM_VAR ? OEM_UNIFY_ERROR
                                           : OEM_UNIFY_FAIL;
    } else if (oem_is_cell(cursor, value, &cell)) {
        Atom *view = oem_stored_view(cursor, universe, row);
        return view && oem_bind(cursor, cell, view) ? OEM_UNIFY_OK
                                                    : OEM_UNIFY_ERROR;
    } else if (hdr->tag != ATOM_EXPR) {
        /* A symbol and an integer compare where they are stored. */
        if (value->kind == ATOM_SYMBOL && hdr->tag == ATOM_SYMBOL)
            return value->sym_id == hdr->sym_or_head ? OEM_UNIFY_OK
                                                     : OEM_UNIFY_FAIL;
        if (value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT &&
            hdr->tag == ATOM_GROUNDED && hdr->subtag == GV_INT)
            return value->ground.ival == tu_int_from_header(hdr)
                ? OEM_UNIFY_OK : OEM_UNIFY_FAIL;
        Atom *atom = term_universe_get_atom(universe, row);
        if (!atom)
            return OEM_UNIFY_ERROR;
        if (value == atom)
            return OEM_UNIFY_OK;
        if (value->kind != ATOM_EXPR && value->kind != ATOM_VAR)
            return atom_eq(value, atom) ? OEM_UNIFY_OK : OEM_UNIFY_FAIL;
        return oem_unify(cursor, value, atom);
    } else if (value->kind != ATOM_EXPR || oem_is_list_cell(value) ||
               oem_value_representation(value) != PETTA_VALUE_ORDINARY) {
        Atom *view = oem_stored_view(cursor, universe, row);
        return view ? oem_unify(cursor, value, view) : OEM_UNIFY_ERROR;
    } else if (value->expr.len !=
               (CettaExprLen)(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK)) {
        return OEM_UNIFY_FAIL;
    }
    CettaExprLen len = (CettaExprLen)(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK);
    for (CettaExprIndex index = 0u; index < len; index++) {
        Atom *child = value->expr.elems[index];
        AtomId part = tu_child_from_header(universe, hdr, index);
        OemUnify unified = OEM_UNIFY_OK;
        if (!oem_stored_symbol_meets(universe, child, part, depth + 1u,
                                     &unified))
            unified = oem_unify_stored(cursor, child, universe, part,
                                       depth + 1u);
        if (unified != OEM_UNIFY_OK)
            return unified;
    }
    return OEM_UNIFY_OK;
}

/* A match's pattern, read in place over `locals`, unified with the stored
 * part `row`, as oem_unify_template_row unifies it with the part's atom. */
static OemUnify oem_unify_template_stored(
    CettaOpenEquationCursor *cursor, const CettaOpenEquationProgram *program,
    Atom **locals, uint32_t template_index, const TermUniverse *universe,
    AtomId row, uint32_t depth) {
    const OemTemplate *item = &program->templates[template_index];
    const CettaTermHdr *hdr = tu_hdr_stored(universe, row);
    if (oem_stored_shape(universe, row, hdr, depth) == OEM_STORED_WHOLE) {
        Atom *atom = term_universe_get_atom(universe, row);
        return atom ? oem_unify_template_row(cursor, program, locals,
                                             template_index, atom, depth)
                    : OEM_UNIFY_ERROR;
    }
    if (item->kind == OEM_T_LITERAL)
        return oem_unify_stored(cursor, item->literal, universe, row, depth);
    if (item->kind == OEM_T_SLOT)
        return oem_unify_stored(cursor, locals[item->slot], universe, row,
                                depth);
    if (item->kind != OEM_T_BUILD || hdr->tag == ATOM_VAR) {
        Atom *built = oem_instantiate(cursor, program, locals,
                                      template_index);
        return built ? oem_unify_stored(cursor, built, universe, row, depth)
                     : OEM_UNIFY_ERROR;
    }
    if (hdr->tag != ATOM_EXPR ||
        (CettaExprLen)(hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) != item->count ||
        item->representation != PETTA_VALUE_ORDINARY)
        return OEM_UNIFY_FAIL;
    for (uint32_t index = 0u; index < item->count; index++) {
        uint32_t child_index = program->template_children[item->first + index];
        const OemTemplate *child = &program->templates[child_index];
        AtomId part = tu_child_from_header(universe, hdr, index);
        Atom *term = child->kind == OEM_T_LITERAL ? child->literal
            : child->kind == OEM_T_SLOT ? locals[child->slot]
            : NULL;
        OemUnify unified = OEM_UNIFY_OK;
        if (!term)
            unified = oem_unify_template_stored(cursor, program, locals,
                                                child_index, universe, part,
                                                depth + 1u);
        else if (!oem_stored_symbol_meets(universe, term, part, depth + 1u,
                                          &unified))
            unified = oem_unify_stored(cursor, term, universe, part,
                                       depth + 1u);
        if (unified != OEM_UNIFY_OK)
            return unified;
    }
    return OEM_UNIFY_OK;
}

/* One stored row of a lazy match, as oem_try_candidate tries a row. */
static __attribute__((noinline)) OemRun oem_try_stored(
                                CettaOpenEquationCursor *cursor,
                                Atom *pattern, const TermUniverse *universe,
                                AtomId row) {
    cursor->stats.match_candidates++;
    cursor->fresh_var_len = 0u;
    /* The row's records are read, and its parts viewed or decoded, as one
     * transition of the shared universe. */
    CETTA_SCOPED_SHARED_TRANSITION(read);
    OemUnify unified = oem_unify_stored(cursor, pattern, universe, row, 0u);
    if (unified != OEM_UNIFY_OK)
        return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED : OEM_RUN_HANDOFF;
    return OEM_RUN_CALLED;
}

/* One stored row of a lazy match whose frame keeps no built pattern, as
 * oem_try_candidate_in_place tries a row. */
static __attribute__((noinline)) OemRun oem_try_stored_in_place(
                                CettaOpenEquationCursor *cursor,
                                const OemCont *resume,
                                const TermUniverse *universe, AtomId row) {
    cursor->stats.match_candidates++;
    cursor->fresh_var_len = 0u;
    const OemStep *step = &resume->program->steps[resume->pc - 1u];
    CETTA_SCOPED_SHARED_TRANSITION(read);
    OemUnify unified = oem_unify_template_stored(
        cursor, resume->program, resume->locals, step->pattern, universe, row,
        0u);
    if (unified != OEM_UNIFY_OK)
        return unified == OEM_UNIFY_FAIL ? OEM_RUN_FAILED : OEM_RUN_HANDOFF;
    return OEM_RUN_CALLED;
}

/* CETTA_OEM_STORED_REFERENCE=1 reads every match from its snapshot, whose
 * rows are decoded, as the machine read them before it read a large
 * space's rows by their ids: the stored-row law compares the two. */
static __attribute__((noinline)) bool oem_stored_reference(void) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("CETTA_OEM_STORED_REFERENCE");
        enabled = setting && strcmp(setting, "1") == 0;
    }
    return enabled != 0;
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
    cursor->ledger_counted = cetta_runtime_stats_is_enabled();
    if (cursor->ledger_counted)
        cetta_runtime_stats_gauge_add(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_LIVE,
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_LIVE_PEAK, 1);
    cursor->stored_reference = oem_stored_reference();
    cursor->program = program;
    atomic_fetch_add_explicit(&program->refs, 1u, memory_order_relaxed);
    cursor->answer_arena = answer_arena;
    cursor->collect_after = oem_collect_sizes()->first;
    cursor->version_sweep_at = oem_version_sweep_floor();
    cursor->major_after = oem_collect_sizes()->major;
    if (runtime)
        cursor->runtime = *runtime;
    arena_init(&cursor->region);
    arena_set_runtime_kind(&cursor->region, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_ledger_tag(&cursor->region, CETTA_ARENA_LEDGER_CURSOR);
    arena_set_block_growth(&cursor->region, OEM_FIRST_BLOCK_BYTES);
    arena_set_hashcons(&cursor->region, NULL);
    oem_link_generations(cursor);
    arena_init(&cursor->names);
    arena_set_runtime_kind(&cursor->names, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_ledger_tag(&cursor->names, CETTA_ARENA_LEDGER_CURSOR);
    arena_set_block_growth(&cursor->names, OEM_FIRST_BLOCK_BYTES);
    arena_set_hashcons(&cursor->names, NULL);
    /* A cursor that cannot open is not opened, and its caller evaluates
     * the call the reference way: the refusal loses nothing. */
    if (!cetta_frame_identity_acquire_quiet(&cursor->epoch)) {
        cursor->epoch = CETTA_FRAME_IDENTITY_NONE;
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
    /* Most searches stay a few frames deep; a deeper one doubles from here. */
    cursor->frames = malloc(OEM_FIRST_FRAMES * sizeof(*cursor->frames));
    cursor->frame_cap = cursor->frames ? OEM_FIRST_FRAMES : 0u;
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
    /* A body entry follows its caller's complete protocol: it enters the
     * trusted version when the arguments meet its entry facts. */
    uint32_t entry = 0u;
    if (ok && program->entry_kind != CETTA_OPEN_ENTRY_READY_CALL &&
        program->relations[0].has_trusted &&
        oem_entry_facts_hold(cursor, program, 0u, imported))
        entry = program->relations[0].trusted;
    ok = ok && oem_push_frame(cursor, program, entry, imported, NULL, 1u);
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
    CettaCallOutcome *returned, Atom ***query_values_out) {
    return cetta_open_equation_cursor_next_above(cursor, 0u, query_vars,
                                                 returned, query_values_out);
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
    cursor->host_return = cetta_call_failure();
    cursor->host_recovers = false;
    cursor->host_own_recovers = false;
    cursor->host_terminal = false;
    cursor->completed_collection = NULL;
    cursor->collection_after = NULL;
    cursor->raise_height = 0u;
    cursor->answer_weight = 1u;
    cursor->answer_folded = false;
    if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT)
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_NONE;
    if (cursor->handoff != CETTA_OPEN_EQUATION_HANDOFF_NONE)
        return false;
    cursor->query_vars = query_vars;
    return true;
}

/* A committed effect cannot be replayed through whole-call fallback.
 * Interrupt and activation-budget stops keep the exact live cursor and
 * can resume it; other handoffs cannot restart an entered effect. */
static CettaOpenEquationStep oem_cursor_outcome(
    CettaOpenEquationCursor *cursor, CettaOpenEquationStep step,
    Atom *value, CettaCallOutcome *returned) {
    /* Only a completed RET (or the dialect's unmatched-call value) can
     * publish ANSWER. Host work leaves its actual continuation in the
     * cursor and returns HOST instead; the answer's shape cannot reopen it. */
    if (step == CETTA_OPEN_EQUATION_ANSWER ||
        step == CETTA_OPEN_EQUATION_RAISE) {
        if (!value) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_SERVICE_ERROR;
            return CETTA_OPEN_EQUATION_FAULT;
        }
        *returned = step == CETTA_OPEN_EQUATION_ANSWER
            ? cetta_call_completed_value(value) : cetta_call_raised(value);
    }
    if (step == CETTA_OPEN_EQUATION_HANDOFF &&
        cursor->effected &&
        cursor->handoff != CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT &&
        cursor->handoff != CETTA_OPEN_EQUATION_HANDOFF_BUDGET)
        return CETTA_OPEN_EQUATION_FAULT;
    return step;
}

CettaOpenEquationStep cetta_open_equation_cursor_next_above(
    CettaOpenEquationCursor *cursor, uint32_t base, Atom *const *query_vars,
    CettaCallOutcome *returned, Atom ***query_values_out) {
    Atom *value = NULL;
    if (!returned ||
        !oem_cursor_enter(cursor, query_vars, &value, query_values_out))
        return CETTA_OPEN_EQUATION_HANDOFF;
    CettaOpenEquationStep step = oem_cursor_run(cursor, base, &value,
                                                query_values_out);
    cursor->query_vars = NULL;
    return oem_cursor_outcome(cursor, step, value, returned);
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

bool cetta_open_equation_cursor_host_terminal(
    const CettaOpenEquationCursor *cursor, Atom ***query_values_out) {
    if (!cursor || !cursor->host_goal || !cursor->host_terminal)
        return false;
    *query_values_out = cursor->query_values;
    return true;
}

bool cetta_open_equation_cursor_host_return(
    const CettaOpenEquationCursor *cursor, CettaCallOutcome *outcome) {
    if (!cursor || !outcome || !cursor->host_goal ||
        cursor->host_mode != CETTA_OPEN_EQUATION_HOST_NATIVE_RETURN)
        return false;
    *outcome = cursor->host_return;
    return true;
}

/* Each branch ends at the activation's return. Check both successors while
 * the captured caller chain is checked once, independently of branch count. */
static bool oem_export_activation_supported(
    const CettaOpenEquationProgram *program, uint32_t pc,
    bool *returns_out, bool *yields_out) {
    *returns_out = *yields_out = false;
    uint32_t *pending = NULL;
    uint32_t len = 0u, cap = 0u;
    bool ok = oem_reserve((void **)&pending, &cap, 1u, sizeof(*pending));
    if (ok)
        pending[len++] = pc;
    while (ok && len) {
        pc = pending[--len];
        bool ended = false;
        while (ok && !ended) {
            if (pc >= program->step_len) {
                ok = false;
                break;
            }
            const OemStep *step = &program->steps[pc];
            switch (step->kind) {
            case OEM_S_TAIL:
            case OEM_S_CALL:
                ok = !program->relations[step->relation].local ||
                     (program->relations[step->relation].fold_step !=
                          SYMBOL_ID_NONE && step->arg_count == 2u);
                ended = step->kind == OEM_S_TAIL;
                *returns_out |= ended;
                pc++;
                break;
            case OEM_S_RET:
                *returns_out = true;
                ended = true;
                break;
            case OEM_S_YIELD:
                *yields_out = true;
                ended = true;
                break;
            case OEM_S_FAIL:
                ended = true;
                break;
            case OEM_S_ENTRY:
            case OEM_S_BUILTIN_ENTRY:
            case OEM_S_BUILTIN_PRIM:
            case OEM_S_BUILTIN_TEST:
            case OEM_S_BUILTIN_FAIL:
            case OEM_S_BUILTIN_HOST:
            case OEM_S_BUILTIN_BIND:
            case OEM_S_BUILTIN_CALL:
                ok = step->target > pc;
                ended = oem_entry_tail(program, step) != NULL;
                *returns_out |= ended;
                pc = step->target;
                break;
            case OEM_S_TEST:
            case OEM_S_CASE:
                ok = step->target > pc &&
                    oem_reserve((void **)&pending, &cap, len + 1u,
                                sizeof(*pending));
                if (ok)
                    pending[len++] = step->target;
                pc++;
                break;
            case OEM_S_HOST:
                pc++;
                break;
            case OEM_S_CUT:
                ok = step->target < program->step_len && step->target != pc;
                pc = step->target;
                break;
            case OEM_S_EQUATION_CUT:
            case OEM_S_PRIM:
            case OEM_S_BIND:
            case OEM_S_MATCH:
                pc++;
                break;
            default:
                /* Entered collection and commit receivers have separate
                 * adapters; no prefix is exported if one is unavailable. */
                ok = false;
                break;
            }
        }
    }
    free(pending);
    return ok;
}

bool cetta_open_equation_cursor_host_continuation_supported(
    const CettaOpenEquationCursor *cursor) {
    const OemFrame *frame = cursor && cursor->frame_len
        ? &cursor->frames[cursor->frame_len - 1u] : NULL;
    if (!frame || !oem_frame_is_host(frame))
        return false;
    for (const OemCont *entry = frame->cont; entry; entry = entry->parent) {
        bool returns, yields;
        if (!oem_export_activation_supported(entry->program, entry->pc,
                                              &returns, &yields))
            return false;
        if (yields && !returns)
            break;
    }
    return true;
}

typedef struct {
    Atom **source;
    Atom **values;
    uint32_t count;
} OemExportLocals;

typedef struct {
    CettaOpenEquationCursor *cursor;
    OemExportLocals *locals;
    uint32_t len, cap;
    bool (*emit)(void *, CettaOpenEquationHostMode, Atom *, Atom *,
                 const PettaPlanNode *, uint32_t, uint32_t);
    void *context;
    uint32_t goal_count;
    /* The retained host boundary remains meaningful after producer HOST
     * frames have been exhausted, e.g. for a cut after collection. */
    uint32_t saved_height;
} OemContinuationExport;

/* One shadow per activation, including slots which a future first store
 * fills. The original OEM slots and their rollback trail remain untouched. */
static Atom **oem_export_locals(OemContinuationExport *export,
                                Atom **source, uint32_t count) {
    for (uint32_t index = 0u; index < export->len; index++)
        if (export->locals[index].source == source)
            return export->locals[index].count == count
                ? export->locals[index].values : NULL;
    if (!oem_reserve((void **)&export->locals, &export->cap,
                     export->len + 1u, sizeof(*export->locals)))
        return NULL;
    CettaOpenEquationCursor *cursor = export->cursor;
    Atom **values = arena_alloc(cursor->answer_arena,
                                sizeof(*values) * (count ? count : 1u));
    if (!values)
        return NULL;
    for (uint32_t index = 0u; index < count; index++) {
        values[index] = source[index]
            ? oem_export(cursor, source[index])
            : atom_var_with_spelling(cursor->answer_arena,
                cursor->program->spelling, fresh_var_id());
        if (!values[index])
            return NULL;
    }
    export->locals[export->len++] = (OemExportLocals){source, values, count};
    return values;
}

static Atom *oem_export_template(OemContinuationExport *export,
    const CettaOpenEquationProgram *program, Atom **locals,
    uint32_t template_index) {
    if (template_index >= program->template_len)
        return NULL;
    return oem_instantiate_in(export->cursor, export->cursor->answer_arena,
                              program, locals, template_index);
}

static bool oem_export_emit(OemContinuationExport *export,
    CettaOpenEquationHostMode mode, Atom *goal, Atom *destination,
    const PettaPlanNode *plan) {
    if (mode != CETTA_OPEN_EQUATION_HOST_RECOVER &&
        mode != CETTA_OPEN_EQUATION_HOST_FAIL &&
        mode != CETTA_OPEN_EQUATION_HOST_RELEASE_OWNER &&
        mode != CETTA_OPEN_EQUATION_HOST_ABORT_COLLECTION &&
        (!goal || !destination))
        return false;
    if (export->goal_count == UINT32_MAX ||
        !export->emit(export->context, mode, goal, destination, plan, 0u, 0u))
        return false;
    export->goal_count++;
    return true;
}

static bool oem_export_ready_call(OemContinuationExport *export,
    const CettaOpenEquationProgram *program, Atom **locals,
    const OemStep *step, Atom *destination) {
    CettaOpenEquationCursor *cursor = export->cursor;
    const OemRelation *relation = &program->relations[step->relation];
    if (relation->local && relation->fold_step != SYMBOL_ID_NONE &&
        step->arg_count == 2u) {
        Atom *parts[] = {
            atom_symbol_id(cursor->answer_arena, g_builtin_syms.foldl_atom),
            oem_export_template(export, program, locals,
                program->step_args[step->first_arg]),
            oem_export_template(export, program, locals,
                program->step_args[step->first_arg + 1u]),
            atom_symbol_id(cursor->answer_arena, relation->fold_step),
        };
        Atom *fold = parts[0] && parts[1] && parts[2] && parts[3]
            ? atom_expr(cursor->answer_arena, parts, 4u) : NULL;
        return fold && oem_export_emit(export,
            CETTA_OPEN_EQUATION_HOST_READY_FOLD, fold, destination, NULL);
    }
    Atom *call = atom_expr_builder_begin(cursor->answer_arena,
                                         step->arg_count + 1u);
    if (!call)
        return false;
    call->expr.elems[0] = atom_symbol_id(cursor->answer_arena,
        program->relations[step->relation].head);
    for (uint32_t index = 0u; index < step->arg_count; index++) {
        call->expr.elems[index + 1u] = oem_export_template(export, program,
            locals, program->step_args[step->first_arg + index]);
        if (!call->expr.elems[index + 1u])
            return false;
    }
    call = call->expr.elems[0]
        ? atom_expr_builder_finish(cursor->answer_arena, call) : NULL;
    return oem_export_emit(export,
        !step->checked_body
            ? CETTA_OPEN_EQUATION_HOST_SOLVE
            : relation->body_is_data
                ? CETTA_OPEN_EQUATION_HOST_READY_DATA
                : CETTA_OPEN_EQUATION_HOST_READY_CALL,
        call, destination, NULL) &&
        (!step->recovers || oem_export_emit(export,
            CETTA_OPEN_EQUATION_HOST_RECOVER, NULL, NULL, NULL));
}

static bool oem_export_grounded(OemContinuationExport *export,
    const CettaOpenEquationProgram *program, Atom **locals,
    const OemStep *step, Atom *destination) {
    CettaOpenEquationCursor *cursor = export->cursor;
    Atom *call = atom_expr_builder_begin(cursor->answer_arena,
                                         step->arg_count + 1u);
    if (!call)
        return false;
    call->expr.elems[0] = atom_symbol_id(cursor->answer_arena, step->op);
    for (uint32_t index = 0u; index < step->arg_count; index++) {
        call->expr.elems[index + 1u] = oem_export_template(export, program,
            locals, program->step_args[step->first_arg + index]);
        if (!call->expr.elems[index + 1u])
            return false;
    }
    call = call->expr.elems[0]
        ? atom_expr_builder_finish(cursor->answer_arena, call) : NULL;
    return oem_export_emit(export, CETTA_OPEN_EQUATION_HOST_GROUNDED,
                            call, destination, NULL);
}

static bool oem_export_commit_prefix(OemContinuationExport *export,
    const CettaOpenEquationProgram *program, Atom **locals, uint32_t pc,
    bool once) {
    CettaOpenEquationCursor *cursor = export->cursor;
    Atom **source = NULL;
    for (uint32_t index = 0u; index < export->len; index++)
        if (export->locals[index].values == locals) {
            source = export->locals[index].source;
            break;
        }
    if (!source)
        return false;
    uint32_t floor = cursor->frame_len;
    if (once) {
        for (uint32_t index = cursor->frame_len; index > 0u; index--) {
            const OemFrame *frame = &cursor->frames[index - 1u];
            if (frame->relation == OEM_ONCE_FRAME && frame->cont &&
                frame->cont->program == program &&
                frame->cont->locals == source && frame->cont->pc == pc) {
                floor = index - 1u;
                break;
            }
        }
    } else
        floor = oem_slot_birth(((const uintptr_t *)source)[-1]);
    if (floor > cursor->frame_len)
        return false;
    uint32_t height = export->saved_height;
    for (uint32_t index = cursor->frame_len; index > floor; index--)
        if (oem_frame_is_host(&cursor->frames[index - 1u]))
            height = cursor->frames[index - 1u].host->height;
    if (height == UINT32_MAX)
        return false;
    return oem_export_emit(export, CETTA_OPEN_EQUATION_HOST_COMMIT_PREFIX,
        atom_int(cursor->answer_arena, floor),
        atom_int(cursor->answer_arena, height), NULL);
}

/* Only the innermost collection owns a yielded answer. Its prefix is
 * already independently copied; promotion adds the delayed-payload carrier
 * without importing anything into the producer's OEM store. */
static bool oem_collection_frame_matches(const OemFrame *frame,
    const CettaOpenEquationProgram *program, Atom **source, uint32_t pc) {
    const OemCont *after = frame->cont;
    if (frame->relation != OEM_COLLECT_FRAME || !after ||
        after->program != program || after->locals != source || !after->pc)
        return false;
    const OemStep *entry = &program->steps[after->pc - 1u];
    return entry->kind == OEM_S_HOST && entry->target == OEM_HOST_COLLECT &&
           program->controls[entry->control].resume == pc;
}

static bool oem_collection_scope(CettaOpenEquationCursor *cursor,
    const CettaOpenEquationProgram *program, Atom **source, uint32_t pc,
    uint32_t saved_height, bool innermost, uint32_t *frame_out,
    uint32_t *height_out) {
    uint32_t index = cursor->frame_len;
    while (index && !oem_collection_frame_matches(
                        &cursor->frames[index - 1u], program, source, pc)) {
        if (innermost && cursor->frames[index - 1u].relation == OEM_COLLECT_FRAME)
            return false;
        index--;
    }
    if (!index)
        return false;
    uint32_t height = saved_height;
    for (uint32_t at = cursor->frame_len; at > index; at--)
        if (oem_frame_is_host(&cursor->frames[at - 1u]))
            height = cursor->frames[at - 1u].host->height;
    if (height == 0u || height == UINT32_MAX)
        return false;
    *frame_out = index;
    *height_out = height;
    return true;
}

static bool oem_export_collection_receipt(OemContinuationExport *export,
    const CettaOpenEquationProgram *program, Atom **source, uint32_t pc,
    Atom *answer, bool promote) {
    CettaOpenEquationCursor *cursor = export->cursor;
    uint32_t index = 0u, height = 0u;
    if (!oem_collection_scope(cursor, program, source, pc,
            export->saved_height, promote, &index, &height))
        return false;
    OemCollectBox *box = cursor->frames[index - 1u].collect;
    if (promote && !box->promoted) {
        OemCollectItem *head = NULL, **tail = &head;
        for (OemCollectItem *old = box->last; old; old = old->prev) {
            Atom *components = atom_expr(&cursor->collected, NULL, 0u);
            Atom *parts[] = {old->value, components};
            Atom *joint = components
                ? atom_expr(&cursor->collected, parts, 2u) : NULL;
            OemCollectItem *item = joint
                ? arena_alloc(&cursor->collected, sizeof(*item)) : NULL;
            if (!item)
                return false;
            *item = (OemCollectItem){.value = joint};
            *tail = item;
            tail = &item->prev;
        }
        box->last = head;
        box->promoted = true;
    }
    if (promote && (!box->host_height || height < box->host_height))
        box->host_height = height;
    Atom *parts[] = {atom_int(cursor->answer_arena, index),
                     atom_int(cursor->answer_arena, height)};
    Atom *receipt = parts[0] && parts[1]
        ? atom_expr(cursor->answer_arena, parts, 2u) : NULL;
    return receipt && oem_export_emit(export,
        promote ? CETTA_OPEN_EQUATION_HOST_YIELD
                : CETTA_OPEN_EQUATION_HOST_ABORT_COLLECTION,
        answer, receipt, NULL);
}

static bool oem_export_collection_receiver(OemContinuationExport *export,
    const CettaOpenEquationProgram *program, Atom **locals,
    const OemStep *step) {
    Atom **source = NULL;
    for (uint32_t index = 0u; index < export->len; index++)
        if (export->locals[index].values == locals)
            source = export->locals[index].source;
    return source && oem_export_collection_receipt(export, program, source,
        (uint32_t)(step - program->steps),
        oem_export_template(export, program, locals, step->value), true);
}

/* Lower one activation up to its return, factoring the entered caller chain
 * outside both branch slices. A selector follows the slices in the emitter
 * stream; their relative lengths let the receiver insert it before them. */
static bool oem_export_activation(OemContinuationExport *export,
    const CettaOpenEquationProgram *program, uint32_t pc, Atom **locals,
    Atom *return_destination, bool store_return) {
    CettaOpenEquationCursor *cursor = export->cursor;
    for (;;) {
        const OemStep *step = &program->steps[pc];
        switch (step->kind) {
        case OEM_S_YIELD:
            return oem_export_collection_receiver(export, program, locals, step);
        case OEM_S_RET:
            return !store_return || oem_export_emit(export,
                CETTA_OPEN_EQUATION_HOST_UNIFY,
                oem_export_template(export, program, locals, step->value),
                return_destination, NULL);
        case OEM_S_TAIL:
            return oem_export_ready_call(export, program, locals, step,
                                          return_destination);
        case OEM_S_FAIL:
            return oem_export_emit(export, CETTA_OPEN_EQUATION_HOST_FAIL,
                                     NULL, NULL, NULL);
        case OEM_S_TEST:
        case OEM_S_CASE: {
            Atom *condition = NULL;
            Atom *pattern = NULL;
            if (step->kind == OEM_S_CASE) {
                condition = oem_export_template(export, program, locals,
                                                  step->value);
                pattern = oem_export_template(export, program, locals,
                                                step->pattern);
            } else if (step->op == SYMBOL_ID_NONE) {
                condition = oem_export_template(export, program, locals,
                    program->step_args[step->first_arg]);
            } else {
                condition = atom_var_with_spelling(cursor->answer_arena,
                    cursor->program->spelling, fresh_var_id());
                if (!condition || !oem_export_grounded(export, program,
                                                       locals, step, condition))
                    return false;
            }
            uint32_t start = export->goal_count;
            if (!condition || (step->kind == OEM_S_CASE && !pattern) ||
                !oem_export_activation(export, program, pc + 1u, locals,
                                        return_destination, store_return))
                return false;
            uint32_t first_len = export->goal_count - start;
            uint32_t middle = export->goal_count;
            if (!oem_export_activation(export, program, step->target, locals,
                                        return_destination, store_return))
                return false;
            uint32_t second_len = export->goal_count - middle;
            if (export->goal_count == UINT32_MAX ||
                !export->emit(export->context,
                    step->kind == OEM_S_CASE
                        ? CETTA_OPEN_EQUATION_HOST_READY_CASE
                        : CETTA_OPEN_EQUATION_HOST_READY_IF,
                    condition, pattern, NULL, first_len, second_len))
                return false;
            export->goal_count++;
            return true;
        }
        case OEM_S_ENTRY:
        case OEM_S_BUILTIN_ENTRY:
        case OEM_S_BUILTIN_PRIM:
        case OEM_S_BUILTIN_TEST:
        case OEM_S_BUILTIN_FAIL:
        case OEM_S_BUILTIN_HOST:
        case OEM_S_BUILTIN_BIND:
        case OEM_S_BUILTIN_CALL: {
            const OemStep *tail = oem_entry_tail(program, step);
            if (!oem_export_emit(export, CETTA_OPEN_EQUATION_HOST_SOLVE,
                oem_export_template(export, program, locals, step->value),
                tail ? return_destination
                     : oem_export_template(export, program, locals, step->pattern),
                program->host_plans[step->relation]))
                return false;
            if (tail)
                return !tail->recovers || oem_export_emit(export,
                    CETTA_OPEN_EQUATION_HOST_RECOVER, NULL, NULL, NULL);
            pc = step->target;
            break;
        }
        case OEM_S_CALL:
            if (!oem_export_ready_call(export, program, locals, step,
                oem_export_template(export, program, locals, step->pattern)))
                return false;
            pc++;
            break;
        case OEM_S_PRIM:
            if (!oem_export_grounded(export, program, locals, step,
                oem_export_template(export, program, locals, step->pattern)))
                return false;
            pc++;
            break;
        case OEM_S_CUT:
            if (!oem_export_commit_prefix(export, program, locals, pc, true))
                return false;
            pc = step->target;
            break;
        case OEM_S_EQUATION_CUT:
            if (!oem_export_emit(export, CETTA_OPEN_EQUATION_HOST_UNIFY,
                oem_export_template(export, program, locals, step->pattern),
                petta_semantics_boolean_value(cursor->answer_arena, true),
                NULL) ||
                !oem_export_commit_prefix(export, program, locals, pc, false))
                return false;
            pc++;
            break;
        case OEM_S_BIND:
            if (!oem_export_emit(export, CETTA_OPEN_EQUATION_HOST_UNIFY,
                oem_export_template(export, program, locals, step->value),
                oem_export_template(export, program, locals, step->pattern),
                NULL))
                return false;
            pc++;
            break;
        case OEM_S_MATCH: {
            Atom *parts[] = {
                oem_export_template(export, program, locals, step->value),
                oem_export_template(export, program, locals, step->pattern),
            };
            Atom *operands = parts[0] && parts[1]
                ? atom_expr(cursor->answer_arena, parts, 2u) : NULL;
            if (!operands || !oem_export_emit(export,
                    step->match_row
                        ? CETTA_OPEN_EQUATION_HOST_READY_MATCH_ROW
                        : CETTA_OPEN_EQUATION_HOST_READY_MATCH,
                    operands, petta_semantics_success_value(
                                  cursor->answer_arena), NULL))
                return false;
            pc++;
            break;
        }
        case OEM_S_HOST: {
            bool type_guard = step->target == OEM_HOST_TYPE_ACCEPT ||
                              step->target == OEM_HOST_TYPE_MATCH;
            const PettaPlanNode *plan = type_guard
                ? NULL : program->host_plans[step->relation];
            CettaOpenEquationHostMode mode =
                step->target == OEM_HOST_COLLECT ||
                step->target == OEM_HOST_ALTERNATIVES ||
                step->target == OEM_HOST_ONCE
                    ? CETTA_OPEN_EQUATION_HOST_SOLVE
                    : oem_host_step_mode(step, plan);
            if (!oem_export_emit(export, mode,
                oem_export_template(export, program, locals, step->value),
                oem_export_template(export, program, locals, step->pattern),
                plan))
                return false;
            pc++;
            break;
        }
        default:
            return false;
        }
    }
}

static bool oem_export_entered_chain(OemContinuationExport *export,
                                      const OemCont *entry) {
    CettaOpenEquationCursor *cursor = export->cursor;
    const CettaOpenEquationProgram *program = entry ? entry->program : NULL;
    uint32_t pc = entry ? entry->pc : 0u;
    Atom **locals = entry
        ? oem_export_locals(export, entry->locals, entry->local_count) : NULL;
    const OemCont *cont = entry ? entry->parent : NULL;
    bool ok = !entry || locals;
    while (ok && program) {
        bool returns, yields;
        ok = oem_export_activation_supported(program, pc, &returns, &yields);
        if (!ok)
            break;
        Atom **caller = cont ? oem_export_locals(export, cont->locals,
                                                  cont->local_count) : NULL;
        Atom *destination = cont && caller
            ? oem_export_template(export, cont->program, caller, cont->pattern)
            : !cont ? oem_export(cursor, cursor->destination) : NULL;
        bool store_return = !cont || (cont->store &&
            !cont->locals[cont->program->templates[cont->pattern].slot]);
        ok = destination && oem_export_activation(export, program, pc,
                                                   locals, destination,
                                                   store_return);
        if (ok && yields && !returns) {
            /* Preserve outer handlers in lexical order without executing
             * their callers or promoting outer collection buffers. */
            for (const OemCont *outer = cont; ok && outer;
                 outer = outer->parent) {
                if (outer->recovers)
                    ok = oem_export_emit(export,
                        CETTA_OPEN_EQUATION_HOST_RECOVER, NULL, NULL, NULL);
                if (ok && outer->pc < outer->program->step_len &&
                    outer->program->steps[outer->pc].kind == OEM_S_YIELD)
                    ok = oem_export_collection_receipt(export,
                        outer->program, outer->locals, outer->pc, NULL, false);
            }
            return ok;
        }
        if (ok && cont && cont->recovers)
            ok = oem_export_emit(export, CETTA_OPEN_EQUATION_HOST_RECOVER,
                                   NULL, NULL, NULL);
        program = cont ? cont->program : NULL;
        locals = caller;
        pc = cont ? cont->pc : 0u;
        cont = cont ? cont->parent : NULL;
    }
    return ok && oem_export_emit(export,
        CETTA_OPEN_EQUATION_HOST_RELEASE_OWNER, NULL, NULL, NULL);
}

bool cetta_open_equation_cursor_export_host_receivers(
    CettaOpenEquationCursor *cursor,
    bool (*emit)(void *, CettaOpenEquationHostMode, uint32_t, uint32_t),
    void *context) {
    const OemFrame *frame = cursor && cursor->frame_len
        ? &cursor->frames[cursor->frame_len - 1u] : NULL;
    if (!frame || !oem_frame_is_host(frame) || !emit)
        return false;
    bool ok = !cursor->host_own_recovers || emit(context,
        CETTA_OPEN_EQUATION_HOST_RECOVER, 0u, 0u);
    for (const OemCont *cont = oem_receiver_cont(frame->cont);
         ok && cont; cont = cont->receiver_parent) {
        if (cont->recovers)
            ok = emit(context, CETTA_OPEN_EQUATION_HOST_RECOVER, 0u, 0u);
        if (ok && cont->pc < cont->program->step_len &&
            cont->program->steps[cont->pc].kind == OEM_S_YIELD) {
            uint32_t floor = 0u, height = 0u;
            ok = oem_collection_scope(cursor, cont->program, cont->locals,
                    cont->pc, UINT32_MAX, false, &floor, &height) &&
                 emit(context, CETTA_OPEN_EQUATION_HOST_ABORT_COLLECTION,
                      floor, height);
        }
    }
    return ok;
}

bool cetta_open_equation_cursor_export_host_continuation(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars,
    Atom *const *host_vars, uint32_t host_count,
    bool (*emit)(void *, CettaOpenEquationHostMode, Atom *, Atom *,
                 const PettaPlanNode *, uint32_t, uint32_t),
    void *context, Atom ***query_values_out) {
    if (query_values_out)
        *query_values_out = NULL;
    if (!emit || !query_values_out ||
        !cetta_open_equation_cursor_host_continuation_supported(cursor))
        return false;
    const OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
    if (frame->host->count != host_count || (host_count && !host_vars))
        return false;
    /* A preceding host answer may have run native caller code. Restore
     * only that native branch before using this answer's canonical aliases. */
    oem_restore(cursor, frame);
    OemContinuationExport export = {.cursor = cursor, .emit = emit,
                                    .context = context,
                                    .saved_height = UINT32_MAX};
    oem_forget_answer_vars(cursor);
    cursor->query_vars = query_vars;
    bool ok = true;
    /* Use the live OPEN_RESUME names: the original exported pointers may
     * have moved during the host call or belonged to its preceding answer. */
    for (uint32_t index = 0u; ok && index < frame->host->count; index++) {
        uint32_t cell = frame->host->cells[index];
        ok = oem_reserve_zeroed((void **)&cursor->answer_fresh,
                 &cursor->answer_fresh_cap, cell + 1u,
                 sizeof(*cursor->answer_fresh)) &&
             oem_reserve((void **)&cursor->fresh_touched,
                 &cursor->fresh_touched_cap, cursor->fresh_touched_len + 1u,
                 sizeof(*cursor->fresh_touched));
        if (ok) {
            cursor->answer_fresh[cell] = host_vars[index];
            cursor->fresh_touched[cursor->fresh_touched_len++] = cell;
        }
    }
    for (uint32_t index = 0u; ok && index < cursor->query_var_count; index++) {
        cursor->query_values[index] = oem_export(cursor,
                                                cursor->query_cells[index]);
        ok = cursor->query_values[index] != NULL;
    }
    ok = ok && oem_export_entered_chain(&export, frame->cont);
    free(export.locals);
    cursor->query_vars = NULL;
    oem_forget_answer_vars(cursor);
    if (ok)
        *query_values_out = cursor->query_values;
    return ok;
}

CettaOpenEquationProgram *cetta_open_equation_cursor_program_at(
    const CettaOpenEquationCursor *cursor, uint32_t index) {
    if (!cursor)
        return NULL;
    if (index == 0u)
        return cursor->program;
    if (index <= cursor->version_len)
        return cursor->versions[index - 1u];
    index -= cursor->version_len + 1u;
    for (uint32_t entry = 0u; entry < cursor->callable_len; entry++) {
        if (cursor->callables[entry].program) {
            if (index == 0u)
                return cursor->callables[entry].program;
            index--;
        }
    }
    return NULL;
}

void cetta_open_equation_cursor_host_height(CettaOpenEquationCursor *cursor,
                                            uint32_t height) {
    OemFrame *frame = cursor && cursor->frame_len
        ? &cursor->frames[cursor->frame_len - 1u] : NULL;
    if (frame && oem_frame_is_host(frame))
        frame->host->height = height;
}

bool cetta_open_equation_cursor_commit_prefix(CettaOpenEquationCursor *cursor,
    uint32_t floor, uint32_t saved_height, uint32_t *height_out) {
    if (!cursor || !height_out || floor > cursor->frame_len ||
        saved_height == 0u || saved_height == UINT32_MAX)
        return false;
    uint32_t height = saved_height;
    for (uint32_t index = cursor->frame_len; index > floor; index--) {
        const OemFrame *frame = &cursor->frames[index - 1u];
        if (oem_frame_is_host(frame)) {
            if (frame->host->height == 0u ||
                frame->host->height == UINT32_MAX)
                return false;
            if (frame->host->height < height)
                height = frame->host->height;
        }
    }
    oem_truncate_frames(cursor, floor);
    *height_out = height;
    return true;
}

bool cetta_open_equation_cursor_collect_answer(
    CettaOpenEquationCursor *cursor, uint32_t index, uint32_t saved_height,
    Atom *answer, Atom *components, bool *cut_out) {
    if (cut_out)
        *cut_out = false;
    if (!cursor || !cut_out || !answer || !components ||
        components->kind != ATOM_EXPR || index == 0u ||
        index > cursor->frame_len ||
        cursor->frames[index - 1u].relation != OEM_COLLECT_FRAME)
        return false;
    for (uint32_t at = index; at < cursor->frame_len; at++)
        if (cursor->frames[at].relation == OEM_COLLECT_FRAME)
            return false;
    OemCollectBox *box = cursor->frames[index - 1u].collect;
    if (!box->promoted || box->count == UINT32_MAX)
        return false;
    Atom *parts[] = {answer, components};
    Atom *joint = atom_expr(&cursor->collected, parts, 2u);
    joint = joint ? atom_deep_copy(&cursor->collected, joint) : NULL;
    OemCollectItem *item = joint
        ? arena_alloc(&cursor->collected, sizeof(*item)) : NULL;
    if (!item)
        return false;
    *item = (OemCollectItem){.value = joint, .prev = box->last};
    box->last = item;
    box->count++;
    if (box->limit && box->count >= box->limit) {
        uint32_t height;
        if (!cetta_open_equation_cursor_commit_prefix(cursor, index,
                                                       saved_height, &height))
            return false;
        cursor->cut_height = height;
        *cut_out = true;
    }
    return true;
}

bool cetta_open_equation_cursor_abort_collection(
    CettaOpenEquationCursor *cursor, uint32_t index, uint32_t saved_height,
    uint32_t *height_out) {
    if (!cursor || !height_out || index == 0u || index > cursor->frame_len ||
        cursor->frames[index - 1u].relation != OEM_COLLECT_FRAME)
        return false;
    ArenaMark mark = cursor->frames[index - 1u].collect->mark;
    if (!cetta_open_equation_cursor_commit_prefix(cursor, index - 1u,
                                                  saved_height, height_out))
        return false;
    arena_reset(&cursor->collected, mark);
    return true;
}

bool cetta_open_equation_cursor_export_collection(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars,
    bool (*emit)(void *, CettaOpenEquationHostMode, Atom *, Atom *,
                 const PettaPlanNode *, uint32_t, uint32_t),
    void *context, Atom **components_out, Atom ***query_values_out) {
    if (components_out)
        *components_out = NULL;
    if (query_values_out)
        *query_values_out = NULL;
    if (!cursor || !emit || !components_out || !query_values_out ||
        !cursor->completed_collection || !cursor->collection_after)
        return false;
    OemCollectBox *box = cursor->completed_collection;
    const OemCont *after = cursor->collection_after;
    ArenaMark mark = box->mark;
    OemContinuationExport export = {.cursor = cursor, .emit = emit,
                                    .context = context,
                                    .saved_height = box->host_height};
    oem_forget_answer_vars(cursor);
    cursor->query_vars = query_vars;
    bool ok = true;
    for (uint32_t index = 0u; ok && index < cursor->query_var_count; index++) {
        cursor->query_values[index] = oem_export(cursor,
                                                cursor->query_cells[index]);
        ok = cursor->query_values[index] != NULL;
    }
    Atom *list = ok
        ? atom_expr_builder_begin(cursor->answer_arena, box->count) : NULL;
    Atom **components = NULL;
    uint32_t component_len = 0u, component_cap = 0u;
    uint32_t remaining = box->count;
    for (OemCollectItem *item = box->last; list && item; item = item->prev) {
        /* One graph copy keeps each answer's aliases with its components;
         * capture already freshened it separately from the producer. */
        Atom *joint = atom_deep_copy(cursor->answer_arena, item->value);
        if (!remaining || !joint || joint->kind != ATOM_EXPR ||
            joint->expr.len != 2u) {
            list = NULL;
            break;
        }
        list->expr.elems[--remaining] = joint;
    }
    for (uint32_t index = 0u; list && !remaining && index < box->count;
         index++) {
        Atom *joint = list->expr.elems[index];
        Atom *carried = joint->expr.elems[1];
        if (!carried || carried->kind != ATOM_EXPR ||
            carried->expr.len > UINT32_MAX - component_len ||
            !oem_reserve((void **)&components, &component_cap,
                component_len + (uint32_t)carried->expr.len,
                sizeof(*components))) {
            list = NULL;
            break;
        }
        list->expr.elems[index] = joint->expr.elems[0];
        for (CettaExprIndex at = 0u; at < carried->expr.len; at++)
            components[component_len++] = carried->expr.elems[at];
    }
    list = list && !remaining
        ? atom_expr_builder_finish(cursor->answer_arena, list) : NULL;
    Atom *payload = list ? atom_expr(cursor->answer_arena, components,
                                     component_len) : NULL;
    free(components);
    Atom **locals = payload
        ? oem_export_locals(&export, after->locals, after->local_count) : NULL;
    if (locals) {
        /* The copied prefix and components now belong to the host. Release
         * this inner buffer before the resumed chain can promote an outer
         * receiver above its mark. */
        cursor->completed_collection = NULL;
        cursor->collection_after = NULL;
        arena_reset(&cursor->collected, mark);
    }
    ok = locals && oem_export_emit(&export, CETTA_OPEN_EQUATION_HOST_UNIFY,
        list, oem_export_template(&export, after->program, locals,
                                   after->pattern), NULL) &&
        oem_export_entered_chain(&export, after);
    free(export.locals);
    cursor->query_vars = NULL;
    oem_forget_answer_vars(cursor);
    if (ok) {
        *components_out = payload;
        *query_values_out = cursor->query_values;
    }
    return ok;
}

uint32_t cetta_open_equation_cursor_cut_height(
    const CettaOpenEquationCursor *cursor) {
    return cursor ? cursor->cut_height : 0u;
}

uint32_t cetta_open_equation_cursor_raise_height(
    const CettaOpenEquationCursor *cursor) {
    return cursor ? cursor->raise_height : 0u;
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
    if (cursor->runtime.withdraws_on_error) {
        for (uint32_t index = 0u; index < host_count; index++) {
            if (atom_is_error(host_values[index])) {
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_WITHDRAWN;
                cursor->query_vars = NULL;
                return false;
            }
        }
    }
    /* Under `raises_he_errors`, a host answer that is an error raises where
     * its goal's continuation resumes, instead of filling the goal's cells. */
    Atom *raised = NULL;
    for (uint32_t index = 0u; cursor->runtime.raises_he_errors && !raised &&
         index < host_count; index++)
        if (atom_is_error(host_values[index]))
            raised = host_values[index];
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
    for (uint32_t index = 0u; unified && !raised && index < host_count;
         index++) {
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
    Atom *raised_import = raised ? oem_import_walk(cursor, &import, raised)
                                 : NULL;
    oem_import_free(&import);
    cetta_var_index_free(&host_index);
    if (raised && !raised_import) {
        cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
        cursor->query_vars = NULL;
        return false;
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
    if (unified && raised)
        cursor->pending_raise = raised_import;
    cursor->query_vars = NULL;
    return unified;
}

CettaOpenEquationStep cetta_open_equation_cursor_continue(
    CettaOpenEquationCursor *cursor, Atom *const *query_vars, uint32_t base,
    CettaCallOutcome *returned, Atom ***query_values_out) {
    return cetta_open_equation_cursor_next_above(cursor, base, query_vars,
                                                 returned, query_values_out);
}

/* Run the frames at depth `base` and above.  A host frame met at the top
 * has no answers left: its goal's choices were below the choice now
 * running. */
static inline __attribute__((always_inline)) CettaOpenEquationStep
oem_cursor_run_mode(CettaOpenEquationCursor *cursor, uint32_t base,
                    Atom **value_out, Atom ***query_values_out,
                    const bool he) {
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
            return CETTA_OPEN_EQUATION_FAULT;
        }
        if (cursor->version_len >= cursor->version_sweep_at)
            oem_sweep_versions(cursor);
        OemFrame *frame = &cursor->frames[cursor->frame_len - 1u];
        oem_restore(cursor, frame);
        OemRun run = OEM_RUN_FAILED;
        OemEntry entry = {0};
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
                if (__builtin_expect(he && cursor->pending_raise != NULL, 0)) {
                    /* A host answer that is an error raises here, in
                     * place of its goal's continuation. */
                    cursor->raised_value = cursor->pending_raise;
                    cursor->pending_raise = NULL;
                    run = oem_he_raise(cursor, resume);
                    goto run_body;
                }
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
                if (box->promoted) {
                    cursor->completed_collection = box;
                    cursor->collection_after = after;
                    return CETTA_OPEN_EQUATION_COLLECTION;
                }
                Atom *list = oem_collected_list(cursor, box);
                if (list && (he && cursor->runtime.withdraws_reducible_collections) &&
                    (he && cursor->runtime.value_reducible) &&
                    cursor->runtime.value_reducible(cursor->runtime.context,
                                                    after->program->space,
                                                    list)) {
                    cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_WITHDRAWN;
                    return CETTA_OPEN_EQUATION_HANDOFF;
                }
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
            if (frame->relation == OEM_PREPARED_FRAME) {
                const OemCont *resume = frame->cont;
                CettaCallOutcome returned = {0};
                run = oem_pull_callable_answer(cursor, cursor->frame_len - 1u, &returned);
                if (run == OEM_RUN_DECLINED)
                    run = oem_service_fault(cursor);
                if (run == OEM_RUN_CALLED) {
                    Atom *value = NULL;
                    run = oem_prepared_call_result(cursor, returned, &value, value_out);
                    if (run == OEM_RUN_CALLED)
                        run = oem_direct_relation_value(cursor, value, resume, &entry, value_out);
                }
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
            Atom *candidate = NULL;
            const TermUniverse *stored_universe = NULL;
            AtomId stored = CETTA_ATOM_ID_NONE;
            if (frame->lazy_rows) {
                /* The row read ahead, and the one after it read now: a
                 * frame with none left goes before its last row runs. */
                stored_universe = frame->lazy->universe;
                stored = frame->lazy->next;
                frame->lazy->next = oem_lazy_rows_fetch(cursor, frame->lazy);
            } else {
                if (frame->next >= frame->rows->len) {
                    oem_pop_frame(cursor);
                    continue;
                }
                candidate = frame->rows->rows[frame->next++];
            }
            Atom *pattern = frame->args ? frame->args[0] : NULL;
            const OemCont *resume = frame->cont;
            if (frame->lazy_rows ? frame->lazy->next == CETTA_ATOM_ID_NONE
                                 : frame->next >= frame->rows->len)
                oem_pop_frame(cursor);
            if (stored_universe)
                run = pattern
                    ? oem_try_stored(cursor, pattern, stored_universe, stored)
                    : oem_try_stored_in_place(cursor, resume,
                                              stored_universe, stored);
            else
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
            bool value_rule = he &&
                !relation->local;
            if (frame->next >= relation->equation_count) {
                if (value_rule && !frame->head_matched) {
                    run = oem_frame_unmatched_value(cursor, relation, &entry,
                                                    value_out);
                    goto run_body;
                }
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
                    if (value_rule && !frame->head_matched) {
                        run = oem_frame_unmatched_value(cursor, relation,
                                                        &entry, value_out);
                        goto run_body;
                    }
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
            bool prior_matched = frame->head_matched;
            uint32_t frame_index = cursor->frame_len - 1u;
            if (!own_frame)
                oem_pop_frame(cursor);
            cursor->params_matched = false;
            run = oem_activate(cursor, version, equation, args,
                               relation->arity, cont, own_frame, &entry);
            bool params_matched = cursor->params_matched;
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
            if (value_rule) {
                if (params_matched) {
                    if (own_frame && frame_index < cursor->frame_len)
                        cursor->frames[frame_index].head_matched = true;
                } else if (!own_frame && !prior_matched &&
                           run == OEM_RUN_FAILED) {
                    run = oem_unmatched_value(cursor, relation, args, cont,
                                              &entry, value_out);
                }
            }
        }
        /* A head or a row that matched runs its body here, the one place
         * the body runner is called from. */
    run_body:
        if (run == OEM_RUN_CALLED)
            run = he
                ? oem_run_body_he(cursor, entry.program, entry.pc,
                                  entry.locals, entry.local_count, entry.cont,
                                  entry.depth, value_out)
                : oem_run_body_plain(cursor, entry.program, entry.pc,
                                     entry.locals, entry.local_count,
                                     entry.cont, entry.depth, value_out);
        /* Failure and a pushed call continue; every other outcome is one
         * test away, since they follow those two in the enumeration. */
        if (run < OEM_RUN_ANSWER)
            continue;
        if (run == OEM_RUN_DECLINED ||
            (run == OEM_RUN_HOST &&
             (!cursor->host_goal || !cursor->host_destination))) {
            oem_service_fault(cursor);
            return CETTA_OPEN_EQUATION_FAULT;
        }
        if (run == OEM_RUN_HOST)
            return CETTA_OPEN_EQUATION_HOST;
        if (run == OEM_RUN_CUT)
            return CETTA_OPEN_EQUATION_CUT;
        if (run == OEM_RUN_FAULT)
            return CETTA_OPEN_EQUATION_FAULT;
        if (run == OEM_RUN_HANDOFF) {
            if (cursor->handoff == CETTA_OPEN_EQUATION_HANDOFF_NONE)
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            return CETTA_OPEN_EQUATION_HANDOFF;
        }
        if (!oem_settle(cursor, base)) {
            cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
            return CETTA_OPEN_EQUATION_HANDOFF;
        }
        if (run == OEM_RUN_RAISE) {
            if ((he && cursor->runtime.withdraws_on_error)) {
                cursor->handoff = CETTA_OPEN_EQUATION_HANDOFF_WITHDRAWN;
                return CETTA_OPEN_EQUATION_HANDOFF;
            }
            if (!(he && cursor->runtime.raises_he_errors))
                return CETTA_OPEN_EQUATION_RAISE;
            /* The error is the call's answer (oem_he_error_answer). */
            *value_out = cursor->raised_value;
        }
        if (query_values_out)
            *query_values_out = cursor->query_values;
        return CETTA_OPEN_EQUATION_ANSWER;
    }
}

static __attribute__((noinline)) CettaOpenEquationStep oem_cursor_run_plain(
    CettaOpenEquationCursor *cursor, uint32_t base, Atom **value_out,
    Atom ***query_values_out) {
    return oem_cursor_run_mode(cursor, base, value_out, query_values_out,
                               false);
}

static __attribute__((noinline)) CettaOpenEquationStep oem_cursor_run_he(
    CettaOpenEquationCursor *cursor, uint32_t base, Atom **value_out,
    Atom ***query_values_out) {
    return oem_cursor_run_mode(cursor, base, value_out, query_values_out,
                               true);
}

/* A cursor under HE's rules (a call no head takes is its own value, which
 * every HE cursor has) runs the copy of the machine specialized to them;
 * any other runs a copy without their tests. */
static CettaOpenEquationStep oem_cursor_run(
    CettaOpenEquationCursor *cursor, uint32_t base, Atom **value_out,
    Atom ***query_values_out) {
    return cursor->runtime.unmatched_call_is_value
        ? oem_cursor_run_he(cursor, base, value_out, query_values_out)
        : oem_cursor_run_plain(cursor, base, value_out, query_values_out);
}

CettaOpenEquationHandoff cetta_open_equation_cursor_handoff(
    const CettaOpenEquationCursor *cursor) {
    return cursor ? cursor->handoff : CETTA_OPEN_EQUATION_HANDOFF_CAPACITY;
}

void cetta_open_equation_cursor_set_type_guard(
    CettaOpenEquationCursor *cursor, void *context,
    CettaOpenTypeResult (*guard)(void *, Space *, Arena *, Atom *, Atom *,
                                bool, Atom ***, uint32_t *)) {
    if (cursor) {
        cursor->runtime.type_context = context;
        cursor->runtime.type_guard = guard;
    }
}

void cetta_open_equation_cursor_set_collection_raise_policy(
    CettaOpenEquationCursor *cursor, bool abort_collections) {
    if (cursor)
        cursor->runtime.raises_abort_collections = abort_collections;
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
    if (cursor) {
        cursor->authority_epoch++;
        cursor->checked_program = NULL;
    }
}

void cetta_open_equation_cursor_suspend(CettaOpenEquationCursor *cursor) {
    if (!cursor)
        return;
    arena_release_spare(&cursor->region, 0u);
    if (cursor->old_ready)
        arena_release_spare(&cursor->old, 0u);
    arena_release_spare(&cursor->names, 0u);
    if (cursor->scratch_ready)
        arena_release_spare(&cursor->scratch, 0u);
    if (cursor->collected_ready)
        arena_release_spare(&cursor->collected, 0u);
    oem_shrink((void **)&cursor->cells, &cursor->cell_cap, cursor->cell_len,
               sizeof(*cursor->cells));
    oem_shrink((void **)&cursor->trail, &cursor->trail_cap, cursor->trail_len,
               sizeof(*cursor->trail));
    oem_shrink((void **)&cursor->stores, &cursor->store_cap,
               cursor->store_len, sizeof(*cursor->stores));
    oem_shrink((void **)&cursor->frames, &cursor->frame_cap,
               cursor->frame_len, sizeof(*cursor->frames));
}

void cetta_open_equation_cursor_close(CettaOpenEquationCursor *cursor) {
    if (!cursor)
        return;
    if (cursor->ledger_counted) {
        cetta_runtime_stats_gauge_add(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_LIVE,
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_LIVE_PEAK, -1);
        cetta_runtime_stats_gauge_add(
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_VERSIONS_LIVE,
            CETTA_RUNTIME_COUNTER_OPEN_EQUATION_CURSOR_VERSIONS_LIVE_PEAK,
            -(int64_t)cursor->version_len);
    }
    if (cursor->external_frames)
        oem_release_external_frames(cursor, 0u);
    if (cursor->runtime.release_pure_callable) {
        for (uint32_t index = 0u;
             index < cursor->pure_callable_len; index++) {
            if (cursor->pure_callables[index].prepared)
                cursor->runtime.release_pure_callable(
                    cursor->runtime.context,
                    cursor->pure_callables[index].prepared);
        }
    }
    free(cursor->pure_callables);
    for (uint32_t index = 0u; index < cursor->callable_len; index++)
        cetta_open_equation_program_release(cursor->callables[index].program);
    free(cursor->callables);
    arena_free(&cursor->region);
    if (cursor->old_ready)
        arena_free(&cursor->old);
    arena_free(&cursor->names);
    if (cursor->collected_ready)
        arena_free(&cursor->collected);
    if (cursor->scratch_ready)
        arena_free(&cursor->scratch);
    free(cursor->cell_names);
    petta_contract_memo_free(cursor->contract_memo);
    if (!cetta_frame_identity_is_none(cursor->epoch))
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
    free(cursor->occurs_seen.buckets);
    free(cursor->occurs_seen.dense);
    free(cursor->ground_facts);
    free(cursor->occurs_close);
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

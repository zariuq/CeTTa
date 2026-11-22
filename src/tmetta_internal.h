#ifndef CETTA_TMETTA_INTERNAL_H
#define CETTA_TMETTA_INTERNAL_H

#include "tmetta.h"
#include "abt.h"
#include "answer_storage.h"
#include "binding/frame_schema.h"
#include "call_outcome.h"
#include "library.h"
#include "native_handle.h"
#include "native_memory.h"
#include "u64_index.h"
#include <stdatomic.h>

typedef enum {
    TM_DATA, TM_CALL, TM_HOLD, TM_RUN, TM_LET, TM_CHOICE, TM_FAIL,
    TM_COLLECT, TM_REFLECT, TM_SERIALIZE, TM_COUNT, TM_EMPTY, TM_MATCH,
    TM_IF, TM_CHECK, TM_UNIFY, TM_EFFECT, TM_FOLD, TM_DELAY, TM_FORCE, TM_NEW
} TmOpcode;

typedef enum { TM_ORDER_CANONICAL, TM_ORDER_ANY, TM_ORDER_INVALID } TmOrder;
typedef enum { TM_DEMAND_DEFAULT, TM_DEMAND_HELD, TM_DEMAND_VALUE } TmDemand;

static inline bool tm_capacity_failed(void) {
    return cetta_native_memory_failed && cetta_native_memory_failed();
}

typedef struct TmCode TmCode;
struct TmCode {
    TmOpcode op;
    bool compiled;
    bool lexical;
    bool superpose;
    /* Authored operand positions, starting at one. Zero means no binder. */
    uint8_t binding;
    TmOrder order;
    Atom *source;
    TmCode *head;
    TmCode **children;
    uint32_t arity;
    BindingsFrameSchema *schema;
    VarId *bound_names;
    Atom **quote_slots;
    TmCode *allocated_next;
};

typedef struct {
    Atom *type;
    BindingsFrameSchema *schema;
    TmDemand *parameters;
    uint32_t count;
} TmDeclaration;

typedef struct {
    Atom *left;
    TmCode *body;
    BindingsFrameSchema *schema;
    bool scalar_pattern;
} TmEquation;

/* Leaves whose whole value is available to the first-order matcher.
 * Opaque resources and structured terms use the general path. */
static inline bool tm_scalar_leaf(const Atom *atom) {
    if (atom->kind == ATOM_SYMBOL) return true;
    if (atom->kind != ATOM_GROUNDED) return false;
    return atom->ground.gkind == GV_INT || atom->ground.gkind == GV_BOOL ||
        atom->ground.gkind == GV_FLOAT || atom->ground.gkind == GV_STRING;
}

typedef struct {
    SymbolId head;
    size_t ordinal;
} TmEquationKey;

typedef struct TmProgram {
    SpaceProgramToken token;
    Arena arena;
    TmCode *allocated;
    AbtSignature binders;
    Atom *error;
    TmEquation *equations;
    TmEquationKey *by_head;
    size_t len, cap;
    CettaU64Index declaration_index;
    TmDeclaration *declarations;
    uint32_t declaration_count;
    _Atomic size_t refs;
} TmProgram;

struct CettaTmettaRuntime {
    TmProgram *cached_program;
    pthread_mutex_t mutex;
    /* The capture world: identities that stored rows keep, in their own
     * namespace, and the knowledge committed about them. */
    CettaFrameIdentityScope capture_scope;
    CettaFrameIdentity capture_epoch;
    Arena knowledge;
    Bindings captures;
    /* The complete schema of an opened frame of k slots, indexed by k. */
    BindingsFrameSchema **slot_schemas;
    uint32_t slot_schema_cap;
};

typedef struct TmScope {
    BindingsFrameSchema *schema;
    const VarId *keys;
    Atom **slots;
    CettaFrameIdentity identity;
    struct TmCollector *collector;
    struct TmScope *parent;
    struct TmScope *allocated_next;
} TmScope;

typedef struct TmAnswers {
    AnswerBank bank;
    CettaAnswerStorage *storage;
    CettaAnswerStorageAccount *account;
    CettaFrameIdentity *local_frames;
    size_t local_frame_count;
    struct TmAnswers *backing;
    _Atomic size_t references;
    bool ordered;
} TmAnswers;

bool tm_named(const Atom *atom, const char *name);
TmDemand tm_type_demand(const Atom *type);
const TmDeclaration *tm_declaration(const TmProgram *program, const Atom *head);
SymbolId tm_equation_head(const Atom *term);
void tm_equation_range(const TmProgram *program, SymbolId head,
    size_t *begin, size_t *end);
TmCode *tm_compile(TmProgram *program, Atom *source);
TmProgram *tm_program(CettaLibraryContext *context, Space *space);
void tm_program_retain(TmProgram *program);
void tm_program_release(TmProgram *program);
struct CettaTmettaRuntime *tm_runtime(CettaLibraryContext *context);
BindingsFrameSchema *tm_schema(Atom *source);
void tm_program_dispose(TmProgram *program);
TmAnswers *tm_answers_get(CettaLibraryContext *context, Atom *value);
Atom *tm_answers_publish(CettaLibraryContext *context, Arena *output,
    const AnswerBank *bank, const CettaIndex *order, bool ordered,
    const CettaFrameIdentity *local_frames, size_t local_frame_count);
int tm_answers_canonical_order(CettaLibraryContext *context, Arena *arena,
    const TmAnswers *answers, CettaIndex *order);
int tm_answer_open(Arena *arena, const AnswerRecord *record, Bindings *caller,
    Atom **value);
bool tm_is_sequence_operation(SymbolId head, uint32_t arity);
bool tm_answers_project(CettaLibraryContext *context, Arena *arena, SymbolId head,
    Atom *const *arguments, uint32_t arity, Atom *source, Bindings *caller,
    CettaCallOutcome *outcome);

#endif

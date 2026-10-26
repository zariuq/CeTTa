#include "native_space_agenda.h"
#include "binding/frame_identity.h"
#include "mm2_lower.h"
#include "native_cost.h"
#include "shared_transition.h"
#include "space_match_backend.h"
#include "symbol.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef enum { INPUT_READ, INPUT_TAKE, INPUT_ALL, INPUT_ABSENT } InputKind;
typedef struct { InputKind kind; Atom *pattern; Atom *collection; } AgendaInput;
typedef struct {
    Bindings environment;
    bool environment_ready;
    SpaceOccurrenceCursor cursor;
    bool cursor_ready;
    bool aggregate_done;
    bool exhausted;
    Atom **aggregate;
    size_t aggregate_length;
    size_t aggregate_capacity;
    Atom *aggregate_value;
    CettaIndex witness;
    Atom *witness_atom;
    Atom *pending_row;
    CettaIndex pending_index;
} JoinLevel;
typedef struct {
    Atom *outputs;
    Atom *witnesses;
    Atom *dependencies;
} JoinedRow;
typedef struct {
    SpaceReadToken read;
    CettaIndex index;
    Atom *exec;
} WaitingExec;
typedef struct {CettaIndex index;Atom *exec;void *advice;} ExecCandidate;
struct CettaSpaceAgendaMatch {
    Arena arena;
    HashConsTable hashcons;
    Arena *execution_arena;
    Bindings captured, result;
    CettaMatchCursor *cursor;
    Atom *pattern, *row;
    CettaFrameIdentity epoch;
    CettaMatchCursorStep decision;
    bool isolated, prepared, result_ready;
};
typedef enum {
    AGENDA_SELECT, AGENDA_CAPTURE, AGENDA_MATCH,
    AGENDA_VALIDATE_CAPTURE, AGENDA_VALIDATE_MATCH,
    AGENDA_BUILD, AGENDA_SINK, AGENDA_COMMIT, AGENDA_WAIT, AGENDA_RANK,
    AGENDA_WORLD_CAPTURE, AGENDA_WORLD_REPORT, AGENDA_DONE,
    AGENDA_ACTIVATE,
} AgendaPhase;

struct CettaSpaceAgenda {
    /* Ownership links are not a scheduling queue. Every live node has one
     * ready occurrence in the enclosing evaluator's continuation hub. */
    CettaSpaceAgenda *root, *owned_next, *pending_child;
    SpaceReadToken origin_read;
    Space world;
    bool world_ready;
    bool yielded_child;
    bool uninterpreted_choice;
    uint64_t world_id, parent_world_id, next_world_id;
    Atom *trace;
    Atom **reported_atoms;
    size_t reported_count, reported_capacity;
    void *chosen_advice;
    Space *space;
    uint64_t space_identity;
    Arena *owner;
    Arena *owned_arena;
    Bindings captured;
    CettaSpaceAgendaContract contract;
    AgendaPhase phase;
    SymbolId exec, comma, input, output, btm, read, take, all, absent, plus, minus;
    Atom *wildcard;
    SpaceOccurrenceCursor selection;
    bool selection_ready;
    Atom *selected;
    Atom *last_selected;
    const char *selected_order;
    CettaIndex selected_index;
    SpaceReadToken selected_read;
    uint8_t *selected_key;
    size_t selected_key_length;
    Space snapshot;
    Space current;
    Space replacement;
    /* Qualified native reads use the held source until its token changes.
     * Discovery refuses a changed source; ordinary matching restarts it.
     * Publication still builds one private replacement before committing. */
    bool borrowed_capture;
    bool enumerate_matches, selected_match;
    bool snapshot_ready, current_ready, replacement_ready;
    bool snapshot_exec_appended,current_exec_appended;
    SpaceReadToken capture_read;
    CettaIndex capture_index;
    CettaIndex capture_length;
    AgendaInput *inputs;
    size_t input_count;
    Atom *sinks;
    JoinLevel *levels;
    size_t depth;
    bool join_ready;
    JoinedRow *rows, *validation;
    size_t row_count, row_capacity, validation_count, validation_capacity;
    CettaFrameIdentity *match_epochs[2];
    size_t match_epoch_count[2], match_epoch_capacity[2];
    bool validating;
    size_t build_index, sink_row, sink_index;
    uint64_t commits, retries;
    Atom *receipt;
    const char *pending;
    WaitingExec *waiting;
    size_t waiting_count, waiting_capacity;
    ExecCandidate *candidates;
    size_t candidate_count,candidate_capacity,ranking,best;
    SpaceReadToken selection_read;
    CettaSelectionAutomaton schedule;
    CettaSpaceAgendaAdvise advise;
    CettaSpaceAgendaBefore before;
    void *advice_context;
    bool oldest;
    CettaSpaceAgendaMatch *matching;
};

struct CettaSpaceAgendaSelection {
    CettaSpaceAgenda *agenda;
    CettaSpaceAgendaChoiceKind kind;
    bool started, completed, stale, leased;
    Atom *receipt;
    const char *pending;
};

/* Observation changes select real provider operations once at scope entry.
 * Unrequested steps and matching do not poll or invoke an observer. */
typedef struct {
    void (*destroy)(CettaSpaceAgenda *);
    CettaSpaceAgendaMatch *(*match_capture)(CettaSpaceAgenda *);
    bool (*match_run)(CettaSpaceAgendaMatch *, uint64_t);
    CettaSpaceAgendaStep (*step)(CettaSpaceAgenda *);
    CettaSpaceAgendaStep (*prepare_source)(CettaSpaceAgenda *, SpaceReadToken, CettaIndex);
    CettaSpaceAgendaStep (*revalidate_choice)(CettaSpaceAgenda *);
} AgendaOperations;
static const AgendaOperations agenda_plain_operations;
static const AgendaOperations agenda_recorded_operations;
static const AgendaOperations agenda_selected_plain_operations;
static const AgendaOperations agenda_selected_recorded_operations;
static const AgendaOperations agenda_controlled_plain_operations;
static const AgendaOperations agenda_controlled_recorded_operations;
static const AgendaOperations agenda_selected_controlled_plain_operations;
static const AgendaOperations agenda_selected_controlled_recorded_operations;
static _Thread_local const AgendaOperations *agenda_operations = &agenda_plain_operations;
static _Thread_local CettaSpaceAgendaObservationScope *agenda_observation;
static _Thread_local CettaSpaceAgendaSelectionScope *agenda_selection;
static _Thread_local CettaSpaceAgendaActivationScope *agenda_activation;

/* Both optional scopes choose actual operations at entry/leave. Ordinary
 * provider steps do not inspect either scope to discover their policy. */
static void agenda_operations_refresh(void) {
    bool selected=false;
    for (CettaSpaceAgendaSelectionScope *scope=agenda_selection;scope;scope=scope->previous)
        if (!scope->selection->completed) {selected=true;break;}
    agenda_operations=agenda_activation
        ? agenda_observation
            ? selected ? &agenda_selected_controlled_recorded_operations : &agenda_controlled_recorded_operations
            : selected ? &agenda_selected_controlled_plain_operations : &agenda_controlled_plain_operations
        : agenda_observation
        ? selected ? &agenda_selected_recorded_operations : &agenda_recorded_operations
        : selected ? &agenda_selected_plain_operations : &agenda_plain_operations;
}

bool cetta_space_agenda_activation_enter(CettaSpaceAgendaActivationScope *scope,
        CettaSpaceAgenda *agenda, CettaSpaceAgendaActivationPermit permit, void *context) {
    if (!scope || !agenda || !permit || scope->entered ||
        agenda->contract.explicit_inputs || agenda->contract.snapshot_worlds) return false;
    for (CettaSpaceAgendaActivationScope *held=agenda_activation;held;held=held->previous)
        if (held==scope || held->agenda==agenda) return false;
    *scope=(CettaSpaceAgendaActivationScope){.agenda=agenda,.permit=permit,
        .context=context,.previous=agenda_activation,.entered=true};
    agenda_activation=scope;agenda_operations_refresh();
    return true;
}

void cetta_space_agenda_activation_leave(CettaSpaceAgendaActivationScope *scope) {
    if (!scope || !scope->entered) return;
    assert(agenda_activation==scope && !scope->notifying);
    agenda_activation=scope->previous;scope->entered=false;scope->previous=NULL;
    agenda_operations_refresh();
}

static bool agenda_activation_permit(CettaSpaceAgenda *agenda) {
    for (CettaSpaceAgendaActivationScope *scope=agenda_activation;scope;scope=scope->previous) {
        if (scope->agenda!=agenda) continue;
        if (scope->notifying) return false;
        scope->notifying=true;
        bool permitted=scope->permit(scope->context,agenda);
        scope->notifying=false;
        return permitted;
    }
    return true;
}

static bool agenda_observation_refuse(CettaSpaceAgendaObservationScope *scope,
                                      const char *reason) {
    if (scope && !scope->refusal) scope->refusal = reason;
    return false;
}

bool cetta_space_agenda_observation_enter(CettaSpaceAgendaObservationScope *scope,
        CettaSpaceAgenda *agenda, CettaSpaceAgendaObserve observe,
        CettaSpaceAgendaPermit permit, CettaSpaceAgendaChargeWitness charge,
        void *context) {
    if (!scope || !agenda || !observe || !permit || !charge || agenda_observation ||
        agenda->root != agenda || agenda->owned_next || agenda->pending_child ||
        !agenda->contract.explicit_inputs || agenda->contract.set_support ||
        agenda->contract.raw_mm2_order || agenda->contract.snapshot_worlds ||
        agenda->advise || agenda->before || agenda->advice_context)
        return false;
    *scope = (CettaSpaceAgendaObservationScope){.previous=agenda_observation,
        .agenda=agenda, .observe=observe, .permit=permit, .charge=charge,
        .context=context, .entered=true};
    agenda_observation = scope;
    agenda_operations_refresh();
    return true;
}

void cetta_space_agenda_observation_leave(CettaSpaceAgendaObservationScope *scope) {
    assert(scope && scope->entered && agenda_observation == scope && !scope->notifying);
    agenda_observation = scope->previous;
    agenda_operations_refresh();
    scope->previous = NULL;
    scope->entered = false;
}

bool cetta_space_agenda_observation_complete(const CettaSpaceAgendaObservationScope *scope) {
    return scope && !scope->entered && !scope->notifying && !scope->refusal;
}

static bool agenda_observation_ready(CettaSpaceAgenda *a) {
    CettaSpaceAgendaObservationScope *scope = agenda_observation;
    if (!scope) return true;
    if (scope->agenda != a || scope->notifying)
        return agenda_observation_refuse(scope,"ForeignOrReentrantAgenda");
    return !scope->refusal;
}

static bool agenda_observe_event(CettaSpaceAgenda *a, CettaSpaceAgendaEventKind kind,
        uint64_t index, uint64_t detail, uint64_t allowance, SpaceReadToken read,
        Atom *pattern, Atom *value, const Bindings *environment) {
    CettaSpaceAgendaObservationScope *scope = agenda_observation;
    if (!scope) return true;
    if (!agenda_observation_ready(a)) return false;
    if (scope->events == UINT64_MAX)
        return agenda_observation_refuse(scope,"AgendaEventOrdinal");
    CettaSpaceAgendaEvent event = {.agenda=a, .kind=kind, .index=index,
        .detail=detail, .allowance=allowance, .read=read,
        .pattern=pattern, .value=value, .environment=environment};
    scope->events++;
    scope->notifying = true;
    bool ok = scope->observe(scope->context,&event);
    scope->notifying = false;
    if (!ok) return agenda_observation_refuse(scope,"AgendaCaptureRefused");
    return !scope->refusal;
}

static bool agenda_observe(CettaSpaceAgenda *a, CettaSpaceAgendaEventKind kind) {
    if (!agenda_observation) return true;
    return agenda_observe_event(a,kind,0u,0u,0u,(SpaceReadToken){0},NULL,NULL,NULL);
}

static void agenda_charge(CettaSpaceAgenda *a, CettaSpaceAgendaChargeSite site,
                           CettaNativeCostKind kind, uint64_t units) {
    CettaSpaceAgendaObservationScope *scope = agenda_observation;
    if (!scope) {
        cetta_native_cost_charge(kind,units);
        return;
    }
    bool witnessed = agenda_observation_ready(a);
    if (witnessed) {
        scope->notifying = true;
        witnessed = scope->charge(scope->context,a,site,(unsigned)kind,units,false);
        scope->notifying = false;
        if (!witnessed) agenda_observation_refuse(scope,"AgendaChargeWitness");
    }
    /* A failed observer cannot erase work already performed by this primitive. */
    cetta_native_cost_charge(kind,units);
    if (witnessed && !scope->refusal) {
        scope->notifying = true;
        bool ok = scope->charge(scope->context,a,site,(unsigned)kind,units,true);
        scope->notifying = false;
        if (!ok) agenda_observation_refuse(scope,"AgendaChargeWitness");
    }
}

static bool agenda_observation_permit(CettaSpaceAgenda *a) {
    CettaSpaceAgendaObservationScope *scope = agenda_observation;
    if (!scope) return true;
    if (scope->commits==UINT64_MAX)
        return agenda_observation_refuse(scope,"AgendaCommitOrdinal");
    if (!agenda_observe(a,CETTA_AGENDA_EVENT_PRECOMMIT)) return false;
    CettaSpaceAgendaEvent event = {.agenda=a, .kind=CETTA_AGENDA_EVENT_PRECOMMIT};
    scope->notifying = true;
    bool ok = scope->permit(scope->context,&event);
    scope->notifying = false;
    if (!ok) return agenda_observation_refuse(scope,"AgendaPrecommitRefused");
    return !scope->refusal;
}

static void agenda_observation_committed(CettaSpaceAgenda *a) {
    CettaSpaceAgendaObservationScope *scope=agenda_observation;
    if (!scope) return;
    assert(scope->agenda==a && !scope->notifying && scope->commits<UINT64_MAX);
    scope->commits++;
}


static void match_free(CettaSpaceAgendaMatch *match) {
    if (!match) return;
    match_atoms_cursor_free(match->cursor);
    bindings_free(&match->result);
    bindings_free(&match->captured);
    if (match->isolated) {
        arena_free(&match->arena);
        hashcons_free(&match->hashcons);
    }
    free(match);
}

static bool shaped(Atom *atom, SymbolId head, size_t arity) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == arity + 1u &&
        atom_is_symbol_id(atom->expr.elems[0], head);
}

static bool headed(Atom *atom, SymbolId head) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len &&
        atom_is_symbol_id(atom->expr.elems[0], head);
}

static Atom *natural(Arena *arena, uint64_t value) {
    if (value <= INT64_MAX) return atom_int(arena, (int64_t)value);
    char digits[32];
    snprintf(digits, sizeof(digits), "%llu", (unsigned long long)value);
    return atom_symbol(arena, digits);
}

/* These fields describe executable syntax. Prime's existing quotation keeps
 * inspecting a retained observation from executing its described writes. */
static Atom *code_value(CettaSpaceAgenda *a, Atom *syntax) {
    return atom_expr2(a->owner,atom_symbol_id(a->owner,g_builtin_syms.quote),syntax);
}

/* MORK's set carrier is compact-expression bytes. Distinct parser/frame
 * names for the same variable-incidence pattern denote the same member.
 * Encoding refusal is an unfinished carrier obligation, never inequality. */
static bool carrier_equal(CettaSpaceAgenda *a, Atom *left, Atom *right, bool *equal) {
    if (!a->contract.set_support) { *equal=atom_eq(left,right); return true; }
    uint8_t *lb=NULL,*rb=NULL;
    size_t ln=0u,rn=0u;
    const char *error=NULL;
    bool complete=cetta_mm2_atom_to_bridge_expr_bytes(a->owner,left,&lb,&ln,&error) &&
        cetta_mm2_atom_to_bridge_expr_bytes(a->owner,right,&rb,&rn,&error);
    if(complete)*equal=ln==rn && (!ln || !memcmp(lb,rb,ln));
    free(lb);free(rb);
    return complete;
}

static bool carrier_contains(CettaSpaceAgenda *a, Space *space, Atom *atom, bool *found) {
    *found=false;
    for(CettaIndex i=0u;i<space_length64(space);i++) {
        bool equal=false;
        if(!carrier_equal(a,space_get_at64(space,i),atom,&equal))return false;
        if(equal){*found=true;return true;}
    }
    /* Even the first set member must have a valid compact representation. */
    if(a->contract.set_support) {
        uint8_t *bytes=NULL;size_t length=0u;const char *error=NULL;
        bool complete=cetta_mm2_atom_to_bridge_expr_bytes(a->owner,atom,&bytes,&length,&error);
        free(bytes);return complete;
    }
    return true;
}

static void level_free(JoinLevel *level) {
    if (level->cursor_ready) space_occurrence_cursor_release(&level->cursor);
    if (level->environment_ready) bindings_free(&level->environment);
    free(level->aggregate);
    memset(level, 0, sizeof(*level));
}

static void join_free(CettaSpaceAgenda *a) {
    if (a->levels) {
        for (size_t i = 0u; i <= a->input_count; i++) level_free(&a->levels[i]);
        free(a->levels);
    }
    a->levels = NULL;
    a->join_ready = false;
}

static Arena *agenda_arena_new(void) {
    Arena *owner=malloc(sizeof(*owner));
    if (!owner) return NULL;
    arena_init_detached(owner);
    arena_set_block_capacity(owner,4096u);
    return owner;
}

static bool agenda_world_init(CettaSpaceAgenda *a) {
    if (!a->world_ready) {
        space_init_with_universe(&a->world,a->space->universe);
        a->world_ready=true;
    }
    if (!a->trace) a->trace=atom_expr(a->owner,NULL,0u);
    return a->trace!=NULL;
}

static CettaSpaceAgenda *agenda_new(Space *space, Arena *owner,
        const Bindings *environment, CettaSpaceAgendaContract contract, bool owned) {
    if (!space || (!owner && !owned) || !g_symbols || space->overlay_base ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT) ||
        (contract.snapshot_worlds && (!contract.explicit_inputs || contract.set_support))) return NULL;
    CettaSpaceAgenda *a = calloc(1u, sizeof(*a));
    if (!a) return NULL;
    if (owned) {
        a->owned_arena=agenda_arena_new();
        if (!a->owned_arena) {free(a);return NULL;}
        owner=a->owned_arena;
    }
    a->space = space; a->space_identity = space->instance_id;
    a->owner = owner; a->contract = contract;
    a->root = a;
    a->origin_read = space_read_token(space);
    a->world_id = 1u; a->next_world_id = 2u;
    if (!bindings_clone(&a->captured, environment) ||
        (owned && !bindings_promote_atoms_to_arena(&a->captured,owner))) {
        bindings_free(&a->captured);
        if (a->owned_arena) {arena_free(a->owned_arena);free(a->owned_arena);}
        free(a);return NULL;
    }
    a->exec = symbol_intern_cstr(g_symbols,"exec");
    a->comma = symbol_intern_cstr(g_symbols,",");
    a->input = symbol_intern_cstr(g_symbols,"I");
    a->output = symbol_intern_cstr(g_symbols,"O");
    a->btm = symbol_intern_cstr(g_symbols,"BTM");
    a->read = symbol_intern_cstr(g_symbols,"read");
    a->take = symbol_intern_cstr(g_symbols,"take");
    a->all = symbol_intern_cstr(g_symbols,"all");
    a->absent = symbol_intern_cstr(g_symbols,"absent");
    a->plus = symbol_intern_cstr(g_symbols,"+");
    a->minus = symbol_intern_cstr(g_symbols,"-");
    a->wildcard = atom_var(owner,"$__native_agenda_row");
    a->phase = AGENDA_SELECT;
    if (contract.snapshot_worlds) {
        if (!agenda_world_init(a)) {
            space_free(&a->world);bindings_free(&a->captured);
            if (a->owned_arena) {arena_free(a->owned_arena);free(a->owned_arena);}
            free(a);return NULL;
        }
        a->capture_length = space_length64(space);
        a->phase = AGENDA_WORLD_CAPTURE;
    }
    return a;
}

CettaSpaceAgenda *cetta_space_agenda_new(Space *space, Arena *owner,
        const Bindings *environment, CettaSpaceAgendaContract contract) {
    return agenda_new(space,owner,environment,contract,false);
}

static bool agenda_control_one(CettaSpaceAgenda *a,
        const CettaSelectionAutomaton *schedule,CettaSpaceAgendaAdvise advise,
        CettaSpaceAgendaBefore before,void *context) {
    if(!a || !schedule || (!advise!=!before) ||
       !cetta_selection_automaton_has_recurrent_oldest(schedule))return false;
    if((!a->advise!=!advise) && (a->phase==AGENDA_SELECT || a->phase==AGENDA_RANK)) {
        if(a->selection_ready)space_occurrence_cursor_release(&a->selection);
        a->selection_ready=false;a->selected=NULL;a->phase=AGENDA_SELECT;
    }
    /* Each descendant owns its phase. An unchanged graph must not replace
     * that phase with the newly parsed descriptor's initial state. */
    if (!cetta_selection_automaton_same_graph(&a->schedule,schedule))
        a->schedule=*schedule;
    a->advise=advise;a->before=before;a->advice_context=context;
    return true;
}

bool cetta_space_agenda_control(CettaSpaceAgenda *a,
        const CettaSelectionAutomaton *schedule,CettaSpaceAgendaAdvise advise,
        CettaSpaceAgendaBefore before,void *context) {
    if (!a) return false;
    for (CettaSpaceAgenda *node=a;node;node=node->owned_next)
        if (!agenda_control_one(node,schedule,advise,before,context)) return false;
    return true;
}

bool cetta_space_agenda_has_advice(const CettaSpaceAgenda *a,const void *advice) {
    if(!a || !advice)return false;
    for(const CettaSpaceAgenda *node=a;node;node=node->owned_next) {
        if (node->phase != AGENDA_DONE && node->chosen_advice==advice) return true;
        for(size_t i=0u;i<node->candidate_count;i++)
            if(node->candidates[i].advice==advice)return true;
    }
    return false;
}

static bool candidate_add(CettaSpaceAgenda *a,CettaIndex index,Atom *row) {
    if(a->candidate_count==a->candidate_capacity) {
        size_t n=a->candidate_capacity?a->candidate_capacity*2u:8u;
        if(n<=a->candidate_capacity || n>SIZE_MAX/sizeof(*a->candidates))return false;
        ExecCandidate *grown=realloc(a->candidates,n*sizeof(*grown));
        if(!grown)return false;
        a->candidates=grown;a->candidate_capacity=n;
    }
    Atom *owned=atom_deep_copy(a->owner,row);
    void *advice=a->advise(a->advice_context,owned,&a->captured,owned->expr.elems[1]);
    if(!advice)return false;
    a->candidates[a->candidate_count++]=(ExecCandidate){index,owned,advice};
    return true;
}

static CettaSpaceAgendaStep pending(CettaSpaceAgenda *a, const char *why) {
    a->pending = why;
    return CETTA_SPACE_AGENDA_PENDING;
}

/* An append cannot replace a physical occurrence already selected. A prefix
 * rewrite may do so even if the new value happens to compare equal. The old
 * transaction then remains owned and cannot authorize a different token. */
static bool selected_occurrence_current(const CettaSpaceAgenda *a) {
    if (!a->contract.explicit_inputs || !a->selected) return true;
    SpaceReadToken current=space_read_token(a->space);
    return current.instance_id==a->selected_read.instance_id &&
        current.prefix_epoch==a->selected_read.prefix_epoch &&
        a->selected_index<space_length64(a->space) &&
        atom_eq(space_get_at64(a->space,a->selected_index),a->selected);
}

static bool borrowed_capture_admitted(const CettaSpaceAgenda *a) {
    const Space *space=a->space;
    return a->contract.explicit_inputs && !a->contract.set_support && !a->contract.snapshot_worlds &&
        space->kind==SPACE_KIND_ATOM && !space->overlay_base &&
        !space->module_own_view && !space->dep_count &&
        space_match_backend_has_native_occurrences(space);
}

static bool waiting_contains(const CettaSpaceAgenda *a,CettaIndex index) {
    for(size_t i=0u;i<a->waiting_count;i++)
        if(a->waiting[i].index==index && space_read_token_is_current(a->waiting[i].read))return true;
    return false;
}

static bool waiting_add(CettaSpaceAgenda *a) {
    if(a->waiting_count==a->waiting_capacity) {
        size_t n=a->waiting_capacity?a->waiting_capacity*2u:8u;
        if(n<=a->waiting_capacity || n>SIZE_MAX/sizeof(*a->waiting))return false;
        WaitingExec *grown=realloc(a->waiting,n*sizeof(*grown));
        if(!grown)return false;
        a->waiting=grown;a->waiting_capacity=n;
    }
    a->waiting[a->waiting_count++]=(WaitingExec){a->capture_read,a->selected_index,a->selected};
    return true;
}

static bool parse_selected(CettaSpaceAgenda *a) {
    Atom *in = a->selected->expr.elems[2];
    Atom *out = a->selected->expr.elems[3];
    if ((!headed(in,a->comma) && !headed(in,a->input)) || !headed(out,a->output)) return false;
    size_t n = in->expr.len - 1u;
    if (n > SIZE_MAX / sizeof(*a->inputs) || n == SIZE_MAX) return false;
    a->inputs = calloc(n ? n : 1u,sizeof(*a->inputs));
    if (!a->inputs) return false;
    a->input_count = n;
    for (size_t i = 0u; i < n; i++) {
        Atom *p = in->expr.elems[i+1u];
        AgendaInput spec = {.kind=INPUT_READ,.pattern=p};
        if (headed(in,a->input)) {
            if (shaped(p,a->btm,1u) || (a->contract.explicit_inputs && shaped(p,a->read,1u)))
                spec.pattern = p->expr.elems[1];
            else if (a->contract.explicit_inputs && shaped(p,a->take,1u))
                spec = (AgendaInput){INPUT_TAKE,p->expr.elems[1],NULL};
            else if (a->contract.explicit_inputs && shaped(p,a->absent,1u))
                spec = (AgendaInput){INPUT_ABSENT,p->expr.elems[1],NULL};
            else if (a->contract.explicit_inputs && shaped(p,a->all,2u))
                spec = (AgendaInput){INPUT_ALL,p->expr.elems[1],p->expr.elems[2]};
            else return false;
        }
        a->inputs[i] = spec;
    }
    for (CettaExprIndex i = 1u; i < out->expr.len; i++)
        if (!shaped(out->expr.elems[i],a->plus,1u) && !shaped(out->expr.elems[i],a->minus,1u))
            return false;
    a->sinks = out;
    return true;
}

static bool join_begin(CettaSpaceAgenda *a) {
    join_free(a);
    if (a->input_count >= SIZE_MAX / sizeof(*a->levels)) return false;
    a->levels = calloc(a->input_count + 1u,sizeof(*a->levels));
    if (!a->levels) return false;
    if (!bindings_clone(&a->levels[0].environment,&a->captured)) {
        bindings_free(&a->levels[0].environment);return false;
    }
    a->levels[0].environment_ready = true;
    a->depth = 0u; a->join_ready = true;
    a->phase = a->validating ? AGENDA_VALIDATE_MATCH : AGENDA_MATCH;
    return true;
}

static bool match_epoch_record(CettaSpaceAgenda *a, CettaFrameIdentity epoch) {
    size_t side=a->validating?1u:0u;
    if(a->match_epoch_count[side]==a->match_epoch_capacity[side]) {
        size_t n=a->match_epoch_capacity[side]?a->match_epoch_capacity[side]*2u:16u;
        CettaFrameIdentity *grown=n>a->match_epoch_capacity[side] && n<=SIZE_MAX/sizeof(*grown)
            ?realloc(a->match_epochs[side],n*sizeof(*grown)):NULL;
        if(!grown)return false;
        a->match_epochs[side]=grown;a->match_epoch_capacity[side]=n;
    }
    a->match_epochs[side][a->match_epoch_count[side]++]=epoch;
    return true;
}

static bool matching_environment_admitted(const Bindings *env) {
    BindingsIterator iterator={.bindings=env};Binding item;
    while(bindings_iterator_next(&iterator,&item))
        if(atom_has_thread_local_resource(item.name_key) ||
           atom_has_thread_local_resource(item.value.skeleton))return false;
    for(uint32_t i=0u;i<env->eq_len;i++)
        if(atom_has_thread_local_resource(env->constraints[i].lhs.skeleton) ||
           atom_has_thread_local_resource(env->constraints[i].rhs.skeleton))return false;
    return true;
}

bool cetta_space_agenda_match_prepared(const CettaSpaceAgenda *a) {
    return a && a->matching && a->matching->prepared;
}

static bool advance_level(CettaSpaceAgenda *a, Bindings *next) {
    JoinLevel *child=&a->levels[a->depth+1u];
    level_free(child);
    bindings_move(&child->environment,next); child->environment_ready=true;
    a->depth++;
    return true;
}

static bool aggregate_add(JoinLevel *level, Atom *value) {
    if (level->aggregate_length == level->aggregate_capacity) {
        size_t n=level->aggregate_capacity ? level->aggregate_capacity*2u : 8u;
        if(n<=level->aggregate_capacity || n>SIZE_MAX/sizeof(*level->aggregate))return false;
        Atom **p=realloc(level->aggregate,n*sizeof(*p)); if(!p)return false;
        level->aggregate=p;level->aggregate_capacity=n;
    }
    level->aggregate[level->aggregate_length++]=value;return true;
}

static CettaSpaceAgendaStep sink_step(CettaSpaceAgenda *a) {
    if(a->sink_row==a->row_count) {a->phase=AGENDA_COMMIT;return CETTA_SPACE_AGENDA_PROGRESS;}
    Atom *out=a->rows[a->sink_row].outputs;
    if(a->sink_index==out->expr.len) {a->sink_row++;a->sink_index=1u;return CETTA_SPACE_AGENDA_PROGRESS;}
    Atom *sink=out->expr.elems[a->sink_index];
    Atom *body=sink->expr.elems[1];
    if(atom_is_symbol_id(sink->expr.elems[0],a->plus)) {
        bool found=false;
        if(a->contract.set_support && !carrier_contains(a,&a->replacement,body,&found))
            return pending(a,"CompactSetCarrier");
        if((!a->contract.set_support || !found) &&
            !space_admit_atom(&a->replacement,a->owner,body))return pending(a,"SinkAdmission");
    } else if(a->contract.set_support) {
        CettaCount n=space_length64(&a->replacement);
        if(n>SIZE_MAX)return pending(a,"SinkOccurrenceMask");
        uint8_t *mask=calloc(n?(size_t)n:1u,1u);
        if(!mask)return pending(a,"SinkOccurrenceMask");
        for(CettaIndex i=0u;i<n;i++) {
            bool equal=false;
            if(!carrier_equal(a,space_get_at64(&a->replacement,i),body,&equal)) {
                free(mask);return pending(a,"CompactSetCarrier");
            }
            mask[i]=equal;
        }
        CettaCount removed=0u;
        bool complete=space_remove_occurrence_mask_stable(&a->replacement,mask,n,&removed);
        free(mask);
        if(!complete)return pending(a,"SinkRemoval");
    } else (void)space_remove(&a->replacement,body);
    a->sink_index++;return CETTA_SPACE_AGENDA_PROGRESS;
}

static CettaSpaceAgendaStep world_capture_step(CettaSpaceAgenda *a) {
    if (a->capture_index<a->capture_length) {
        Atom *row=space_get_at64(a->space,a->capture_index);
        if (!row || !space_admit_atom(&a->world,a->owner,row))
            return pending(a,"WorldSnapshotAdmission");
        a->capture_index++;
        cetta_native_cost_charge(CETTA_COST_CANDIDATE_ROW,1u);
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    a->space=&a->world;a->space_identity=a->world.instance_id;
    a->phase=AGENDA_SELECT;
    return CETTA_SPACE_AGENDA_PROGRESS;
}

static CettaSpaceAgendaStep world_report_step(CettaSpaceAgenda *a) {
    if (a->reported_count<space_length64(a->space)) {
        if (a->reported_count==a->reported_capacity) {
            size_t n=a->reported_capacity?a->reported_capacity*2u:8u;
            if (n<=a->reported_capacity || n>SIZE_MAX/sizeof(*a->reported_atoms))
                return pending(a,"WorldObservationCapacity");
            Atom **grown=realloc(a->reported_atoms,n*sizeof(*grown));
            if (!grown) return pending(a,"WorldObservationCapacity");
            a->reported_atoms=grown;a->reported_capacity=n;
        }
        Atom *row=space_get_at64(a->space,a->reported_count);
        if (!row) return pending(a,"WorldObservationProjection");
        a->reported_atoms[a->reported_count++]=row;
        cetta_native_cost_charge(CETTA_COST_PUBLISH,1u);
        return CETTA_SPACE_AGENDA_PROGRESS;
    }
    if (a->reported_count!=(CettaExprLen)a->reported_count)
        return pending(a,"WorldObservationLength");
    a->receipt=atom_expr(a->owner,(Atom *[]){atom_symbol(a->owner,"SnapshotWorld"),
        natural(a->owner,a->world_id),
        atom_expr2(a->owner,atom_symbol(a->owner,"atoms"),code_value(a,
            atom_expr(a->owner,a->reported_atoms,(CettaExprLen)a->reported_count))),
        atom_expr2(a->owner,atom_symbol(a->owner,"trace"),code_value(a,a->trace)),
        atom_expr2(a->owner,atom_symbol(a->owner,"source"),atom_expr2(a->owner,
            natural(a->owner,a->root->origin_read.instance_id),
            natural(a->owner,a->root->origin_read.revision))),
        atom_expr2(a->owner,atom_symbol(a->owner,"state"),atom_symbol(a->owner,"Quiescent"))},6u);
    if (!a->receipt) return pending(a,"WorldObservationCapacity");
    a->phase=AGENDA_DONE;
    return CETTA_SPACE_AGENDA_WORLD;
}

/* Both compiled providers below execute these same phase transitions. */
#define AGENDA_SUFFIX _plain
#define AGENDA_RECORDING 0
#define AGENDA_CONTROLLED 0
#include "native_space_agenda_steps.inc"
#undef AGENDA_CONTROLLED
#undef AGENDA_RECORDING
#undef AGENDA_SUFFIX
#define AGENDA_SUFFIX _recorded
#define AGENDA_RECORDING 1
#define AGENDA_CONTROLLED 0
#include "native_space_agenda_steps.inc"
#undef AGENDA_CONTROLLED
#undef AGENDA_RECORDING
#undef AGENDA_SUFFIX
#define AGENDA_SUFFIX _controlled_plain
#define AGENDA_RECORDING 0
#define AGENDA_CONTROLLED 1
#include "native_space_agenda_steps.inc"
#undef AGENDA_CONTROLLED
#undef AGENDA_RECORDING
#undef AGENDA_SUFFIX
#define AGENDA_SUFFIX _controlled_recorded
#define AGENDA_RECORDING 1
#define AGENDA_CONTROLLED 1
#include "native_space_agenda_steps.inc"
#undef AGENDA_CONTROLLED
#undef AGENDA_RECORDING
#undef AGENDA_SUFFIX

#define AGENDA_OPERATIONS(suffix, advance) { \
    .destroy=cetta_space_agenda_free##suffix, \
    .match_capture=cetta_space_agenda_match_capture##suffix, \
    .match_run=cetta_space_agenda_match_run##suffix, \
    .step=advance, \
    .prepare_source=cetta_space_agenda_prepare_source##suffix, \
    .revalidate_choice=revalidate_choice##suffix, \
}
static const AgendaOperations agenda_plain_operations =
    AGENDA_OPERATIONS(_plain,cetta_space_agenda_step_plain);
static const AgendaOperations agenda_recorded_operations =
    AGENDA_OPERATIONS(_recorded,cetta_space_agenda_step_recorded);
static const AgendaOperations agenda_controlled_plain_operations =
    AGENDA_OPERATIONS(_controlled_plain,cetta_space_agenda_step_controlled_plain);
static const AgendaOperations agenda_controlled_recorded_operations =
    AGENDA_OPERATIONS(_controlled_recorded,cetta_space_agenda_step_controlled_recorded);
static CettaSpaceAgendaStep selection_step_kernel(CettaSpaceAgendaSelection *,
    const AgendaOperations *);
static inline __attribute__((always_inline)) CettaSpaceAgendaStep selection_dispatch(
        CettaSpaceAgenda *a, const AgendaOperations *base) {
    for (CettaSpaceAgendaSelectionScope *scope=agenda_selection;scope;scope=scope->previous) {
        if (scope->selection->agenda!=a || scope->selection->completed) continue;
        CettaSpaceAgendaStep result=selection_step_kernel(scope->selection,base);
        if (scope->selection->completed) {
            bool pending=false;
            for (CettaSpaceAgendaSelectionScope *held=agenda_selection;held;held=held->previous)
                if (!held->selection->completed) {pending=true;break;}
            if (!pending) agenda_operations=base;
        }
        return result;
    }
    return base->step(a);
}
static CettaSpaceAgendaStep selected_step_plain(CettaSpaceAgenda *a) {
    return selection_dispatch(a,&agenda_plain_operations);
}
static CettaSpaceAgendaStep selected_step_recorded(CettaSpaceAgenda *a) {
    return selection_dispatch(a,&agenda_recorded_operations);
}
static CettaSpaceAgendaStep selected_step_controlled_plain(CettaSpaceAgenda *a) {
    return selection_dispatch(a,&agenda_controlled_plain_operations);
}
static CettaSpaceAgendaStep selected_step_controlled_recorded(CettaSpaceAgenda *a) {
    return selection_dispatch(a,&agenda_controlled_recorded_operations);
}
static const AgendaOperations agenda_selected_plain_operations =
    AGENDA_OPERATIONS(_plain,selected_step_plain);
static const AgendaOperations agenda_selected_recorded_operations =
    AGENDA_OPERATIONS(_recorded,selected_step_recorded);
static const AgendaOperations agenda_selected_controlled_plain_operations =
    AGENDA_OPERATIONS(_controlled_plain,selected_step_controlled_plain);
static const AgendaOperations agenda_selected_controlled_recorded_operations =
    AGENDA_OPERATIONS(_controlled_recorded,selected_step_controlled_recorded);
#undef AGENDA_OPERATIONS

void cetta_space_agenda_free(CettaSpaceAgenda *agenda) {
    agenda_operations->destroy(agenda);
}
CettaSpaceAgendaMatch *cetta_space_agenda_match_capture(CettaSpaceAgenda *agenda) {
    return agenda_operations->match_capture(agenda);
}
bool cetta_space_agenda_match_run(CettaSpaceAgendaMatch *match, uint64_t node_budget) {
    return agenda_operations->match_run(match, node_budget);
}
CettaSpaceAgendaStep cetta_space_agenda_step(CettaSpaceAgenda *agenda) {
    return agenda_operations->step(agenda);
}
CettaSpaceAgendaStep cetta_space_agenda_prepare_source(CettaSpaceAgenda *agenda,
        SpaceReadToken source, CettaIndex index) {
    return agenda_operations->prepare_source(agenda,source,index);
}

CettaSpaceAgenda *cetta_space_agenda_child(CettaSpaceAgenda *a) {
    return a?a->pending_child:NULL;
}

bool cetta_space_agenda_accept_child(CettaSpaceAgenda *a,CettaSpaceAgenda *child) {
    if (!a || !child || a->pending_child!=child || a->root!=child->root) return false;
    a->pending_child=NULL;
    return true;
}

void *cetta_space_agenda_advice(const CettaSpaceAgenda *a) {
    return a?a->chosen_advice:NULL;
}

bool cetta_space_agenda_current(const CettaSpaceAgenda *a) {
    return a && (!a->contract.snapshot_worlds || space_read_token_is_current(a->root->origin_read));
}

bool cetta_space_agenda_ready(const CettaSpaceAgenda *a) {
    for (const CettaSpaceAgenda *node=a;node;node=node->owned_next)
        if (node->phase!=AGENDA_DONE && !node->pending) return true;
    return false;
}

Atom *cetta_space_agenda_publication_source(const CettaSpaceAgenda *a) {
    if (!a || !a->contract.explicit_inputs ||
        a->phase != AGENDA_COMMIT || !a->selected || !cetta_space_agenda_current(a) ||
        !selected_occurrence_current(a) ||
        !space_read_token_is_current(a->capture_read)) return NULL;
    return a->selected;
}

Atom *cetta_space_agenda_activation_source(const CettaSpaceAgenda *a) {
    if (!a || a->contract.explicit_inputs || a->contract.snapshot_worlds ||
        a->phase!=AGENDA_ACTIVATE || !a->selected ||
        !space_read_token_is_current(a->selection_read)) return NULL;
    return a->selected;
}

void cetta_space_agenda_resume(CettaSpaceAgenda *a) {
    for (CettaSpaceAgenda *node=a;node;node=node->owned_next)node->pending=NULL;
}

Atom *cetta_space_agenda_receipt(CettaSpaceAgenda *a) {return a?a->receipt:NULL;}
Atom *cetta_space_agenda_pending(CettaSpaceAgenda *a) {
    if(!a)return NULL;
    static const char *phases[]={"Select","Capture","Match","ValidateCapture","ValidateMatch","Build","Sink","Commit","Wait","Rank","WorldCapture","WorldReport","Done","Activate"};
    Atom **waiting=a->waiting_count?malloc(a->waiting_count*sizeof(*waiting)):NULL;
    for(size_t i=0u;waiting && i<a->waiting_count;i++)
        waiting[i]=atom_expr(a->owner,(Atom *[]){natural(a->owner,a->waiting[i].read.revision),
            natural(a->owner,a->waiting[i].index),a->waiting[i].exec},3u);
    Atom *fields[10]={atom_symbol(a->owner,"SpaceAgenda"),
        atom_expr2(a->owner,atom_symbol(a->owner,"phase"),atom_symbol(a->owner,phases[a->phase])),
        atom_expr2(a->owner,atom_symbol(a->owner,"selected"),code_value(a,a->selected?a->selected:atom_expr(a->owner,NULL,0u))),
        atom_expr2(a->owner,atom_symbol(a->owner,"commits"),natural(a->owner,a->commits)),
        atom_expr2(a->owner,atom_symbol(a->owner,"retries"),natural(a->owner,a->retries)),
        atom_expr2(a->owner,atom_symbol(a->owner,"obligation"),atom_symbol(a->owner,a->pending?a->pending:"Ready")),
        atom_expr2(a->owner,atom_symbol(a->owner,"waiting"),code_value(a,
            waiting?atom_expr(a->owner,waiting,(CettaExprLen)a->waiting_count):
                a->waiting_count?atom_symbol(a->owner,"PendingProjection"):atom_expr(a->owner,NULL,0u))),
        atom_expr2(a->owner,atom_symbol(a->owner,"observation"),atom_symbol(a->owner,
            a->contract.snapshot_worlds?"snapshot-worlds":"committed-trace"))};
    if (a->contract.snapshot_worlds) {
        uint64_t live=0u,blocked=0u;
        for (const CettaSpaceAgenda *node=a;node;node=node->owned_next) {
            if (node->phase==AGENDA_DONE)continue;
            live++;if(node->pending)blocked++;
        }
        fields[8]=atom_expr2(a->owner,atom_symbol(a->owner,"live-worlds"),natural(a->owner,live));
        fields[9]=atom_expr2(a->owner,atom_symbol(a->owner,"blocked-worlds"),natural(a->owner,blocked));
    }
    Atom *result=atom_expr(a->owner,fields,a->contract.snapshot_worlds?10u:8u);
    free(waiting);return result;
}

static void agenda_storage_one(const CettaSpaceAgenda *a,CettaStorageCensus *c) {
    if(!a || !cetta_storage_object(c,a,sizeof(*a),CETTA_STORAGE_BLOCK))return;
    if(a->owned_arena)arena_storage_census(a->owned_arena,c);
    bindings_storage_census(&a->captured,c);
    if(a->matching && cetta_storage_object(c,a->matching,sizeof(*a->matching),CETTA_STORAGE_BLOCK)) {
        if(a->matching->isolated) {
            arena_storage_census(&a->matching->arena,c);
            hashcons_storage_census(&a->matching->hashcons,c);
        }
        bindings_storage_census(&a->matching->captured,c);
        bindings_storage_census(&a->matching->result,c);
        match_atoms_cursor_storage_census(a->matching->cursor,c);
    }
    cetta_storage_array(c,a->reported_atoms,a->reported_capacity,sizeof(*a->reported_atoms),0u);
    if (a->world_ready)space_storage_census(&a->world,c);
    cetta_storage_array(c,a->selected_key,a->selected_key_length,1u,0u);
    cetta_storage_array(c,a->inputs,a->input_count,sizeof(*a->inputs),0u);
    cetta_storage_array(c,a->rows,a->row_capacity,sizeof(*a->rows),0u);
    cetta_storage_array(c,a->validation,a->validation_capacity,sizeof(*a->validation),0u);
    for(size_t i=0u;i<2u;i++)
        cetta_storage_array(c,a->match_epochs[i],a->match_epoch_capacity[i],sizeof(uint32_t),0u);
    cetta_storage_array(c,a->waiting,a->waiting_capacity,sizeof(*a->waiting),0u);
    cetta_storage_array(c,a->candidates,a->candidate_capacity,sizeof(*a->candidates),0u);
    if(a->snapshot_ready)space_storage_census(&a->snapshot,c);
    if(a->current_ready)space_storage_census(&a->current,c);
    if(a->replacement_ready)space_storage_census(&a->replacement,c);
    if(a->selection_ready)space_occurrence_cursor_storage_census(&a->selection,c);
    if(a->levels) {
        cetta_storage_array(c,a->levels,a->input_count+1u,sizeof(*a->levels),0u);
        for(size_t i=0u;i<=a->input_count;i++) {
            const JoinLevel *l=&a->levels[i];
            if(l->environment_ready)bindings_storage_census(&l->environment,c);
            cetta_storage_array(c,l->aggregate,l->aggregate_capacity,sizeof(*l->aggregate),0u);
            if(l->cursor_ready)space_occurrence_cursor_storage_census(&l->cursor,c);
        }
    }
}

void cetta_space_agenda_storage(const CettaSpaceAgenda *a,CettaStorageCensus *c) {
    for (const CettaSpaceAgenda *node=a;node;node=node->owned_next)agenda_storage_one(node,c);
}

struct CettaSpaceAgendaDiscovery {
    Space *space;
    Arena *owner;
    CettaSpaceAgendaChoiceKind kind;
    bool owned_capture;
    Bindings captured;
    SpaceReadToken source;
    SpaceOccurrenceCursor cursor;
    bool cursor_ready, exhausted, source_ready;
    CettaIndex source_index;
    SymbolId exec;
    CettaSpaceAgenda *preparing;
    CettaSpaceAgenda **ready;
    size_t count, capacity;
    const char *pending;
};

static CettaSpaceAgendaDiscovery *discovery_new(Space *space,
        Arena *owner, const Bindings *environment, bool owned, CettaSpaceAgendaChoiceKind kind) {
    if (!space || (!owner && !owned) || !environment || !g_symbols || space->overlay_base ||
        (kind!=CETTA_SPACE_CHOICE_TRANSACTION_COMMIT && kind!=CETTA_SPACE_CHOICE_MM2_ACTIVATION &&
         kind!=CETTA_SPACE_CHOICE_TRANSACTION_MATCH &&
         kind!=CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH) ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT)) return NULL;
    CettaSpaceAgendaDiscovery *d=calloc(1u,sizeof(*d));
    if (!d) return NULL;
    if (owned) {
        owner=agenda_arena_new();
        if (!owner) {free(d);return NULL;}
    }
    d->owned_capture=owned;d->kind=kind;
    d->space=space;d->owner=owner;d->source=space_read_token(space);
    d->exec=symbol_intern_cstr(g_symbols,"exec");
    if (!bindings_clone(&d->captured,environment) ||
        (owned && !bindings_promote_atoms_to_arena(&d->captured,owner))) {
        bindings_free(&d->captured);
        if (owned) {arena_free(owner);free(owner);}
        free(d);return NULL;
    }
    return d;
}

CettaSpaceAgendaDiscovery *cetta_space_agenda_discovery_new(Space *space,
        Arena *owner, const Bindings *environment) {
    return discovery_new(space,owner,environment,false,CETTA_SPACE_CHOICE_TRANSACTION_COMMIT);
}

CettaSpaceAgendaDiscovery *cetta_space_agenda_discovery_new_owned(Space *space,
        const Bindings *environment) {
    return cetta_space_agenda_discovery_new_owned_kind(space,environment,
        CETTA_SPACE_CHOICE_TRANSACTION_COMMIT);
}

CettaSpaceAgendaDiscovery *cetta_space_agenda_discovery_new_owned_kind(Space *space,
        const Bindings *environment, CettaSpaceAgendaChoiceKind kind) {
    return discovery_new(space,NULL,environment,true,kind);
}

const Bindings *cetta_space_agenda_discovery_captured(const CettaSpaceAgendaDiscovery *d) {
    return d ? &d->captured : NULL;
}

void cetta_space_agenda_discovery_free(CettaSpaceAgendaDiscovery *d) {
    if (!d) return;
    if (d->cursor_ready) space_occurrence_cursor_release(&d->cursor);
    cetta_space_agenda_free(d->preparing);
    for (size_t i=0u;i<d->count;i++) cetta_space_agenda_free(d->ready[i]);
    free(d->ready);bindings_free(&d->captured);
    if (d->owned_capture) {arena_free(d->owner);free(d->owner);}
    free(d);
}

bool cetta_space_agenda_discovery_current(const CettaSpaceAgendaDiscovery *d) {
    return d && space_read_token_is_current(d->source);
}

const char *cetta_space_agenda_discovery_pending(const CettaSpaceAgendaDiscovery *d) {
    return d ? d->pending : "DiscoveryOwner";
}

bool cetta_space_agenda_discovery_choice(const CettaSpaceAgendaDiscovery *d,
        uint64_t ticket, CettaSpaceAgendaChoice *view) {
    if (!d || !view || !ticket || ticket>d->count) return false;
    const CettaSpaceAgenda *a=d->ready[(size_t)ticket-1u];
    if (!a) return false;
    if (d->kind==CETTA_SPACE_CHOICE_MM2_ACTIVATION) {
        if (a->phase!=AGENDA_ACTIVATE || !a->selected) return false;
        *view=(CettaSpaceAgendaChoice){.kind=d->kind,.ticket=ticket,
            .source_index=a->selected_index,.source=a->selected,
            .current=space_read_token_is_current(a->selection_read)};
    } else {
        if (a->phase!=AGENDA_BUILD || a->row_count!=1u || !a->validating) return false;
        *view=(CettaSpaceAgendaChoice){.kind=d->kind,.ticket=ticket,.source_index=a->selected_index,
            .source=a->selected,.outputs=a->rows[0].outputs,
            .dependencies=a->rows[0].dependencies,
            .current=selected_occurrence_current(a) && space_read_token_is_current(a->capture_read)};
    }
    return true;
}

CettaSpaceAgendaSelection *cetta_space_agenda_discovery_take(CettaSpaceAgendaDiscovery *d,
        uint64_t ticket) {
    if (!d || !ticket || ticket>d->count) return NULL;
    CettaSpaceAgenda *a=d->ready[(size_t)ticket-1u];
    if (!a) return NULL;
    CettaSpaceAgendaSelection *selected=calloc(1u,sizeof(*selected));
    if (!selected) return NULL;
    selected->agenda=a;
    selected->kind=d->kind;
    a->borrowed_capture=false;
    d->ready[(size_t)ticket-1u]=NULL;
    return selected;
}

static bool discovery_retain(CettaSpaceAgendaDiscovery *d, CettaSpaceAgenda **proposal) {
    if (d->count==UINT64_MAX) return false;
    if (d->count==d->capacity) {
        size_t capacity=d->capacity ? d->capacity*2u : 8u;
        if (capacity<=d->capacity || capacity>SIZE_MAX/sizeof(*d->ready)) return false;
        CettaSpaceAgenda **ready=realloc(d->ready,capacity*sizeof(*ready));
        if (!ready) return false;
        d->ready=ready;d->capacity=capacity;
    }
    d->ready[d->count++]=*proposal;*proposal=NULL;
    return true;
}

static CettaSpaceAgendaDiscoveryStep discovery_prepare(CettaSpaceAgendaDiscovery *d) {
    Atom *source=space_occurrence_cursor_atom(&d->cursor,d->source_index);
    if (!source) {
        d->pending="DiscoveryProjection";return CETTA_SPACE_DISCOVERY_INCOMPLETE;
    }
    if (!shaped(source,d->exec,3u)) {
        d->source_ready=false;return CETTA_SPACE_DISCOVERY_PROGRESS;
    }
    if (!d->preparing) {
        bool raw=d->kind==CETTA_SPACE_CHOICE_MM2_ACTIVATION;
        d->preparing=agenda_new(d->space,d->owner,&d->captured,
            (CettaSpaceAgendaContract){.set_support=raw,.raw_mm2_order=raw,
                .explicit_inputs=!raw},d->owned_capture);
        if (d->preparing)
            d->preparing->borrowed_capture=!raw && d->space->kind==SPACE_KIND_ATOM &&
                !d->space->module_own_view && !d->space->dep_count;
        if (d->preparing)
            d->preparing->enumerate_matches=d->kind==CETTA_SPACE_CHOICE_TRANSACTION_MATCH ||
                d->kind==CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH;
    }
    if (!d->preparing) {
        d->pending="PreparationCapacity";return CETTA_SPACE_DISCOVERY_INCOMPLETE;
    }
    CettaSpaceAgendaStep result=cetta_space_agenda_prepare_source(
        d->preparing,d->source,d->source_index);
    if (d->preparing->phase!=AGENDA_SELECT) {
        d->source_ready=false;
    }
    if (!cetta_space_agenda_discovery_current(d)) {
        d->pending="StaleDiscoverySource";return CETTA_SPACE_DISCOVERY_INVALIDATED;
    }
    if (result==CETTA_SPACE_AGENDA_PENDING) {
        d->pending=d->preparing->pending;return CETTA_SPACE_DISCOVERY_INCOMPLETE;
    }
    return CETTA_SPACE_DISCOVERY_PROGRESS;
}

CettaSpaceAgendaDiscoveryStep cetta_space_agenda_discovery_step(
        CettaSpaceAgendaDiscovery *d, CettaSpaceAgendaChoice *view) {
    if (view) memset(view,0,sizeof(*view));
    if (!d || !view) return CETTA_SPACE_DISCOVERY_INCOMPLETE;
    CETTA_SCOPED_SHARED_TRANSITION(discovery);
    d->pending=NULL;
    if (!cetta_space_agenda_discovery_current(d)) {
        d->pending="StaleDiscoverySource";return CETTA_SPACE_DISCOVERY_INVALIDATED;
    }
    if (d->exhausted) return CETTA_SPACE_DISCOVERY_EXHAUSTED;
    if (d->source_ready) return discovery_prepare(d);
    if (d->preparing) {
        CettaSpaceAgenda *a=d->preparing;
        bool raw=d->kind==CETTA_SPACE_CHOICE_MM2_ACTIVATION;
        if (a->pending_child &&
            (d->kind==CETTA_SPACE_CHOICE_TRANSACTION_MATCH ||
             d->kind==CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH)) {
            CettaSpaceAgenda *child=a->pending_child;
            assert(a->root==a && a->owned_next==child && child->root==a &&
                child->phase==AGENDA_BUILD && child->selected_match);
            if (!discovery_retain(d,&a->pending_child)) {
                d->pending="ChoiceCapacity";return CETTA_SPACE_DISCOVERY_INCOMPLETE;
            }
            /* The retained matcher keeps its next binding. Its independently
             * captured row now leaves the temporary ownership graph. */
            a->owned_next=child->owned_next;child->owned_next=NULL;child->root=child;
            if (a->phase==AGENDA_SELECT && !a->selected) {
                cetta_space_agenda_free(a);d->preparing=NULL;
            }
            bool found=cetta_space_agenda_discovery_choice(d,d->count,view);
            assert(found);(void)found;
            return CETTA_SPACE_DISCOVERY_CHOICE;
        }
        if (raw ? a->phase==AGENDA_ACTIVATE : a->phase==AGENDA_BUILD) {
            if (!raw && (!a->validating || a->row_count!=1u || a->build_index)) {
                d->pending="UnpreparedChoice";return CETTA_SPACE_DISCOVERY_INCOMPLETE;
            }
            if (raw ? !space_read_token_is_current(a->selection_read) :
                !selected_occurrence_current(a) || !space_read_token_is_current(a->capture_read)) {
                d->pending="StalePreparedChoice";return CETTA_SPACE_DISCOVERY_INVALIDATED;
            }
            /* The proposal and epochs are operational state. Captured row
             * indices and write images are rebuilt only after selection. */
            if (a->snapshot_ready) {space_free(&a->snapshot);a->snapshot_ready=false;}
            if (a->current_ready) {space_free(&a->current);a->current_ready=false;}
            if (a->replacement_ready) {space_free(&a->replacement);a->replacement_ready=false;}
            if (!discovery_retain(d,&d->preparing)) {
                d->pending="ChoiceCapacity";return CETTA_SPACE_DISCOVERY_INCOMPLETE;
            }
            bool found=cetta_space_agenda_discovery_choice(d,d->count,view);
            assert(found);(void)found;
            return CETTA_SPACE_DISCOVERY_CHOICE;
        }
        if (raw) {
            d->pending="UnpreparedActivation";return CETTA_SPACE_DISCOVERY_INCOMPLETE;
        }
        if (a->phase==AGENDA_SINK || a->phase==AGENDA_COMMIT) {
            d->pending="PublicationDuringDiscovery";return CETTA_SPACE_DISCOVERY_INCOMPLETE;
        }
        CettaSpaceAgendaStep step=cetta_space_agenda_step(a);
        if (!cetta_space_agenda_discovery_current(d)) {
            d->pending="StaleDiscoverySource";return CETTA_SPACE_DISCOVERY_INVALIDATED;
        }
        if (step==CETTA_SPACE_AGENDA_PENDING) {
            d->pending=a->pending;return CETTA_SPACE_DISCOVERY_INCOMPLETE;
        }
        /* A complete unmatched Transaction returns to Select. Do not let
         * this provider choose another source occurrence on the next grant. */
        if (a->phase==AGENDA_SELECT && !a->selected && !a->pending_child) {
            cetta_space_agenda_free(a);d->preparing=NULL;
        }
        return CETTA_SPACE_DISCOVERY_PROGRESS;
    }
    if (!d->cursor_ready) {
        if (!space_occurrence_cursor_init_scan(d->space,&d->cursor)) {
            d->pending="DiscoveryCursor";return CETTA_SPACE_DISCOVERY_INCOMPLETE;
        }
        d->cursor_ready=true;
    }
    CettaIndex index;
    SpaceOccurrenceCursorStep step=space_occurrence_cursor_next(&d->cursor,&index,NULL);
    cetta_native_cost_charge(CETTA_COST_QUEUE_VISIT,1u);
    if (!cetta_space_agenda_discovery_current(d)) {
        d->pending="StaleDiscoverySource";return CETTA_SPACE_DISCOVERY_INVALIDATED;
    }
    if (step==SPACE_OCCURRENCE_CURSOR_END) {
        d->exhausted=true;return CETTA_SPACE_DISCOVERY_EXHAUSTED;
    }
    if (step!=SPACE_OCCURRENCE_CURSOR_ITEM) {
        d->pending="DiscoveryCursorChanged";return CETTA_SPACE_DISCOVERY_INVALIDATED;
    }
    d->source_ready=true;d->source_index=index;
    return CETTA_SPACE_DISCOVERY_PROGRESS;
}

static CettaSpaceAgendaStep selection_step_kernel(CettaSpaceAgendaSelection *s,
        const AgendaOperations *operations) {
    if (!s || !s->agenda) return CETTA_SPACE_AGENDA_PENDING;
    if (s->completed) return CETTA_SPACE_AGENDA_CLOSED;
    if (s->stale) return pending(s->agenda,s->pending);
    CETTA_SCOPED_SHARED_TRANSITION(selection);
    CettaSpaceAgenda *a=s->agenda;
    bool private_world=s->kind==CETTA_SPACE_CHOICE_TRANSACTION_WORLD_MATCH;
    if (private_world && !space_read_token_is_current(a->root->origin_read)) {
        s->stale=true;s->pending="StaleSnapshotSource";a->pending=s->pending;
        return CETTA_SPACE_AGENDA_PENDING;
    }
    /* Matching still uses the existing live validation contract. Only a
     * fully revalidated tuple may switch its publication destination. */
    if (private_world && s->started && a->phase==AGENDA_BUILD &&
        !a->contract.snapshot_worlds) {
        if (!agenda_world_init(a)) return pending(a,"WorldContinuationCapacity");
        a->parent_world_id=a->world_id;
        a->world_id=a->next_world_id++;
        a->contract.snapshot_worlds=true;
    }
    s->pending=NULL;
    uint64_t commits_before=a->commits;
    CettaSpaceAgendaStep result;
    if (!a->contract.explicit_inputs) {
        if (a->phase!=AGENDA_ACTIVATE || !a->selected ||
            !space_read_token_is_current(a->selection_read)) {
            s->stale=true;s->pending="StaleActivationSource";a->pending=s->pending;
            return CETTA_SPACE_AGENDA_PENDING;
        }
        s->started=true;
        result=operations->step(a);
        s->completed=a->phase!=AGENDA_ACTIVATE;
        if (result==CETTA_SPACE_AGENDA_PENDING) s->pending=a->pending;
        return result;
    }
    if (!s->started) {
        result=operations->revalidate_choice(a);
        s->started=a->phase!=AGENDA_BUILD;
    } else result=operations->step(a);
    if (result==CETTA_SPACE_AGENDA_COMMITTED ||
        (private_world && a->contract.snapshot_worlds && a->commits>commits_before)) {
        s->completed=true;s->receipt=cetta_space_agenda_receipt(a);
    }
    else if (!a->validating || a->row_count!=1u || !a->selected ||
        (a->selected_match && a->phase==AGENDA_WAIT)) {
        s->stale=true;s->pending="ChangedPreparedChoice";a->pending=s->pending;
        return CETTA_SPACE_AGENDA_PENDING;
    } else if (result==CETTA_SPACE_AGENDA_PENDING) s->pending=a->pending;
    return result;
}

CettaSpaceAgendaStep cetta_space_agenda_selection_step(CettaSpaceAgendaSelection *s) {
    if (!s || s->leased) return CETTA_SPACE_AGENDA_PENDING;
    return selection_step_kernel(s,agenda_observation
        ? &agenda_recorded_operations : &agenda_plain_operations);
}

bool cetta_space_agenda_selection_enter(CettaSpaceAgendaSelectionScope *scope,
        CettaSpaceAgendaSelection *s) {
    if (!scope || !s || !s->agenda) return false;
    CETTA_SCOPED_SHARED_TRANSITION(selection);
    if (s->leased) return false;
    for (CettaSpaceAgendaSelectionScope *held=agenda_selection;held;held=held->previous)
        if (held==scope) return false;
    *scope=(CettaSpaceAgendaSelectionScope){.selection=s,.previous=agenda_selection,.entered=true};
    s->leased=true;agenda_selection=scope;agenda_operations_refresh();
    return true;
}
void cetta_space_agenda_selection_leave(CettaSpaceAgendaSelectionScope *scope) {
    if (!scope || !scope->entered) return;
    CETTA_SCOPED_SHARED_TRANSITION(selection);
    assert(agenda_selection==scope && scope->selection->leased);
    agenda_selection=scope->previous;scope->selection->leased=false;
    scope->entered=false;scope->previous=NULL;agenda_operations_refresh();
}
CettaSpaceAgenda *cetta_space_agenda_selection_provider(CettaSpaceAgendaSelection *s) {
    return s ? s->agenda : NULL;
}

const char *cetta_space_agenda_selection_pending(const CettaSpaceAgendaSelection *s) {
    return s ? s->pending : "SelectionOwner";
}
Atom *cetta_space_agenda_selection_receipt(CettaSpaceAgendaSelection *s) {
    return s ? s->receipt : NULL;
}
void cetta_space_agenda_selection_free(CettaSpaceAgendaSelection *s) {
    if (!s) return;
    assert(!s->leased);
    cetta_space_agenda_free(s->agenda);free(s);
}
void cetta_space_agenda_selection_storage(const CettaSpaceAgendaSelection *s,
        CettaStorageCensus *c) {
    if (!s || !cetta_storage_object(c,s,sizeof(*s),CETTA_STORAGE_BLOCK)) return;
    cetta_space_agenda_storage(s->agenda,c);
}

void cetta_space_agenda_discovery_storage(const CettaSpaceAgendaDiscovery *d,
        CettaStorageCensus *c) {
    if (!d || !cetta_storage_object(c,d,sizeof(*d),CETTA_STORAGE_BLOCK)) return;
    if (d->owned_capture)arena_storage_census(d->owner,c);
    bindings_storage_census(&d->captured,c);
    cetta_storage_array(c,d->ready,d->capacity,sizeof(*d->ready),0u);
    if (d->cursor_ready) space_occurrence_cursor_storage_census(&d->cursor,c);
    cetta_space_agenda_storage(d->preparing,c);
    for (size_t i=0u;i<d->count;i++) cetta_space_agenda_storage(d->ready[i],c);
}

/* Exact boundary images for the named explicit-input serial contract. */
static const char agenda_checkpoint_shell_kind, agenda_checkpoint_complete_kind;
static const char agenda_checkpoint_world_kind;

static bool agenda_checkpoint_refuse(SpaceCheckpointBudget *budget,
                                      const char *reason) {
    if (budget && !budget->refusal) budget->refusal = reason;
    return false;
}

static bool agenda_checkpoint_work(SpaceCheckpointBudget *budget, size_t n) {
    if (!budget || budget->refusal) return false;
    if (budget->work > budget->work_limit || n > budget->work_limit-budget->work)
        return agenda_checkpoint_refuse(budget,"AgendaInspectionLimit");
    budget->work += n;
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,n);
    return true;
}

static bool agenda_checkpoint_array_admitted(const void *items, size_t length,
        size_t capacity, size_t width, SpaceCheckpointBudget *budget) {
    if (!agenda_checkpoint_work(budget,1u)) return false;
    if (!width || length > capacity || capacity > SIZE_MAX/width ||
        ((capacity != 0u) != (items != NULL)))
        return agenda_checkpoint_refuse(budget,"AgendaArrayShape");
    return true;
}

/* The source header is held by the caller; historical tokens never supply
 * a second, possibly expired header to dereference. */
static bool agenda_checkpoint_token_admitted(const CettaSpaceAgenda *a,
        SpaceReadToken token, SpaceCheckpointBudget *budget) {
    if (!agenda_checkpoint_work(budget,1u)) return false;
    if (!token.space) {
        if (token.instance_id || token.revision || token.prefix_epoch ||
            token.base_prefix_epoch || token.base_dependency_epoch)
            return agenda_checkpoint_refuse(budget,"AgendaEmptyReadToken");
        return true;
    }
    if (token.space != a->space || token.instance_id != a->space_identity ||
        !token.prefix_epoch || token.prefix_epoch > a->space->prefix_epoch ||
        token.revision > a->space->revision || token.base_prefix_epoch ||
        token.base_dependency_epoch)
        return agenda_checkpoint_refuse(budget,"AgendaReadTokenAuthority");
    return true;
}

static bool agenda_checkpoint_cursor_empty(const SpaceOccurrenceCursor *c,
                                          SpaceCheckpointBudget *budget) {
    if (!agenda_checkpoint_work(budget,1u)) return false;
    if (c->read.space || c->read.instance_id || c->read.revision ||
        c->read.prefix_epoch || c->read.base_prefix_epoch || c->read.base_dependency_epoch ||
        c->prefix_epoch || c->ceiling || c->space || c->occurrences || c->nodes ||
        c->leaf_pos || c->node_len || c->flat || c->flat_capacity || c->flat_len ||
        c->flat_next || c->flat_mode || c->full_scan || c->full_next || c->pinned ||
        c->logical_scan || c->logical_reads.root.space ||
        c->logical_reads.root.instance_id || c->logical_reads.root.revision ||
        c->logical_reads.root.prefix_epoch || c->logical_reads.root.base_prefix_epoch ||
        c->logical_reads.root.base_dependency_epoch || c->logical_reads.bases ||
        c->logical_reads.base_count)
        return agenda_checkpoint_refuse(budget,"AgendaInactiveSelection");
    return true;
}

bool cetta_space_agenda_checkpoint_admitted(const CettaSpaceAgenda *a,
                                           SpaceCheckpointBudget *budget) {
    if (!agenda_checkpoint_work(budget,1u)) return false;
    if (!a || !a->space || !a->owner || a->root != a || a->owned_next ||
        a->pending_child || a->yielded_child || a->uninterpreted_choice ||
        a->enumerate_matches || a->selected_match ||
        a->space_identity != a->space->instance_id ||
        (a->world_ready && a->space != &a->world) ||
        (!a->world_ready && a->space == &a->world))
        return agenda_checkpoint_refuse(budget,"AgendaBoundaryOwner");
    if (!a->contract.explicit_inputs || a->contract.set_support ||
        a->contract.raw_mm2_order || a->contract.snapshot_worlds ||
        a->advise || a->before || a->advice_context || a->chosen_advice ||
        a->trace || a->reported_atoms || a->reported_count || a->reported_capacity)
        return agenda_checkpoint_refuse(budget,"AgendaBoundaryProfile");
    if (a->phase != AGENDA_SELECT || a->selection_ready || a->selected ||
        a->owned_arena ||
        a->borrowed_capture ||
        a->matching || a->snapshot_ready || a->current_ready || a->replacement_ready ||
        a->inputs || a->input_count || a->levels || a->join_ready ||
        a->rows || a->row_count || a->row_capacity || a->validation ||
        a->validation_count || a->validation_capacity || a->validating ||
        a->selected_key || a->selected_key_length)
        return agenda_checkpoint_refuse(budget,"UnrestorableCut");
    for (size_t i=0u;i<2u;i++) {
        if (!agenda_checkpoint_work(budget,1u)) return false;
        if (a->match_epochs[i] || a->match_epoch_count[i] || a->match_epoch_capacity[i])
            return agenda_checkpoint_refuse(budget,"UnrestorableCut");
    }
    if (!agenda_checkpoint_cursor_empty(&a->selection,budget)) return false;
    if (a->schedule.state_count > 1u || a->schedule.state ||
        (a->schedule.state_count && (a->schedule.lane[0] != CETTA_SELECTION_LANE_OLDEST ||
                                    a->schedule.next[0])))
        return agenda_checkpoint_refuse(budget,"AgendaFifoProfile");
    if (!agenda_checkpoint_array_admitted(a->waiting,a->waiting_count,
            a->waiting_capacity,sizeof(*a->waiting),budget) ||
        !agenda_checkpoint_array_admitted(a->candidates,a->candidate_count,
            a->candidate_capacity,sizeof(*a->candidates),budget) ||
        !agenda_checkpoint_token_admitted(a,a->origin_read,budget) ||
        !agenda_checkpoint_token_admitted(a,a->selected_read,budget) ||
        !agenda_checkpoint_token_admitted(a,a->capture_read,budget) ||
        !agenda_checkpoint_token_admitted(a,a->selection_read,budget)) return false;
    for (size_t i=0u;i<a->waiting_count;i++)
        if (!agenda_checkpoint_token_admitted(a,a->waiting[i].read,budget)) return false;
    for (size_t i=0u;i<a->candidate_count;i++) {
        if (!agenda_checkpoint_work(budget,1u)) return false;
        if (a->candidates[i].advice)
            return agenda_checkpoint_refuse(budget,"AgendaCandidateAdvice");
    }
    return space_checkpoint_native_admitted(a->space,budget);
}

static void *agenda_checkpoint_alloc(size_t capacity, size_t width,
                                    SpaceCheckpointBudget *budget) {
    if (!capacity) return NULL;
    if (!agenda_checkpoint_work(budget,1u)) return NULL;
    if (!width || capacity > SIZE_MAX/width ||
        budget->retained_bytes > budget->byte_limit ||
        capacity*width > budget->byte_limit-budget->retained_bytes ||
        budget->allocation_attempts == SIZE_MAX ||
        capacity*width > SIZE_MAX-budget->attempted_bytes) {
        agenda_checkpoint_refuse(budget,"AgendaRetainedBytes");
        return NULL;
    }
    budget->allocation_attempts++;
    budget->attempted_bytes += capacity*width;
    void *result = calloc(capacity,width);
    if (!result) {
        agenda_checkpoint_refuse(budget,"AgendaAllocation");
        return NULL;
    }
    budget->retained_bytes += capacity*width;
    return result;
}

static bool agenda_checkpoint_atom(AtomDeepCopySession *atoms, Atom *source,
        Atom **image, SpaceCheckpointBudget *budget) {
    if (!agenda_checkpoint_work(budget,1u)) return false;
    *image = source ? atom_deep_copy_session_copy(atoms,source) : NULL;
    return !source || *image || agenda_checkpoint_refuse(budget,"AgendaAtomOwner");
}

static SpaceReadToken agenda_checkpoint_token_copy(const CettaSpaceAgenda *a,
        const CettaSpaceAgenda *b, SpaceReadToken token) {
    if (!token.space) return token;
    token.space = b->space;
    token.instance_id = b->space_identity;
    if (token.prefix_epoch == a->space->prefix_epoch)
        token.prefix_epoch = b->space->prefix_epoch;
    return token;
}

const CettaSpaceAgenda *cetta_space_agenda_checkpoint_lookup(
        const AtomDeepCopySession *atoms, const CettaSpaceAgenda *source) {
    return atom_deep_copy_session_object_forwarded(atoms,
        &agenda_checkpoint_complete_kind,source,sizeof(*source));
}

Space *cetta_space_agenda_checkpoint_target(const CettaSpaceAgenda *source) {
    return source ? source->space : NULL;
}

bool cetta_space_agenda_checkpoint_owns_target(const CettaSpaceAgenda *source) {
    return source && source->world_ready && source->space == &source->world;
}

bool cetta_space_agenda_checkpoint_regions(const CettaSpaceAgenda *a,
    const Space *regions[CETTA_SPACE_AGENDA_REGION_COUNT]) {
    if(!a || !regions || !a->space || a->root!=a)return false;
    regions[0]=a->space;regions[1]=&a->snapshot;regions[2]=&a->current;regions[3]=&a->replacement;
    return true;
}

bool cetta_space_agenda_checkpoint_copy(const CettaSpaceAgenda *a,
        Arena *destination, AtomDeepCopySession *atoms,
        CettaSpaceAgenda **out, SpaceCheckpointBudget *budget) {
    if (!out || *out || !destination || !atoms)
        return agenda_checkpoint_refuse(budget,"AgendaImageOwner");
    cetta_native_cost_charge(CETTA_COST_CAPTURE,1u);
    if (!cetta_space_agenda_checkpoint_admitted(a,budget)) return false;
    Arena *mapped = NULL;
    if (!atom_deep_copy_session_destination_for_owner(atoms,a->owner,&mapped) ||
        mapped != destination ||
        atom_deep_copy_session_object_forwarded(atoms,&agenda_checkpoint_shell_kind,a,sizeof(*a)) ||
        atom_deep_copy_session_object_forwarded(atoms,&agenda_checkpoint_world_kind,a->space,sizeof(*a->space)))
        return agenda_checkpoint_refuse(budget,"AgendaImageRelation");
    SpaceReadToken source_read = space_read_token(a->space);
    CettaSpaceAgenda *b = agenda_checkpoint_alloc(1u,sizeof(*b),budget);
    if (!b) return false;
    *out = b;
    b->root = b;
    b->owner = destination;
    bindings_init(&b->captured);
    if (!atom_deep_copy_session_object_record(atoms,&agenda_checkpoint_shell_kind,a,b,sizeof(*a)) ||
        !atom_deep_copy_session_object_record(atoms,&agenda_checkpoint_world_kind,a->space,&b->world,sizeof(*a->space)))
        return agenda_checkpoint_refuse(budget,"AgendaImageRelation");
    /* Header storage is already included in the agenda allocation. Give the
     * world primitive that exact inline credit, restoring it if init refuses. */
    budget->retained_bytes -= sizeof(b->world);
    bool world_complete = space_checkpoint_native_copy(a->space,&b->world,
                                                       &b->world_ready,budget);
    if (!b->world_ready) budget->retained_bytes += sizeof(b->world);
    if (!world_complete) return false;
    b->space = &b->world;
    b->space_identity = b->world.instance_id;
    b->contract = a->contract;
    b->phase = a->phase;
    b->world_id = a->world_id;
    b->parent_world_id = a->parent_world_id;
    b->next_world_id = a->next_world_id;
    b->exec=a->exec; b->comma=a->comma; b->input=a->input; b->output=a->output;
    b->btm=a->btm; b->read=a->read; b->take=a->take; b->all=a->all;
    b->absent=a->absent; b->plus=a->plus; b->minus=a->minus;
    b->selected_index=a->selected_index;
    b->snapshot_exec_appended=a->snapshot_exec_appended;
    b->current_exec_appended=a->current_exec_appended;
    b->capture_index=a->capture_index; b->capture_length=a->capture_length;
    b->depth=a->depth; b->build_index=a->build_index;
    b->sink_row=a->sink_row; b->sink_index=a->sink_index;
    b->commits=a->commits; b->retries=a->retries; b->pending=a->pending;
    b->ranking=a->ranking; b->best=a->best; b->schedule=a->schedule; b->oldest=a->oldest;
    b->origin_read=agenda_checkpoint_token_copy(a,b,a->origin_read);
    b->selected_read=agenda_checkpoint_token_copy(a,b,a->selected_read);
    b->capture_read=agenda_checkpoint_token_copy(a,b,a->capture_read);
    b->selection_read=agenda_checkpoint_token_copy(a,b,a->selection_read);
    if (!agenda_checkpoint_work(budget,1u) ||
        !bindings_clone(&b->captured,&a->captured) ||
        !bindings_copy_owned_atoms_with_session(&b->captured,destination,atoms))
        return agenda_checkpoint_refuse(budget,"AgendaBindingsOwner");
    if (!agenda_checkpoint_atom(atoms,a->wildcard,&b->wildcard,budget) ||
        !agenda_checkpoint_atom(atoms,a->last_selected,&b->last_selected,budget) ||
        !agenda_checkpoint_atom(atoms,a->sinks,&b->sinks,budget) ||
        !agenda_checkpoint_atom(atoms,a->receipt,&b->receipt,budget)) return false;
    if (a->waiting_capacity) {
        b->waiting=agenda_checkpoint_alloc(a->waiting_capacity,sizeof(*b->waiting),budget);
        if (!b->waiting) return false;
        b->waiting_capacity=a->waiting_capacity;
    }
    for (size_t i=0u;i<a->waiting_count;i++) {
        if (!agenda_checkpoint_work(budget,1u)) return false;
        b->waiting[i].read=agenda_checkpoint_token_copy(a,b,a->waiting[i].read);
        b->waiting[i].index=a->waiting[i].index;
        if (!agenda_checkpoint_atom(atoms,a->waiting[i].exec,&b->waiting[i].exec,budget)) return false;
        b->waiting_count++;
    }
    if (a->candidate_capacity) {
        b->candidates=agenda_checkpoint_alloc(a->candidate_capacity,sizeof(*b->candidates),budget);
        if (!b->candidates) return false;
        b->candidate_capacity=a->candidate_capacity;
    }
    for (size_t i=0u;i<a->candidate_count;i++) {
        if (!agenda_checkpoint_work(budget,1u)) return false;
        b->candidates[i].index=a->candidates[i].index;
        if (!agenda_checkpoint_atom(atoms,a->candidates[i].exec,&b->candidates[i].exec,budget)) return false;
        b->candidate_count++;
    }
    if (!space_read_token_is_current(source_read))
        return agenda_checkpoint_refuse(budget,"AgendaSourceChanged");
    if (!atom_deep_copy_session_object_record(atoms,&agenda_checkpoint_complete_kind,a,b,sizeof(*a)))
        return agenda_checkpoint_refuse(budget,"AgendaImageCompletion");
    return true;
}

#define AC_S(name,x,y) do { if (!read->scalar(read->context,name,(uint64_t)(x),(uint64_t)(y))) return false; } while (0)
#define AC_P(name,x,y) do { if (!read->address(read->context,name,(x),(y))) return false; } while (0)
#define AC_F(name,x,y) do { if (!read->physical(read->context,name,(uint64_t)(x),(uint64_t)(y))) return false; } while (0)
#define AC_I(name,role,x,y) do { if (!read->identity(read->context,name,role,(x),(y))) return false; } while (0)
#define AC_A(name,x,y) do { if (!read->atom(read->context,name,(x),(y))) return false; } while (0)

static bool agenda_checkpoint_token_compare(const char *site,
        SpaceReadToken a, SpaceReadToken b, const CettaCheckpointCompare *read) {
    if (!cetta_checkpoint_target(read,site,a.space,a.instance_id,b.space,b.instance_id)) return false;
    AC_I("agenda.token.instance",CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE,a.instance_id,b.instance_id);
    AC_S("agenda.token.revision",a.revision,b.revision);
    AC_I("agenda.token.prefix",CETTA_CHECKPOINT_TARGET_SPACE_PREFIX,a.prefix_epoch,b.prefix_epoch);
    AC_S("agenda.token.base_prefix",a.base_prefix_epoch,b.base_prefix_epoch);
    AC_S("agenda.token.base_dependency",a.base_dependency_epoch,b.base_dependency_epoch);
    return true;
}

bool cetta_space_agenda_checkpoint_read_token(const char *field,SpaceReadToken left,
        SpaceReadToken right,const CettaCheckpointCompare *read) {
    return cetta_checkpoint_compare_valid(read)&&agenda_checkpoint_token_compare(field,left,right,read);
}
bool cetta_space_agenda_checkpoint_compare(const CettaSpaceAgenda *a,
        const CettaSpaceAgenda *b, const AtomDeepCopySession *borrowed_origin,
        const CettaCheckpointCompare *read, SpaceCheckpointBudget *budget) {
    if (!cetta_checkpoint_compare_valid(read) ||
        !cetta_space_agenda_checkpoint_admitted(a,budget) ||
        !cetta_space_agenda_checkpoint_admitted(b,budget)) return false;
    if (borrowed_origin ? (a->world_ready || !b->world_ready) : (a->world_ready != b->world_ready))
        return agenda_checkpoint_refuse(budget,"AgendaLifetimeMode");
    if (borrowed_origin) {
        Arena *mapped = NULL;
        if (cetta_space_agenda_checkpoint_lookup(borrowed_origin,a) != b ||
            atom_deep_copy_session_object_forwarded(borrowed_origin,
                &agenda_checkpoint_world_kind,a->space,sizeof(*a->space)) != b->space ||
            !atom_deep_copy_session_destination_for_owner(borrowed_origin,a->owner,&mapped) ||
            mapped != b->owner)
            return agenda_checkpoint_refuse(budget,"AgendaOriginWitness");
    }
    AC_P("agenda",a,b); AC_P("agenda.root",a->root,b->root);
    AC_P("agenda.owned_next",a->owned_next,b->owned_next);
    AC_P("agenda.pending_child",a->pending_child,b->pending_child);
    AC_S("agenda.borrowed_origin",borrowed_origin!=NULL,borrowed_origin!=NULL);
    AC_F("agenda.world_ready",a->world_ready,b->world_ready);
    AC_S("agenda.yielded_child",a->yielded_child,b->yielded_child);
    AC_S("agenda.uninterpreted_choice",a->uninterpreted_choice,b->uninterpreted_choice);
    AC_S("agenda.enumerate_matches",a->enumerate_matches,b->enumerate_matches);
    AC_S("agenda.selected_match",a->selected_match,b->selected_match);
    AC_S("agenda.world_id",a->world_id,b->world_id);
    AC_S("agenda.parent_world_id",a->parent_world_id,b->parent_world_id);
    AC_S("agenda.next_world_id",a->next_world_id,b->next_world_id);
    AC_A("agenda.trace",a->trace,b->trace);
    AC_P("agenda.reported_atoms",a->reported_atoms,b->reported_atoms);
    AC_S("agenda.reported_count",a->reported_count,b->reported_count);
    AC_F("agenda.reported_capacity",a->reported_capacity,b->reported_capacity);
    AC_P("agenda.chosen_advice",a->chosen_advice,b->chosen_advice);
    if (!cetta_checkpoint_target(read,"agenda.space",a->space,a->space_identity,
            b->space,b->space_identity)) return false;
    AC_I("agenda.space_identity",CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE,a->space_identity,b->space_identity);
    CettaCheckpointCompare world_read = *read;
    if (!read->arena(read->context,"agenda.owner",a->owner,b->owner) ||
        !bindings_checkpoint_compare(&a->captured,&b->captured,read) ||
        !space_checkpoint_native_compare(a->space,b->space,&world_read,budget)) return false;
    AC_S("agenda.explicit_inputs",a->contract.explicit_inputs,b->contract.explicit_inputs);
    AC_S("agenda.set_support",a->contract.set_support,b->contract.set_support);
    AC_S("agenda.raw_mm2_order",a->contract.raw_mm2_order,b->contract.raw_mm2_order);
    AC_S("agenda.snapshot_worlds",a->contract.snapshot_worlds,b->contract.snapshot_worlds);
    AC_S("agenda.phase",a->phase,b->phase);
#define AC_MEMBER(f) AC_S("agenda." #f,a->f,b->f)
    AC_MEMBER(exec); AC_MEMBER(comma); AC_MEMBER(input); AC_MEMBER(output);
    AC_MEMBER(btm); AC_MEMBER(read); AC_MEMBER(take); AC_MEMBER(all);
    AC_MEMBER(absent); AC_MEMBER(plus); AC_MEMBER(minus);
    AC_A("agenda.wildcard",a->wildcard,b->wildcard);
    AC_MEMBER(selection_ready);
    /* Admission checked every member of this released zero cursor. */
    AC_P("agenda.selection.space",a->selection.space,b->selection.space);
    AC_A("agenda.selected",a->selected,b->selected);
    AC_A("agenda.last_selected",a->last_selected,b->last_selected);
    AC_MEMBER(selected_index);
    AC_P("agenda.selected_key",a->selected_key,b->selected_key);
    AC_MEMBER(selected_key_length);
    AC_MEMBER(snapshot_ready); AC_MEMBER(current_ready); AC_MEMBER(replacement_ready);
    AC_MEMBER(snapshot_exec_appended); AC_MEMBER(current_exec_appended);
    AC_MEMBER(capture_index); AC_MEMBER(capture_length);
    AC_P("agenda.inputs",a->inputs,b->inputs); AC_MEMBER(input_count);
    AC_A("agenda.sinks",a->sinks,b->sinks);
    AC_P("agenda.levels",a->levels,b->levels); AC_MEMBER(depth); AC_MEMBER(join_ready);
    AC_P("agenda.rows",a->rows,b->rows); AC_MEMBER(row_count); AC_MEMBER(row_capacity);
    AC_P("agenda.validation",a->validation,b->validation);
    AC_MEMBER(validation_count); AC_MEMBER(validation_capacity);
    for (size_t i=0u;i<2u;i++) {
        AC_S("agenda.epoch_bank",i,i);
        AC_P("agenda.match_epochs",a->match_epochs[i],b->match_epochs[i]);
        AC_S("agenda.match_epoch_count",a->match_epoch_count[i],b->match_epoch_count[i]);
        AC_F("agenda.match_epoch_capacity",a->match_epoch_capacity[i],b->match_epoch_capacity[i]);
    }
    AC_MEMBER(validating); AC_MEMBER(build_index); AC_MEMBER(sink_row); AC_MEMBER(sink_index);
    AC_MEMBER(commits); AC_MEMBER(retries);
    AC_A("agenda.receipt",a->receipt,b->receipt);
    /* Reasons are static constants of this normally returning provider. */
    AC_S("agenda.pending_static",(uintptr_t)a->pending,(uintptr_t)b->pending);
    if (!agenda_checkpoint_token_compare("agenda.origin_read",a->origin_read,b->origin_read,read) ||
        !agenda_checkpoint_token_compare("agenda.selected_read",a->selected_read,b->selected_read,read) ||
        !agenda_checkpoint_token_compare("agenda.capture_read",a->capture_read,b->capture_read,read) ||
        !agenda_checkpoint_token_compare("agenda.selection_read",a->selection_read,b->selection_read,read)) return false;
    if (a->waiting_count != b->waiting_count || a->waiting_capacity != b->waiting_capacity ||
        a->candidate_count != b->candidate_count || a->candidate_capacity != b->candidate_capacity)
        return agenda_checkpoint_refuse(budget,"AgendaArrayExtent");
    AC_P("agenda.waiting",a->waiting,b->waiting);
    AC_MEMBER(waiting_count); AC_F("agenda.waiting_capacity",a->waiting_capacity,b->waiting_capacity);
    for (size_t i=0u;i<a->waiting_count;i++) {
        AC_S("agenda.waiting_row",i,i);
        if (!agenda_checkpoint_token_compare("agenda.waiting.read",a->waiting[i].read,b->waiting[i].read,read)) return false;
        AC_S("agenda.waiting.index",a->waiting[i].index,b->waiting[i].index);
        AC_A("agenda.waiting.exec",a->waiting[i].exec,b->waiting[i].exec);
    }
    AC_P("agenda.candidates",a->candidates,b->candidates);
    AC_MEMBER(candidate_count); AC_F("agenda.candidate_capacity",a->candidate_capacity,b->candidate_capacity);
    for (size_t i=0u;i<a->candidate_count;i++) {
        AC_S("agenda.candidate_row",i,i);
        AC_S("agenda.candidate.index",a->candidates[i].index,b->candidates[i].index);
        AC_A("agenda.candidate.exec",a->candidates[i].exec,b->candidates[i].exec);
        AC_P("agenda.candidate.advice",a->candidates[i].advice,b->candidates[i].advice);
    }
    AC_MEMBER(ranking); AC_MEMBER(best);
    AC_S("agenda.schedule.state_count",a->schedule.state_count,b->schedule.state_count);
    AC_S("agenda.schedule.state",a->schedule.state,b->schedule.state);
    for (size_t i=0u;i<CETTA_SELECTION_AUTOMATON_STATE_CAPACITY;i++) {
        AC_S("agenda.schedule.slot",i,i);
        AC_S("agenda.schedule.lane",a->schedule.lane[i],b->schedule.lane[i]);
        AC_S("agenda.schedule.next",a->schedule.next[i],b->schedule.next[i]);
    }
    /* The admission predicate proves these service pointers are all NULL. */
    AC_S("agenda.advise_present",a->advise!=NULL,b->advise!=NULL);
    AC_S("agenda.before_present",a->before!=NULL,b->before!=NULL);
    AC_P("agenda.advice_context",a->advice_context,b->advice_context);
    AC_MEMBER(oldest); AC_P("agenda.matching",a->matching,b->matching);
#undef AC_MEMBER
    return true;
}
#undef AC_S
#undef AC_P
#undef AC_F
#undef AC_I
#undef AC_A

typedef enum {
    AGENDA_WORD_SCALAR, AGENDA_WORD_PHYSICAL, AGENDA_WORD_IDENTITY,
    AGENDA_WORD_ATOM, AGENDA_WORD_BINDINGS, AGENDA_WORD_BYTES, AGENDA_WORD_TOKEN
} AgendaWordKind;
typedef struct {
    Bindings source_pin, image;
    /* These source locations are compared only as opaque labels after the
     * synchronous event. The actual backing is held by source_pin. */
    const Bindings *source_location;
    const void *source_occurrence;
    size_t event;
    bool source_ready, image_ready;
} AgendaWordBindings;
typedef struct {
    const char *field;
    AgendaWordKind kind;
    CettaCheckpointIdentityRole role;
    size_t length;
    union {
        uint64_t scalar;
        Atom *atom;
        AgendaWordBindings *bindings;
        void *bytes;
        SpaceReadToken token;
    } value;
} AgendaWordCell;
typedef struct {
    size_t end;
    CettaSpaceAgendaEventKind kind;
    uint64_t committed;
    bool post_reserved;
} AgendaWordEvent;
struct CettaSpaceAgendaTranscript {
    Arena *owner;
    AtomDeepCopySession *atoms;
    AgendaWordCell *cells;
    AgendaWordEvent *boundaries;
    size_t length, capacity, events, event_limit, completed_events;
    size_t direct_bytes;
    uint64_t committed;
    CettaFrameIdentityScope frame_holds;
    const char *refusal;
    bool finished, post_reserved;
};

static bool agenda_word_refuse(CettaSpaceAgendaTranscript *t,
                               SpaceCheckpointBudget *budget, const char *why) {
    if (t && !t->refusal) t->refusal = why;
    return agenda_checkpoint_refuse(budget,why);
}

static void *agenda_word_alloc(CettaSpaceAgendaTranscript *t, size_t bytes,
                               SpaceCheckpointBudget *budget) {
    void *p = agenda_checkpoint_alloc(1u,bytes,budget);
    if (!p) {
        if (t && !t->refusal) t->refusal = budget && budget->refusal
            ? budget->refusal : "TranscriptAllocation";
        return NULL;
    }
    if (t) t->direct_bytes += bytes; /* allocation already checked total bound */
    return p;
}

size_t cetta_space_agenda_transcript_cell_capacity(size_t bytes,size_t events) {
    if(!events||bytes<sizeof(CettaSpaceAgendaTranscript)||
        events>(bytes-sizeof(CettaSpaceAgendaTranscript))/sizeof(AgendaWordEvent))return 0u;
    size_t remaining=bytes-sizeof(CettaSpaceAgendaTranscript)-events*sizeof(AgendaWordEvent);
    size_t cells=remaining/(2u*sizeof(AgendaWordCell));
    return cells>=events?cells:0u;
}
CettaSpaceAgendaTranscript *cetta_space_agenda_transcript_new(Arena *destination,
        AtomDeepCopySession *owned_session, size_t cell_limit, size_t event_limit,
        SpaceCheckpointBudget *budget) {
    if (!destination || !owned_session || !cell_limit || !event_limit ||
        cell_limit > SIZE_MAX/sizeof(AgendaWordCell) ||
        event_limit > SIZE_MAX/sizeof(AgendaWordEvent) || event_limit > cell_limit)
        return NULL;
    CettaSpaceAgendaTranscript *t = agenda_word_alloc(NULL,sizeof(*t),budget);
    if (!t) return NULL; /* Caller still owns the session. */
    t->owner=destination; t->atoms=owned_session; t->event_limit=event_limit;
    t->direct_bytes=sizeof(*t);
    t->cells=agenda_word_alloc(t,cell_limit*sizeof(*t->cells),budget);
    if (t->cells) t->capacity=cell_limit;
    if (t->cells) t->boundaries=agenda_word_alloc(t,
        event_limit*sizeof(*t->boundaries),budget);
    return t; /* Includes a disposable partial owner on cell allocation refusal. */
}

static AgendaWordCell *agenda_word_cell(CettaSpaceAgendaTranscript *t,
        const char *field, AgendaWordKind kind, SpaceCheckpointBudget *budget) {
    if (!t || t->refusal || t->finished || !t->atoms || !field) return NULL;
    if (!agenda_checkpoint_work(budget,1u)) {
        agenda_word_refuse(t,budget,"TranscriptInspectionLimit"); return NULL;
    }
    if (t->length==t->capacity) {
        agenda_word_refuse(t,budget,"TranscriptCellCapacity"); return NULL;
    }
    AgendaWordCell *cell=&t->cells[t->length++];
    *cell=(AgendaWordCell){.field=field,.kind=kind};
    return cell;
}

static bool agenda_word_scalar(CettaSpaceAgendaTranscript *t, const char *field,
        uint64_t value, bool physical, SpaceCheckpointBudget *budget) {
    AgendaWordCell *cell=agenda_word_cell(t,field,
        physical?AGENDA_WORD_PHYSICAL:AGENDA_WORD_SCALAR,budget);
    if (!cell) return false;
    cell->value.scalar=value; return true;
}

static bool agenda_word_identity(CettaSpaceAgendaTranscript *t, const char *field,
        CettaCheckpointIdentityRole role, uint64_t value, SpaceCheckpointBudget *budget) {
    AgendaWordCell *cell=agenda_word_cell(t,field,AGENDA_WORD_IDENTITY,budget);
    if (!cell) return false;
    cell->role=role; cell->value.scalar=value;
    if (role==CETTA_CHECKPOINT_FRAME && value &&
        !cetta_frame_identity_scope_retain(&t->frame_holds,(CettaFrameIdentity){value}))
        return agenda_word_refuse(t,budget,"TranscriptFrameOwner");
    return true;
}

static bool agenda_word_atom(CettaSpaceAgendaTranscript *t, const char *field,
        Atom *value, SpaceCheckpointBudget *budget) {
    AgendaWordCell *cell=agenda_word_cell(t,field,AGENDA_WORD_ATOM,budget);
    if (!cell) return false;
    cell->value.atom=value?atom_deep_copy_session_copy(t->atoms,value):NULL;
    return !value || cell->value.atom || agenda_word_refuse(t,budget,"TranscriptAtomOwner");
}

static bool agenda_word_bindings(CettaSpaceAgendaTranscript *t, const char *field,
        const Bindings *value, SpaceCheckpointBudget *budget) {
    AgendaWordCell *cell=agenda_word_cell(t,field,AGENDA_WORD_BINDINGS,budget);
    if (!cell || !value) return cell!=NULL;
    if (!t->events) return agenda_word_refuse(t,budget,"TranscriptBindingsEvent");
    size_t ordinal=t->events-1u;
    size_t begin=ordinal?t->boundaries[ordinal-1u].end:0u;
    if (begin>=t->length) return agenda_word_refuse(t,budget,"TranscriptBindingsEvent");
    /* One retained snapshot for each actual container within this event.
     * Other cuts may observe the same mutable location in a different state. */
    for (size_t i=begin;i+1u<t->length;i++) {
        if (!agenda_checkpoint_work(budget,1u))
            return agenda_word_refuse(t,budget,"TranscriptBindingsLookup");
        const AgendaWordCell *prior=&t->cells[i];
        if (prior->kind!=AGENDA_WORD_BINDINGS || !prior->value.bindings) continue;
        AgendaWordBindings *known=prior->value.bindings;
        if (known->source_location==value) {
            if (known->event!=ordinal || !known->source_ready || !known->image_ready)
                return agenda_word_refuse(t,budget,"TranscriptBindingsOwner");
            cell->value.bindings=known; /* length zero: borrowed snapshot edge */
            return true;
        }
    }
    AgendaWordBindings *b=agenda_word_alloc(t,sizeof(*b),budget);
    if (!b) return false;
    cell->value.bindings=b; cell->length=1u; /* unique owning cell */
    b->source_location=value; b->source_occurrence=value->prime_ext; b->event=ordinal;
    bindings_init(&b->source_pin); bindings_init(&b->image);
    b->source_ready=true; b->image_ready=true;
    /* Keep the actual shared storage alive until this entire forwarding
     * episode ends, including buffers subsequently replaced by the producer. */
    if (!bindings_clone(&b->source_pin,value) || !bindings_clone(&b->image,&b->source_pin) ||
        !bindings_copy_owned_atoms_with_session(&b->image,t->owner,t->atoms))
        return agenda_word_refuse(t,budget,"TranscriptBindingsOwner");
    return true;
}

static bool agenda_word_bytes(CettaSpaceAgendaTranscript *t, const char *field,
        const void *source, size_t length, SpaceCheckpointBudget *budget) {
    AgendaWordCell *cell=agenda_word_cell(t,field,AGENDA_WORD_BYTES,budget);
    if (!cell) return false;
    if (length && !source) return agenda_word_refuse(t,budget,"TranscriptByteOwner");
    if (!agenda_checkpoint_work(budget,length))
        return agenda_word_refuse(t,budget,"TranscriptByteInspection");
    if (length) {
        cell->value.bytes=agenda_word_alloc(t,length,budget);
        if (!cell->value.bytes) return false;
        memcpy(cell->value.bytes,source,length);
    }
    cell->length=length; return true;
}

static bool agenda_word_token(CettaSpaceAgendaTranscript *t, const char *field,
        SpaceReadToken token, SpaceCheckpointBudget *budget) {
    AgendaWordCell *cell=agenda_word_cell(t,field,AGENDA_WORD_TOKEN,budget);
    if (!cell) return false;
    cell->value.token=token; return true;
}

#define AW_S(f,v) do { if(!agenda_word_scalar(t,f,(uint64_t)(v),false,budget))return false; } while(0)
#define AW_F(f,v) do { if(!agenda_word_scalar(t,f,(uint64_t)(v),true,budget))return false; } while(0)
#define AW_A(f,v) do { if(!agenda_word_atom(t,f,v,budget))return false; } while(0)
#define AW_I(f,r,v) do { if(!agenda_word_identity(t,f,r,(uint64_t)(v),budget))return false; } while(0)
#define AW_B(f,v) do { if(!agenda_word_bindings(t,f,v,budget))return false; } while(0)
#define AW_T(f,v) do { if(!agenda_word_token(t,f,v,budget))return false; } while(0)

static bool agenda_word_world(CettaSpaceAgendaTranscript *t, const char *field,
        const Space *s, bool present, SpaceCheckpointBudget *budget) {
    AW_S(field,present);
    if (!present) return true;
    if (!s || !s->native.universe || s->overlay_base ||
        (s->native.atom_id_width_bits!=32u && s->native.atom_id_width_bits!=64u) ||
        s->native.start>s->native.cap || s->native.len>s->native.cap-s->native.start ||
        (s->native.len && !s->native.atom_ids))
        return agenda_word_refuse(t,budget,"TranscriptEncodedWorld");
    size_t width=s->native.atom_id_width_bits/8u;
    if (s->native.cap>SIZE_MAX/width)
        return agenda_word_refuse(t,budget,"TranscriptEncodedExtent");
    AW_T("world.read",space_read_token(s));
    AW_S("world.equation_revision",s->equation_revision);
    AW_S("world.declaration_revision",s->declaration_revision);
    AW_S("world.universe_instance",s->native.universe->instance_id);
    AW_S("world.universe_generation",s->native.universe->storage_epoch);
    AW_S("world.width",s->native.atom_id_width_bits);
    AW_S("world.start",s->native.start); AW_S("world.length",s->native.len);
    AW_F("world.capacity",s->native.cap);
    return agenda_word_bytes(t,"world.encoded_rows",s->native.atom_ids
        ? s->native.atom_ids+(size_t)s->native.start*width : NULL,
        (size_t)s->native.len*width,budget);
}

static bool agenda_word_cursor(CettaSpaceAgendaTranscript *t, const char *field,
        const SpaceOccurrenceCursor *c, bool present, SpaceCheckpointBudget *budget) {
    AW_S(field,present);
    if (!present) return true;
    AW_T("cursor.read",c->read);
    AW_I("cursor.prefix",CETTA_CHECKPOINT_TARGET_SPACE_PREFIX,c->prefix_epoch);
    AW_S("cursor.ceiling",c->ceiling); AW_S("cursor.leaf_pos",c->leaf_pos);
    AW_S("cursor.node_len",c->node_len); AW_S("cursor.flat_len",c->flat_len);
    AW_S("cursor.flat_next",c->flat_next); AW_F("cursor.flat_capacity",c->flat_capacity);
    AW_S("cursor.flat_mode",c->flat_mode); AW_S("cursor.full_scan",c->full_scan);
    AW_S("cursor.full_next",c->full_next); AW_S("cursor.pinned",c->pinned);
    AW_S("cursor.logical_scan",c->logical_scan);
    AW_T("cursor.logical_root",c->logical_reads.root);
    AW_S("cursor.logical_bases",c->logical_reads.base_count);
    if (c->logical_reads.base_count && !c->logical_reads.bases)
        return agenda_word_refuse(t,budget,"TranscriptCursorBases");
    for (size_t i=0u;i<c->logical_reads.base_count;i++) {
        AW_S("cursor.base_index",i); AW_T("cursor.base",c->logical_reads.bases[i]);
    }
    /* Opaque query buffers are represented by the real poll chronology.
     * They remain in a refused active provider and are never reconstructed. */
    return true;
}

static bool agenda_word_state(CettaSpaceAgendaTranscript *t,
        const CettaSpaceAgendaEvent *event, SpaceCheckpointBudget *budget) {
    const CettaSpaceAgenda *a=event->agenda;
    if (!a || !a->space || !a->owner || a->root!=a || a->owned_next ||
        a->pending_child || a->owned_arena || a->enumerate_matches || a->selected_match ||
        !a->contract.explicit_inputs || a->contract.set_support ||
        a->contract.raw_mm2_order || a->contract.snapshot_worlds ||
        a->advise || a->before || a->advice_context || a->chosen_advice ||
        a->trace || a->reported_count || a->reported_capacity || a->reported_atoms ||
        a->selected_key || a->selected_key_length || a->yielded_child || a->uninterpreted_choice ||
        a->space_identity!=a->space->instance_id ||
        (a->world_ready ? a->space!=&a->world : a->space==&a->world) ||
        a->schedule.state_count>1u || a->schedule.state ||
        (a->schedule.state_count &&
            (a->schedule.lane[0]!=CETTA_SELECTION_LANE_OLDEST || a->schedule.next[0])))
        return agenda_word_refuse(t,budget,"TranscriptProviderProfile");
    AW_S("event.kind",event->kind); AW_S("event.index",event->index);
    AW_S("event.detail",event->detail); AW_S("event.allowance",event->allowance);
    AW_T("event.read",event->read); AW_A("event.pattern",event->pattern);
    AW_A("event.value",event->value); AW_B("event.environment",event->environment);
    AW_S("agenda.phase",a->phase); AW_S("agenda.borrowed_capture",a->borrowed_capture);
    AW_S("agenda.world_id",a->world_id);
    AW_S("agenda.parent_world_id",a->parent_world_id); AW_S("agenda.next_world_id",a->next_world_id);
    AW_F("agenda.borrowed_target",!a->world_ready);
    AW_S("agenda.explicit_inputs",a->contract.explicit_inputs);
    AW_I("agenda.space_identity",CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE,a->space_identity);
    AW_S("agenda.set_support",a->contract.set_support); AW_S("agenda.raw_mm2_order",a->contract.raw_mm2_order);
    AW_S("agenda.snapshot_worlds",a->contract.snapshot_worlds);
#define AW_SYMBOL(f) AW_S("agenda.symbol." #f,a->f)
    AW_SYMBOL(exec); AW_SYMBOL(comma); AW_SYMBOL(input); AW_SYMBOL(output);
    AW_SYMBOL(btm); AW_SYMBOL(read); AW_SYMBOL(take); AW_SYMBOL(all);
    AW_SYMBOL(absent); AW_SYMBOL(plus); AW_SYMBOL(minus);
#undef AW_SYMBOL
    AW_S("agenda.ranking",a->ranking); AW_S("agenda.best",a->best); AW_S("agenda.oldest",a->oldest);
    AW_S("schedule.state_count",a->schedule.state_count); AW_S("schedule.state",a->schedule.state);
    for (size_t i=0u;i<CETTA_SELECTION_AUTOMATON_STATE_CAPACITY;i++) {
        AW_S("schedule.index",i); AW_S("schedule.lane",a->schedule.lane[i]); AW_S("schedule.next",a->schedule.next[i]);
    }
    AW_S("waiting.length",a->waiting_count); AW_F("waiting.capacity",a->waiting_capacity);
    AW_S("candidates.length",a->candidate_count); AW_F("candidates.capacity",a->candidate_capacity);
    if (a->waiting_count>a->waiting_capacity || (a->waiting_count && !a->waiting) ||
        a->candidate_count>a->candidate_capacity || (a->candidate_count && !a->candidates))
        return agenda_word_refuse(t,budget,"TranscriptSelectionArrays");
    for (size_t i=0u;i<a->waiting_count;i++) {
        AW_S("waiting.ordinal",i); AW_T("waiting.read",a->waiting[i].read);
        AW_S("waiting.index",a->waiting[i].index); AW_A("waiting.exec",a->waiting[i].exec);
    }
    for (size_t i=0u;i<a->candidate_count;i++) {
        if (a->candidates[i].advice) return agenda_word_refuse(t,budget,"TranscriptCandidateAdvice");
        AW_S("candidate.ordinal",i); AW_S("candidate.index",a->candidates[i].index);
        AW_A("candidate.exec",a->candidates[i].exec);
    }
    AW_S("agenda.selected_index",a->selected_index);
    AW_A("agenda.selected",a->selected); AW_A("agenda.last_selected",a->last_selected);
    AW_A("agenda.wildcard",a->wildcard); AW_A("agenda.sinks",a->sinks);
    AW_A("agenda.receipt",a->receipt); AW_B("agenda.captured",&a->captured);
    AW_T("agenda.origin_read",a->origin_read); AW_T("agenda.selected_read",a->selected_read);
    AW_T("agenda.capture_read",a->capture_read); AW_T("agenda.selection_read",a->selection_read);
    AW_S("agenda.capture_index",a->capture_index); AW_S("agenda.capture_length",a->capture_length);
    AW_S("agenda.validating",a->validating); AW_S("agenda.depth",a->depth);
    AW_S("agenda.join_ready",a->join_ready); AW_S("agenda.build_index",a->build_index);
    AW_S("agenda.sink_row",a->sink_row); AW_S("agenda.sink_index",a->sink_index);
    AW_S("agenda.commits",a->commits); AW_S("agenda.retries",a->retries);
    AW_S("agenda.snapshot_exec_appended",a->snapshot_exec_appended);
    AW_S("agenda.current_exec_appended",a->current_exec_appended);
    if (a->pending) {
        size_t n=0u;
        while (n<256u && a->pending[n]) n++;
        if (n==256u) return agenda_word_refuse(t,budget,"TranscriptPendingLabel");
        if (!agenda_word_bytes(t,"agenda.pending",a->pending,n+1u,budget)) return false;
    } else if (!agenda_word_bytes(t,"agenda.pending",NULL,0u,budget)) return false;
    if (!agenda_word_cursor(t,"agenda.selection",&a->selection,a->selection_ready,budget) ||
        !agenda_word_world(t,"agenda.target",a->space,true,budget) ||
        !agenda_word_world(t,"agenda.snapshot",&a->snapshot,a->snapshot_ready,budget) ||
        !agenda_word_world(t,"agenda.current",&a->current,a->current_ready,budget) ||
        !agenda_word_world(t,"agenda.replacement",&a->replacement,a->replacement_ready,budget)) return false;
    AW_S("agenda.inputs",a->input_count);
    if (a->input_count && !a->inputs) return agenda_word_refuse(t,budget,"TranscriptInputs");
    for (size_t i=0u;i<a->input_count;i++) {
        AW_S("input.index",i); AW_S("input.kind",a->inputs[i].kind);
        AW_A("input.pattern",a->inputs[i].pattern); AW_A("input.collection",a->inputs[i].collection);
    }
    AW_S("agenda.levels_present",a->levels!=NULL);
    if (a->levels) {
        if (a->input_count==SIZE_MAX || a->depth>a->input_count)
            return agenda_word_refuse(t,budget,"TranscriptDepth");
        for (size_t i=0u;i<=a->input_count;i++) {
            const JoinLevel *level=&a->levels[i];
            AW_S("level.index",i); AW_S("level.environment_ready",level->environment_ready);
            if (level->environment_ready) AW_B("level.environment",&level->environment);
            if (!agenda_word_cursor(t,"level.cursor",&level->cursor,level->cursor_ready,budget)) return false;
            AW_S("level.aggregate_done",level->aggregate_done); AW_S("level.exhausted",level->exhausted);
            AW_S("level.aggregate_length",level->aggregate_length);
            AW_F("level.aggregate_capacity",level->aggregate_capacity);
            if (level->aggregate_length>level->aggregate_capacity ||
                (level->aggregate_length && !level->aggregate))
                return agenda_word_refuse(t,budget,"TranscriptAggregate");
            for (size_t j=0u;j<level->aggregate_length;j++) {
                AW_S("aggregate.index",j); AW_A("aggregate.value",level->aggregate[j]);
            }
            AW_A("level.aggregate_value",level->aggregate_value);
            AW_S("level.witness",level->witness); AW_A("level.witness_atom",level->witness_atom);
            AW_A("level.pending_row",level->pending_row); AW_S("level.pending_index",level->pending_index);
        }
    }
    for (size_t side=0u;side<2u;side++) {
        const JoinedRow *rows=side?a->validation:a->rows;
        size_t length=side?a->validation_count:a->row_count;
        size_t capacity=side?a->validation_capacity:a->row_capacity;
        AW_S("rows.side",side); AW_S("rows.length",length); AW_F("rows.capacity",capacity);
        if (length>capacity || (length && !rows) ||
            a->match_epoch_count[side]>a->match_epoch_capacity[side] ||
            (a->match_epoch_count[side] && !a->match_epochs[side]))
            return agenda_word_refuse(t,budget,"TranscriptRows");
        for (size_t i=0u;i<length;i++) {
            AW_S("row.index",i); AW_A("row.outputs",rows[i].outputs);
            AW_A("row.witnesses",rows[i].witnesses); AW_A("row.dependencies",rows[i].dependencies);
        }
        AW_S("epochs.length",a->match_epoch_count[side]); AW_F("epochs.capacity",a->match_epoch_capacity[side]);
        for (size_t i=0u;i<a->match_epoch_count[side];i++) {
            AW_S("epoch.index",i); AW_I("epoch.frame",CETTA_CHECKPOINT_FRAME,a->match_epochs[side][i].word);
        }
    }
    AW_S("agenda.matching",a->matching!=NULL);
    if (a->matching) {
        /* Only initialized scalar matcher disposition is observed here.
         * Isolated operands and result buffers are not admitted roots. */
        AW_S("match.isolated",a->matching->isolated); AW_S("match.prepared",a->matching->prepared);
        AW_S("match.result_ready",a->matching->result_ready); AW_S("match.decision",a->matching->decision);
        AW_I("match.epoch",CETTA_CHECKPOINT_FRAME,a->matching->epoch.word);
    }
    return true;
}

bool cetta_space_agenda_transcript_append(CettaSpaceAgendaTranscript *t,
        const CettaSpaceAgendaEvent *event, SpaceCheckpointBudget *budget) {
    if (!t || !event || event->kind>=CETTA_AGENDA_EVENT_COUNT) return false;
    /* Publication is a retained fact even if the following snapshot refuses. */
    if (event->kind==CETTA_AGENDA_EVENT_COMMITTED) {
        if (!t->post_reserved || t->committed==UINT64_MAX)
            return agenda_word_refuse(t,budget,"TranscriptCommitReservation");
        t->post_reserved=false; t->committed++;
    }
    if (t->refusal || t->finished || t->events==t->event_limit)
        return agenda_word_refuse(t,budget,"TranscriptEventCapacity");
    if (event->kind==CETTA_AGENDA_EVENT_PRECOMMIT &&
        (t->post_reserved || t->event_limit-t->events<2u || t->capacity-t->length<2u))
        return agenda_word_refuse(t,budget,"TranscriptPostReservation");
    size_t ordinal=t->events++;
    if (!agenda_word_scalar(t,"event.ordinal",ordinal,false,budget) ||
        !agenda_word_state(t,event,budget)) {
        if (!t->refusal) t->refusal=budget&&budget->refusal?budget->refusal:"TranscriptCapture";
        return false;
    }
    if (event->kind==CETTA_AGENDA_EVENT_PRECOMMIT) {
        if (t->capacity-t->length<2u)
            return agenda_word_refuse(t,budget,"TranscriptPostReservation");
        t->post_reserved=true;
    }
    t->boundaries[ordinal]=(AgendaWordEvent){.end=t->length,.kind=event->kind,
        .committed=t->committed,.post_reserved=t->post_reserved};
    t->completed_events++;
    return true;
}

/* Validate one completed event against the still-held provider. This reader
 * is independent of the append helpers: its required field sequence cannot
 * be satisfied by two transcripts that share an encoder omission. It neither
 * executes a matcher nor constructs an expected transcript. */
typedef struct {
    CettaSpaceAgendaTranscript *word;
    const CettaCheckpointCompare *origin;
    SpaceCheckpointBudget *budget;
    size_t at,end,begin,event;
} AgendaLiveWord;

/* A synchronous adapter for the two mutable header roles only. All payload,
 * identity, arena, cache and service fields use the caller's persistent
 * reader and retain its fuel/accounting and alias relation. */
typedef struct {
    const CettaCheckpointCompare *parent;
    const Bindings *source,*image;
} AgendaLiveBindingsRead;
#define AL_FORWARD(name,args,call) static bool agenda_live_bindings_##name args {     AgendaLiveBindingsRead *r=raw;return r->parent->name call; }
AL_FORWARD(scalar,(void *raw,const char *f,uint64_t a,uint64_t b),
    (r->parent->context,f,a,b))
AL_FORWARD(identity,(void *raw,const char *f,CettaCheckpointIdentityRole k,uint64_t a,uint64_t b),
    (r->parent->context,f,k,a,b))
AL_FORWARD(address,(void *raw,const char *f,const void *a,const void *b),
    (r->parent->context,f,a,b))
AL_FORWARD(arena,(void *raw,const char *f,const Arena *a,const Arena *b),
    (r->parent->context,f,a,b))
AL_FORWARD(atom,(void *raw,const char *f,const Atom *a,const Atom *b),
    (r->parent->context,f,a,b))
AL_FORWARD(physical,(void *raw,const char *f,uint64_t a,uint64_t b),
    (r->parent->context,f,a,b))
AL_FORWARD(bytes,(void *raw,const char *f,const void *a,size_t n,const void *b,size_t m),
    (r->parent->context,f,a,n,b,m))
AL_FORWARD(service,(void *raw,const char *f,size_t work,size_t bytes),
    (r->parent->context,f,work,bytes))
#undef AL_FORWARD
static void agenda_live_bindings_service_release(void *raw,size_t bytes) {
    AgendaLiveBindingsRead *r=raw;
    r->parent->service_release(r->parent->context,bytes);
}
static bool agenda_live_bindings_frame_scope(void *raw,const char *field,
    const CettaFrameIdentityScope *a,uint64_t ai,const CettaFrameIdentityScope *b,uint64_t bi) {
    AgendaLiveBindingsRead *r=raw;
    return r->parent->frame_scope(r->parent->context,field,a,ai,b,bi);
}
static bool agenda_live_bindings_target(void *raw,const char *field,
    const Space *a,uint64_t ai,const Space *b,uint64_t bi) {
    AgendaLiveBindingsRead *r=raw;
    return r->parent->target(r->parent->context,field,a,ai,b,bi);
}
static bool agenda_live_bindings_retained_atom(void *raw,const char *field,
    CettaCheckpointRetainedAtomRole role,const Atom *a,const Atom *b) {
    AgendaLiveBindingsRead *r=raw;
    return r->parent->retained_atom(r->parent->context,field,role,a,b);
}
static bool agenda_live_bindings_container(void *raw,const char *field,
    CettaCheckpointContainerRole role,const void *a,const void *b) {
    AgendaLiveBindingsRead *r=raw;
    bool pair=role==CETTA_CHECKPOINT_BINDINGS_CONTAINER
        ? a==r->source && b==r->image
        : role==CETTA_CHECKPOINT_BINDINGS_OCCURRENCE_CONTAINER &&
            a==r->source->prime_ext && b==r->image->prime_ext;
    return r->parent->physical(r->parent->context,field,(uintptr_t)a,(uintptr_t)b)&&
        r->parent->scalar(r->parent->context,field,pair&&((a!=NULL)==(b!=NULL)),true);
}

static const AgendaWordCell *agenda_live_cell(AgendaLiveWord *r,
        const char *field,AgendaWordKind kind) {
    if(!agenda_checkpoint_work(r->budget,1u))return NULL;
    if(r->at>=r->end)return NULL;
    const AgendaWordCell *cell=&r->word->cells[r->at++];
    size_t n=strlen(field);
    if(!agenda_checkpoint_work(r->budget,n+1u)||!cell->field||
        cell->kind!=kind||strcmp(cell->field,field))return NULL;
    return cell;
}
static bool agenda_live_scalar(AgendaLiveWord *r,const char *field,
        uint64_t value,bool physical) {
    const AgendaWordCell *cell=agenda_live_cell(r,field,
        physical?AGENDA_WORD_PHYSICAL:AGENDA_WORD_SCALAR);
    return cell&&cell->value.scalar==value;
}
static bool agenda_live_identity(AgendaLiveWord *r,const char *field,
        CettaCheckpointIdentityRole role,uint64_t value) {
    const AgendaWordCell *cell=agenda_live_cell(r,field,AGENDA_WORD_IDENTITY);
    return cell&&cell->role==role&&cell->value.scalar==value;
}
static bool agenda_live_atom(AgendaLiveWord *r,const char *field,const Atom *value) {
    const AgendaWordCell *cell=agenda_live_cell(r,field,AGENDA_WORD_ATOM);
    if(!cell)return false;
    const Atom *image=value?atom_deep_copy_session_forwarded(r->word->atoms,value):NULL;
    /* Closed identity-retained roots may bypass the forwarding table. The
     * caller's restricted domain/whole-graph reader still authenticates them. */
    if(value&&!image&&cell->value.atom==value)image=value;
    return image==cell->value.atom&&((value!=NULL)==(image!=NULL))&&
        r->origin->atom(r->origin->context,field,value,image);
}
static bool agenda_live_bindings(AgendaLiveWord *r,const char *field,
        const Bindings *value) {
    const AgendaWordCell *cell=agenda_live_cell(r,field,AGENDA_WORD_BINDINGS);
    if(!cell)return false;
    const AgendaWordBindings *b=cell->value.bindings;
    if(!value||!b)return r->origin->scalar(r->origin->context,
        "transcript.binding_present",value!=NULL,b!=NULL)&&
        r->origin->scalar(r->origin->context,"transcript.binding_owning_cell",cell->length,0u);
    if (!r->origin->scalar(r->origin->context,"transcript.binding_source",
            b->source_location==value,true) ||
        !r->origin->scalar(r->origin->context,"transcript.binding_event",b->event,r->event) ||
        !r->origin->scalar(r->origin->context,"transcript.binding_occurrence_source",
            b->source_occurrence==value->prime_ext,true) || !b->source_ready || !b->image_ready)
        return false;
    /* Earlier rows in this event have already been validated against their
     * live fields. Independently check the snapshot's alias bijection, even
     * if the encoder were changed to allocate a second equal snapshot. */
    bool seen=false;
    for (size_t i=r->begin;i+1u<r->at;i++) {
        if (!agenda_checkpoint_work(r->budget,1u)) return false;
        const AgendaWordCell *prior=&r->word->cells[i];
        if (prior->kind!=AGENDA_WORD_BINDINGS || !prior->value.bindings) continue;
        const AgendaWordBindings *known=prior->value.bindings;
        if (known==b) seen=true;
        if (!r->origin->scalar(r->origin->context,"transcript.binding_alias",
                known->source_location==value,known==b) ||
            !r->origin->scalar(r->origin->context,"transcript.binding_occurrence_alias",
                known->source_occurrence==value->prime_ext,
                known->image.prime_ext==b->image.prime_ext)) return false;
    }
    if (!r->origin->scalar(r->origin->context,"transcript.binding_owning_cell",
            cell->length,seen?0u:1u)) return false;
    AgendaLiveBindingsRead context={r->origin,value,&b->image};
    CettaCheckpointCompare read={.context=&context,
        .scalar=agenda_live_bindings_scalar,.identity=agenda_live_bindings_identity,
        .address=agenda_live_bindings_address,.container=agenda_live_bindings_container,
        .arena=agenda_live_bindings_arena,.atom=agenda_live_bindings_atom,
        .frame_scope=r->origin->frame_scope?agenda_live_bindings_frame_scope:NULL,
        .target=r->origin->target?agenda_live_bindings_target:NULL,
        .retained_atom=r->origin->retained_atom?agenda_live_bindings_retained_atom:NULL,
        .physical=agenda_live_bindings_physical,.bytes=agenda_live_bindings_bytes,
        .service=agenda_live_bindings_service,
        .service_release=r->origin->service_release?agenda_live_bindings_service_release:NULL};
    return bindings_checkpoint_compare(value,&b->image,&read);
}
static bool agenda_live_bytes(AgendaLiveWord *r,const char *field,
        const void *value,size_t length) {
    const AgendaWordCell *cell=agenda_live_cell(r,field,AGENDA_WORD_BYTES);
    if(!cell||cell->length!=length||(length&&(!value||!cell->value.bytes)))return false;
    const unsigned char *a=value,*b=cell->value.bytes;
    for(size_t i=0u;i<length;i++)
        if(!agenda_checkpoint_work(r->budget,1u)||a[i]!=b[i])return false;
    return true;
}
static bool agenda_live_token(AgendaLiveWord *r,const char *field,SpaceReadToken value) {
    const AgendaWordCell *cell=agenda_live_cell(r,field,AGENDA_WORD_TOKEN);
    if(!cell||!agenda_checkpoint_work(r->budget,7u))return false;
    SpaceReadToken image=cell->value.token;
    return value.space==image.space&&value.instance_id==image.instance_id&&
        value.revision==image.revision&&value.prefix_epoch==image.prefix_epoch&&
        value.base_prefix_epoch==image.base_prefix_epoch&&
        value.base_dependency_epoch==image.base_dependency_epoch;
}
#define AL_S(f,v) do {if(!agenda_live_scalar(r,f,(uint64_t)(v),false))return false;}while(0)
#define AL_P(f,v) do {if(!agenda_live_scalar(r,f,(uint64_t)(v),true))return false;}while(0)
#define AL_I(f,k,v) do {if(!agenda_live_identity(r,f,k,(uint64_t)(v)))return false;}while(0)
#define AL_A(f,v) do {if(!agenda_live_atom(r,f,v))return false;}while(0)
#define AL_B(f,v) do {if(!agenda_live_bindings(r,f,v))return false;}while(0)
#define AL_T(f,v) do {if(!agenda_live_token(r,f,v))return false;}while(0)

static bool agenda_live_world(AgendaLiveWord *r,const char *field,
        const Space *world,bool present) {
    AL_S(field,present);
    if(!present)return true;
    if(!world||!world->native.universe||world->overlay_base||
        (world->native.atom_id_width_bits!=32u&&world->native.atom_id_width_bits!=64u)||
        world->native.start>world->native.cap||
        world->native.len>world->native.cap-world->native.start||
        (world->native.len&&!world->native.atom_ids))return false;
    size_t width=world->native.atom_id_width_bits/8u;
    if(world->native.cap>SIZE_MAX/width)return false;
    AL_T("world.read",space_read_token(world));
    AL_S("world.equation_revision",world->equation_revision);
    AL_S("world.declaration_revision",world->declaration_revision);
    AL_S("world.universe_instance",world->native.universe->instance_id);
    AL_S("world.universe_generation",world->native.universe->storage_epoch);
    AL_S("world.width",world->native.atom_id_width_bits);
    AL_S("world.start",world->native.start);
    AL_S("world.length",world->native.len);
    AL_P("world.capacity",world->native.cap);
    return agenda_live_bytes(r,"world.encoded_rows",world->native.atom_ids?
        world->native.atom_ids+(size_t)world->native.start*width:NULL,
        (size_t)world->native.len*width);
}
static bool agenda_live_cursor(AgendaLiveWord *r,const char *field,
        const SpaceOccurrenceCursor *cursor,bool present) {
    AL_S(field,present);
    if(!present)return true;
    AL_T("cursor.read",cursor->read);
    AL_I("cursor.prefix",CETTA_CHECKPOINT_TARGET_SPACE_PREFIX,cursor->prefix_epoch);
    AL_S("cursor.ceiling",cursor->ceiling);AL_S("cursor.leaf_pos",cursor->leaf_pos);
    AL_S("cursor.node_len",cursor->node_len);AL_S("cursor.flat_len",cursor->flat_len);
    AL_S("cursor.flat_next",cursor->flat_next);AL_P("cursor.flat_capacity",cursor->flat_capacity);
    AL_S("cursor.flat_mode",cursor->flat_mode);AL_S("cursor.full_scan",cursor->full_scan);
    AL_S("cursor.full_next",cursor->full_next);AL_S("cursor.pinned",cursor->pinned);
    AL_S("cursor.logical_scan",cursor->logical_scan);
    AL_T("cursor.logical_root",cursor->logical_reads.root);
    AL_S("cursor.logical_bases",cursor->logical_reads.base_count);
    if(cursor->logical_reads.base_count&&!cursor->logical_reads.bases)return false;
    for(size_t i=0u;i<cursor->logical_reads.base_count;i++) {
        AL_S("cursor.base_index",i);AL_T("cursor.base",cursor->logical_reads.bases[i]);
    }
    return true;
}
static bool agenda_live_state(AgendaLiveWord *r,const CettaSpaceAgendaEvent *event) {
    const CettaSpaceAgenda *a=event->agenda;
    if(!a||!a->space||!a->owner||a->root!=a||a->owned_next||a->pending_child||
        a->owned_arena||a->enumerate_matches||a->selected_match||
        !a->contract.explicit_inputs||a->contract.set_support||a->contract.raw_mm2_order||
        a->contract.snapshot_worlds||a->advise||a->before||a->advice_context||a->chosen_advice||
        a->trace||a->reported_count||a->reported_capacity||a->reported_atoms||
        a->selected_key||a->selected_key_length||a->yielded_child||a->uninterpreted_choice||
        a->space_identity!=a->space->instance_id||
        (a->world_ready?a->space!=&a->world:a->space==&a->world)||
        a->schedule.state_count>1u||a->schedule.state||
        (a->schedule.state_count&&(a->schedule.lane[0]!=CETTA_SELECTION_LANE_OLDEST||
                                  a->schedule.next[0])))return false;
    AL_S("event.kind",event->kind);AL_S("event.index",event->index);
    AL_S("event.detail",event->detail);AL_S("event.allowance",event->allowance);
    AL_T("event.read",event->read);AL_A("event.pattern",event->pattern);
    AL_A("event.value",event->value);AL_B("event.environment",event->environment);
    AL_S("agenda.phase",a->phase);AL_S("agenda.borrowed_capture",a->borrowed_capture);
    AL_S("agenda.world_id",a->world_id);
    AL_S("agenda.parent_world_id",a->parent_world_id);AL_S("agenda.next_world_id",a->next_world_id);
    AL_P("agenda.borrowed_target",!a->world_ready);
    AL_S("agenda.explicit_inputs",a->contract.explicit_inputs);
    AL_I("agenda.space_identity",CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE,a->space_identity);
    AL_S("agenda.set_support",a->contract.set_support);AL_S("agenda.raw_mm2_order",a->contract.raw_mm2_order);
    AL_S("agenda.snapshot_worlds",a->contract.snapshot_worlds);
    AL_S("agenda.symbol.exec",a->exec);AL_S("agenda.symbol.comma",a->comma);
    AL_S("agenda.symbol.input",a->input);AL_S("agenda.symbol.output",a->output);
    AL_S("agenda.symbol.btm",a->btm);AL_S("agenda.symbol.read",a->read);
    AL_S("agenda.symbol.take",a->take);AL_S("agenda.symbol.all",a->all);
    AL_S("agenda.symbol.absent",a->absent);AL_S("agenda.symbol.plus",a->plus);
    AL_S("agenda.symbol.minus",a->minus);
    AL_S("agenda.ranking",a->ranking);AL_S("agenda.best",a->best);AL_S("agenda.oldest",a->oldest);
    AL_S("schedule.state_count",a->schedule.state_count);AL_S("schedule.state",a->schedule.state);
    for(size_t i=0u;i<CETTA_SELECTION_AUTOMATON_STATE_CAPACITY;i++) {
        AL_S("schedule.index",i);AL_S("schedule.lane",a->schedule.lane[i]);AL_S("schedule.next",a->schedule.next[i]);
    }
    AL_S("waiting.length",a->waiting_count);AL_P("waiting.capacity",a->waiting_capacity);
    AL_S("candidates.length",a->candidate_count);AL_P("candidates.capacity",a->candidate_capacity);
    if(a->waiting_count>a->waiting_capacity||(a->waiting_count&&!a->waiting)||
        a->candidate_count>a->candidate_capacity||(a->candidate_count&&!a->candidates))return false;
    for(size_t i=0u;i<a->waiting_count;i++) {
        AL_S("waiting.ordinal",i);AL_T("waiting.read",a->waiting[i].read);
        AL_S("waiting.index",a->waiting[i].index);AL_A("waiting.exec",a->waiting[i].exec);
    }
    for(size_t i=0u;i<a->candidate_count;i++) {
        if(a->candidates[i].advice)return false;
        AL_S("candidate.ordinal",i);AL_S("candidate.index",a->candidates[i].index);
        AL_A("candidate.exec",a->candidates[i].exec);
    }
    AL_S("agenda.selected_index",a->selected_index);
    AL_A("agenda.selected",a->selected);AL_A("agenda.last_selected",a->last_selected);
    AL_A("agenda.wildcard",a->wildcard);AL_A("agenda.sinks",a->sinks);
    AL_A("agenda.receipt",a->receipt);AL_B("agenda.captured",&a->captured);
    AL_T("agenda.origin_read",a->origin_read);AL_T("agenda.selected_read",a->selected_read);
    AL_T("agenda.capture_read",a->capture_read);AL_T("agenda.selection_read",a->selection_read);
    AL_S("agenda.capture_index",a->capture_index);AL_S("agenda.capture_length",a->capture_length);
    AL_S("agenda.validating",a->validating);AL_S("agenda.depth",a->depth);
    AL_S("agenda.join_ready",a->join_ready);AL_S("agenda.build_index",a->build_index);
    AL_S("agenda.sink_row",a->sink_row);AL_S("agenda.sink_index",a->sink_index);
    AL_S("agenda.commits",a->commits);AL_S("agenda.retries",a->retries);
    AL_S("agenda.snapshot_exec_appended",a->snapshot_exec_appended);
    AL_S("agenda.current_exec_appended",a->current_exec_appended);
    size_t pending=0u;
    if(a->pending) {
        while(pending<256u&&a->pending[pending])pending++;
        if(pending==256u)return false;
        pending++;
    }
    if(!agenda_live_bytes(r,"agenda.pending",a->pending,pending)||
        !agenda_live_cursor(r,"agenda.selection",&a->selection,a->selection_ready)||
        !agenda_live_world(r,"agenda.target",a->space,true)||
        !agenda_live_world(r,"agenda.snapshot",&a->snapshot,a->snapshot_ready)||
        !agenda_live_world(r,"agenda.current",&a->current,a->current_ready)||
        !agenda_live_world(r,"agenda.replacement",&a->replacement,a->replacement_ready))return false;
    AL_S("agenda.inputs",a->input_count);
    if(a->input_count&&!a->inputs)return false;
    for(size_t i=0u;i<a->input_count;i++) {
        AL_S("input.index",i);AL_S("input.kind",a->inputs[i].kind);
        AL_A("input.pattern",a->inputs[i].pattern);AL_A("input.collection",a->inputs[i].collection);
    }
    AL_S("agenda.levels_present",a->levels!=NULL);
    if(a->levels) {
        if(a->input_count==SIZE_MAX||a->depth>a->input_count)return false;
        for(size_t i=0u;i<=a->input_count;i++) {
            const JoinLevel *level=&a->levels[i];
            AL_S("level.index",i);AL_S("level.environment_ready",level->environment_ready);
            if(level->environment_ready)AL_B("level.environment",&level->environment);
            if(!agenda_live_cursor(r,"level.cursor",&level->cursor,level->cursor_ready))return false;
            AL_S("level.aggregate_done",level->aggregate_done);AL_S("level.exhausted",level->exhausted);
            AL_S("level.aggregate_length",level->aggregate_length);AL_P("level.aggregate_capacity",level->aggregate_capacity);
            if(level->aggregate_length>level->aggregate_capacity||
                (level->aggregate_length&&!level->aggregate))return false;
            for(size_t j=0u;j<level->aggregate_length;j++) {
                AL_S("aggregate.index",j);AL_A("aggregate.value",level->aggregate[j]);
            }
            AL_A("level.aggregate_value",level->aggregate_value);
            AL_S("level.witness",level->witness);AL_A("level.witness_atom",level->witness_atom);
            AL_A("level.pending_row",level->pending_row);AL_S("level.pending_index",level->pending_index);
        }
    }
    for(size_t side=0u;side<2u;side++) {
        const JoinedRow *rows=side?a->validation:a->rows;
        size_t length=side?a->validation_count:a->row_count;
        size_t capacity=side?a->validation_capacity:a->row_capacity;
        AL_S("rows.side",side);AL_S("rows.length",length);AL_P("rows.capacity",capacity);
        if(length>capacity||(length&&!rows)||a->match_epoch_count[side]>a->match_epoch_capacity[side]||
            (a->match_epoch_count[side]&&!a->match_epochs[side]))return false;
        for(size_t i=0u;i<length;i++) {
            AL_S("row.index",i);AL_A("row.outputs",rows[i].outputs);
            AL_A("row.witnesses",rows[i].witnesses);AL_A("row.dependencies",rows[i].dependencies);
        }
        AL_S("epochs.length",a->match_epoch_count[side]);AL_P("epochs.capacity",a->match_epoch_capacity[side]);
        for(size_t i=0u;i<a->match_epoch_count[side];i++) {
            AL_S("epoch.index",i);AL_I("epoch.frame",CETTA_CHECKPOINT_FRAME,a->match_epochs[side][i].word);
        }
    }
    AL_S("agenda.matching",a->matching!=NULL);
    if(a->matching) {
        AL_S("match.isolated",a->matching->isolated);AL_S("match.prepared",a->matching->prepared);
        AL_S("match.result_ready",a->matching->result_ready);AL_S("match.decision",a->matching->decision);
        AL_I("match.epoch",CETTA_CHECKPOINT_FRAME,a->matching->epoch.word);
    }
    return true;
}

bool cetta_space_agenda_transcript_validate_live(CettaSpaceAgendaTranscript *t,
        const CettaSpaceAgendaEvent *event,const CettaCheckpointCompare *origin,
        SpaceCheckpointBudget *budget,SpaceReadToken *replacement) {
    if(replacement)*replacement=(SpaceReadToken){0};
    if(!t||!event||!event->agenda||!cetta_checkpoint_compare_valid(origin)||
        t->finished||!t->atoms||!cetta_space_agenda_transcript_complete(t)||!t->events)
        return agenda_word_refuse(t,budget,"TranscriptLiveBoundary");
    size_t ordinal=t->events-1u;
    const AgendaWordEvent *last=&t->boundaries[ordinal];
    size_t begin=ordinal?t->boundaries[ordinal-1u].end:0u;
    if(last->kind!=event->kind||last->end!=t->length||begin>=last->end||
        last->committed!=t->committed||last->post_reserved!=t->post_reserved)
        return agenda_word_refuse(t,budget,"TranscriptLiveBoundary");
    AgendaLiveWord r={t,origin,budget,begin,last->end,begin,ordinal};
    bool ok=agenda_live_scalar(&r,"event.ordinal",ordinal,false)&&
        agenda_live_state(&r,event)&&r.at==r.end;
    if(!ok)return agenda_word_refuse(t,budget,"TranscriptLiveMismatch");
    if(replacement&&event->agenda->replacement_ready)
        *replacement=space_read_token(&event->agenda->replacement);
    return true;
}
#undef AL_S
#undef AL_P
#undef AL_I
#undef AL_A
#undef AL_B
#undef AL_T

void cetta_space_agenda_transcript_finish(CettaSpaceAgendaTranscript *t) {
    if (!t || t->finished) return;
    /* Memo keys may refer to these source backing allocations. */
    atom_deep_copy_session_free(t->atoms); t->atoms=NULL;
    for (size_t i=0u;i<t->length;i++) {
        AgendaWordCell *cell=&t->cells[i];
        if (cell->kind==AGENDA_WORD_BINDINGS && cell->length==1u && cell->value.bindings &&
            cell->value.bindings->source_ready) {
            bindings_free(&cell->value.bindings->source_pin);
            cell->value.bindings->source_ready=false;
        }
    }
    t->finished=true;
}

void cetta_space_agenda_transcript_free(CettaSpaceAgendaTranscript *t) {
    if (!t) return;
    cetta_space_agenda_transcript_finish(t);
    for (size_t i=0u;i<t->length;i++) {
        AgendaWordCell *cell=&t->cells[i];
        if (cell->kind==AGENDA_WORD_BINDINGS && cell->length==1u && cell->value.bindings) {
            if (cell->value.bindings->image_ready) bindings_free(&cell->value.bindings->image);
            free(cell->value.bindings);
        } else if (cell->kind==AGENDA_WORD_BYTES) free(cell->value.bytes);
    }
    cetta_frame_identity_scope_clear(&t->frame_holds);
    free(t->boundaries); free(t->cells); free(t);
}

void cetta_space_agenda_transcript_storage(const CettaSpaceAgendaTranscript *t,
                                          CettaStorageCensus *c) {
    if (!t || !cetta_storage_object(c,t,sizeof(*t),CETTA_STORAGE_BLOCK)) return;
    arena_storage_census(t->owner,c);
    atom_deep_copy_session_storage_census(t->atoms,c);
    cetta_storage_array(c,t->cells,t->capacity,sizeof(*t->cells),0u);
    cetta_storage_array(c,t->boundaries,t->event_limit,sizeof(*t->boundaries),0u);
    if (t->frame_holds.identities && t->frame_holds.identities!=t->frame_holds.inline_identities)
        cetta_storage_array(c,t->frame_holds.identities,t->frame_holds.cap,sizeof(*t->frame_holds.identities),0u);
    for (size_t i=0u;i<t->length;i++) {
        const AgendaWordCell *cell=&t->cells[i];
        if (cell->kind==AGENDA_WORD_BINDINGS && cell->length==1u && cell->value.bindings) {
            const AgendaWordBindings *b=cell->value.bindings;
            if (cetta_storage_object(c,b,sizeof(*b),CETTA_STORAGE_BLOCK)) {
                if (b->source_ready) bindings_storage_census(&b->source_pin,c);
                if (b->image_ready) bindings_storage_census(&b->image,c);
            }
        } else if (cell->kind==AGENDA_WORD_BYTES)
            cetta_storage_array(c,cell->value.bytes,cell->length,1u,0u);
    }
}

bool cetta_space_agenda_transcript_global_extension_budget(const CettaSpaceAgendaTranscript *t,
    const HashConsTable *owner,size_t nodes,size_t *bytes,size_t *work) {
    return t&&!t->finished&&t->atoms&&cetta_space_agenda_transcript_complete(t)&&
        atom_deep_copy_session_global_extension_budget(t->atoms,owner,nodes,bytes,work);
}
bool cetta_space_agenda_transcript_extend_globals(CettaSpaceAgendaTranscript *t,
    const HashConsTable *owner,size_t nodes,size_t work,size_t bytes,
    AtomDeepCopyGlobalInspection *inspection) {
    if(inspection)*inspection=(AtomDeepCopyGlobalInspection){0};
    return t&&!t->finished&&t->atoms&&cetta_space_agenda_transcript_complete(t)&&
        atom_deep_copy_session_extend_globals(t->atoms,owner,nodes,work,bytes,inspection);
}

bool cetta_space_agenda_transcript_correspondence_live(const CettaSpaceAgendaTranscript *t) {
    return t&&!t->finished&&t->atoms&&cetta_space_agenda_transcript_complete(t);
}
bool cetta_space_agenda_transcript_maps_arena(const CettaSpaceAgendaTranscript *t,
    const Arena *source,const Arena *destination) {
    return t&&!t->finished&&t->atoms&&
        atom_deep_copy_session_maps_arena(t->atoms,source,destination);
}
const Atom *cetta_space_agenda_transcript_forwarded(const CettaSpaceAgendaTranscript *t,
    const Atom *source) {
    return t && t->atoms && !t->finished
        ? atom_deep_copy_session_forwarded(t->atoms,source) : NULL;
}

bool cetta_space_agenda_transcript_complete(const CettaSpaceAgendaTranscript *t) {
    return t && !t->refusal && t->cells && t->boundaries &&
        t->events<=t->event_limit && t->length<=t->capacity &&
        t->events==t->completed_events;
}
size_t cetta_space_agenda_transcript_events(const CettaSpaceAgendaTranscript *t) {
    return t?t->events:0u;
}
bool cetta_space_agenda_transcript_counts(const CettaSpaceAgendaTranscript *t,
        size_t *cells,size_t *capacity) {
    if(!t||!cells||!capacity)return false;
    *cells=t->length;*capacity=t->capacity;return true;
}
uint64_t cetta_space_agenda_transcript_commits(const CettaSpaceAgendaTranscript *t) {
    return t?t->committed:0u;
}
const char *cetta_space_agenda_transcript_refusal(const CettaSpaceAgendaTranscript *t) {
    return t?t->refusal:"MissingTranscript";
}

/* Read the stored boundaries of the actual two owners. No shortened owner
 * is manufactured, and no later recorded event is modified or discarded. */
static bool agenda_word_compare(const CettaSpaceAgendaTranscript *a,
        const CettaSpaceAgendaTranscript *b, const CettaCheckpointCompare *read,
        bool prefix) {
    if (!cetta_checkpoint_compare_valid(read) ||
        !cetta_space_agenda_transcript_complete(a) ||
        !cetta_space_agenda_transcript_complete(b) ||
        (prefix ? (!b->events || b->events>a->events) : a->events!=b->events) ||
        !read->scalar(read->context,"transcript.events",b->events,b->events) ||
        !read->arena(read->context,"transcript.owner",a->owner,b->owner)) return false;
    size_t end=0u;
    uint64_t committed=0u;
    bool reserved=false;
    for (size_t i=0u;i<b->events;i++) {
        const AgendaWordEvent *x=&a->boundaries[i], *y=&b->boundaries[i];
        if (!read->scalar(read->context,"transcript.boundary",i,i) ||
            !read->scalar(read->context,"transcript.end",x->end,y->end) ||
            !read->scalar(read->context,"transcript.event_kind",x->kind,y->kind) ||
            !read->scalar(read->context,"transcript.committed",x->committed,y->committed) ||
            !read->scalar(read->context,"transcript.post_reserved",x->post_reserved,y->post_reserved)) return false;
        if (x->end!=y->end || x->kind!=y->kind ||
            x->kind>=CETTA_AGENDA_EVENT_COUNT || x->end<=end ||
            x->end-end<2u || x->end>a->length || y->end>b->length) return false;
        const AgendaWordCell *ax=&a->cells[end], *by=&b->cells[end];
        if (ax->kind!=AGENDA_WORD_SCALAR || by->kind!=AGENDA_WORD_SCALAR ||
            strcmp(ax->field,"event.ordinal") || strcmp(by->field,"event.ordinal") ||
            ax->value.scalar!=i || by->value.scalar!=i ||
            ax[1].kind!=AGENDA_WORD_SCALAR || by[1].kind!=AGENDA_WORD_SCALAR ||
            strcmp(ax[1].field,"event.kind") || strcmp(by[1].field,"event.kind") ||
            ax[1].value.scalar!=(uint64_t)x->kind || by[1].value.scalar!=(uint64_t)y->kind)
            return false;
        if (x->kind==CETTA_AGENDA_EVENT_PRECOMMIT) {
            if (reserved) return false;
            reserved=true;
        } else if (x->kind==CETTA_AGENDA_EVENT_COMMITTED) {
            if (!reserved || committed==UINT64_MAX) return false;
            reserved=false; committed++;
        }
        if (x->committed!=committed || y->committed!=committed ||
            x->post_reserved!=reserved || y->post_reserved!=reserved) return false;
        end=x->end;
    }
    if (end!=b->length || committed!=b->committed || reserved!=b->post_reserved ||
        (prefix ? (!reserved || b->boundaries[b->events-1u].kind!=CETTA_AGENDA_EVENT_PRECOMMIT)
                : (end!=a->length || committed!=a->committed || reserved!=a->post_reserved)) ||
        !read->scalar(read->context,"transcript.cells",end,b->length)) return false;
    for (size_t i=0u;i<end;i++) {
        const AgendaWordCell *x=&a->cells[i], *y=&b->cells[i];
        if (x->kind!=y->kind || x->role!=y->role || strcmp(x->field,y->field) ||
            !read->scalar(read->context,"transcript.cell",i,i) ||
            !read->scalar(read->context,"transcript.kind",x->kind,y->kind) ||
            !read->bytes(read->context,"transcript.field",x->field,strlen(x->field),y->field,strlen(y->field))) return false;
        switch(x->kind) {
        case AGENDA_WORD_SCALAR:
            if (!read->scalar(read->context,x->field,x->value.scalar,y->value.scalar)) return false;
            break;
        case AGENDA_WORD_PHYSICAL:
            if (!read->physical(read->context,x->field,x->value.scalar,y->value.scalar)) return false;
            break;
        case AGENDA_WORD_IDENTITY:
            if (!read->identity(read->context,x->field,x->role,x->value.scalar,y->value.scalar)) return false;
            if (x->role==CETTA_CHECKPOINT_FRAME && (x->value.scalar||y->value.scalar) &&
                read->frame_scope && !read->frame_scope(read->context,"transcript.frame_owner",
                    &a->frame_holds,x->value.scalar,&b->frame_holds,y->value.scalar)) return false;
            break;
        case AGENDA_WORD_ATOM:
            if (!read->atom(read->context,x->field,x->value.atom,y->value.atom)) return false;
            break;
        case AGENDA_WORD_BINDINGS:
            if (x->length>1u || y->length>1u ||
                !read->scalar(read->context,"transcript.binding_owning_cell",x->length,y->length) ||
                (x->value.bindings!=NULL)!=(y->value.bindings!=NULL) ||
                !read->scalar(read->context,x->field,x->value.bindings!=NULL,y->value.bindings!=NULL)) return false;
            if (x->value.bindings && !bindings_checkpoint_compare(
                    &x->value.bindings->image,&y->value.bindings->image,read)) return false;
            break;
        case AGENDA_WORD_BYTES:
            if (x->length!=y->length || !read->bytes(read->context,x->field,
                    x->value.bytes,x->length,y->value.bytes,y->length)) return false;
            break;
        case AGENDA_WORD_TOKEN:
            if (!agenda_checkpoint_token_compare(x->field,x->value.token,y->value.token,read)) return false;
            break;
        }
    }
    return true;
}


bool cetta_space_agenda_transcript_compare(const CettaSpaceAgendaTranscript *a,
        const CettaSpaceAgendaTranscript *b, const CettaCheckpointCompare *read) {
    return agenda_word_compare(a,b,read,false);
}

bool cetta_space_agenda_transcript_compare_prefix(const CettaSpaceAgendaTranscript *recorded,
        const CettaSpaceAgendaTranscript *attempt, const CettaCheckpointCompare *read) {
    return agenda_word_compare(recorded,attempt,read,true);
}

#undef AW_S
#undef AW_F
#undef AW_A
#undef AW_I
#undef AW_B
#undef AW_T

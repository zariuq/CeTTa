#ifndef CETTA_EXPERIMENT_SHADOW_PROBE_H
#define CETTA_EXPERIMENT_SHADOW_PROBE_H
#include "atom.h"
#include "prepared_pure_machine.h"
#include "space.h"
extern _Thread_local unsigned gc_probe_guest_depth;
bool gc_probe_shadow_begin(size_t choices);
void gc_probe_shadow_end(void);
void gc_probe_visit_id(SymbolId id, const char *owner);
void gc_probe_visit_atom(const Atom *atom, const char *owner);
void gc_probe_visit_space(const Space *space, const char *owner);
void gc_probe_visit_bindings(const Bindings *bindings, const char *owner);
void gc_probe_visit_named_states(const Registry *registry);
void gc_probe_opaque(const char *owner);
void gc_probe_visit_prepared(const CettaPreparedPureAnswerCursor *cursor);
uint64_t gc_probe_missed_reads(void);
bool gc_probe_is_marked(SymbolId id);
#endif

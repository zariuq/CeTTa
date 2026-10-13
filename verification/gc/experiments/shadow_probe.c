/* Non-destructive mark/use experiment. The use oracle observes wrapped text,
 * length, hash and pin operations. Raw-ID-only comparisons are NOT covered.
 * An observed miss refutes this scanner; zero misses does not prove coverage. */
#define _GNU_SOURCE
#include "shadow_probe.h"
#include "term_graph.h"
#include <dlfcn.h>
#include <execinfo.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Thread_local unsigned gc_probe_guest_depth;
static _Thread_local SymbolShadowScan shadow;
static _Thread_local uint8_t *candidates, *seen_misses;
static _Thread_local const void **visited;
static _Thread_local size_t visited_len, visited_cap;
static _Thread_local bool scanning;
static uint64_t polls, scans, choice_scans, max_choices, misses, opaque;
static uint64_t marked_peak, unmarked_peak, outdated;
static const char *opaque_names[16];
static uint64_t opaque_counts[16];
static size_t opaque_name_count;

static bool omitted(const char *owner) {
    const char *omit = getenv("CETTA_GC_SHADOW_OMIT");
    return omit && (strcmp(omit, owner) == 0 || strcmp(omit, "all") == 0);
}
static bool remember(const void *pointer) {
    if (!pointer) return false;
    if (!visited_cap || visited_len >= visited_cap / 2u) {
        size_t capacity = visited_cap ? visited_cap * 2u : 1024u;
        const void **next = calloc(capacity, sizeof(*next)); assert(next);
        for (size_t i = 0; i < visited_cap; i++) {
            if (!visited[i]) continue;
            size_t slot = ((uintptr_t)visited[i] >> 4u) % capacity;
            while (next[slot]) slot = (slot + 1u) % capacity;
            next[slot] = visited[i];
        }
        free(visited); visited = next; visited_cap = capacity;
    }
    size_t slot = ((uintptr_t)pointer >> 4u) % visited_cap;
    while (visited[slot] && visited[slot] != pointer) slot = (slot + 1u) % visited_cap;
    if (visited[slot]) return false;
    visited[slot] = pointer; visited_len++;
    return true;
}
void gc_probe_opaque(const char *owner) {
    opaque++;
    size_t i = 0;
    while (i < opaque_name_count && strcmp(opaque_names[i], owner) != 0) i++;
    if (i == opaque_name_count && i < 16) opaque_names[opaque_name_count++] = owner;
    if (i < 16) opaque_counts[i]++;
}
void gc_probe_visit_id(SymbolId id, const char *owner) {
    if (!omitted(owner)) (void)symbol_shadow_mark(&shadow, id);
}
void gc_probe_visit_space(const Space *space, const char *owner) {
    if (!space || omitted(owner) || !remember(space)) return;
    for (CettaIndex i = 0; i < space_length64(space); i++)
        gc_probe_visit_atom(space_get_at64(space, i), owner);
}
void gc_probe_visit_bindings(const Bindings *bindings, const char *owner) {
    if (!bindings || omitted(owner)) return;
    BindingsIterator it = {.bindings = bindings};
    Binding binding;
    while (bindings_iterator_next(&it, &binding)) {
        gc_probe_visit_id(binding.spelling, owner);
        gc_probe_visit_atom(binding.name_key, owner);
        gc_probe_visit_atom(binding.value.skeleton, owner);
    }
}
void gc_probe_visit_named_states(const Registry *registry) {
    if (!registry || omitted("named-state")) return;
    for (uint32_t i = 0; i < registry_state_count(registry); i++) {
        SymbolId key; Atom *value;
        assert(registry_state_entry(registry, i, &key, &value));
        gc_probe_visit_id(key, "named-state");
        gc_probe_visit_atom(value, "named-state");
    }
}
void gc_probe_visit_atom(const Atom *root, const char *owner) {
    if (!root || omitted(owner)) return;
    size_t len = 1, cap = 64;
    const Atom **stack = malloc(cap * sizeof(*stack)); assert(stack); stack[0] = root;
#define PUSH(atom) do { \
    const Atom *push_atom__ = (atom); \
    if (push_atom__) { \
        if (len == cap) { cap *= 2u; stack = realloc(stack, cap * sizeof(*stack)); assert(stack); } \
        stack[len++] = push_atom__; \
    } \
} while (0)
    while (len) {
        const Atom *node = stack[--len];
        if (!remember(node)) continue;
        switch (node->kind) {
        case ATOM_SYMBOL: case ATOM_VAR:
            gc_probe_visit_id(atom_symbol_current_id(node), owner);
            if (node->kind == ATOM_VAR) PUSH(node->name_key);
            break;
        case ATOM_EXPR:
            for (CettaExprIndex i = 0; i < node->expr.len; i++) PUSH(node->expr.elems[i]);
            break;
        case ATOM_GROUNDED:
            switch (node->ground.gkind) {
            case GV_SPACE: gc_probe_visit_space(node->ground.ptr, "spaces"); break;
            case GV_CAPTURE: {
                const CaptureClosure *c = node->ground.ptr;
                if (c && !omitted("closures")) gc_probe_visit_space(c->space_ptr, "closures");
                break;
            }
            case GV_STATE: {
                const StateCell *s = node->ground.ptr;
                if (s && !omitted("state")) { PUSH(s->value); PUSH(s->content_type); }
                break;
            }
            case GV_BINDINGS: gc_probe_visit_bindings(node->ground.ptr, "bindings"); break;
            case GV_TERM_GRAPH: {
                const CettaTermGraphRef *ref = atom_term_graph_ref(node);
                if (ref && !omitted("term-graphs") && remember(ref->graph)) {
                    for (uint32_t i = 0; i < term_graph_node_count(ref->graph); i++) {
                        gc_probe_visit_id(term_graph_name(ref->graph, i), "term-graphs");
                        PUSH(term_graph_leaf(ref->graph, i));
                    }
                }
                break;
            }
            case GV_FOREIGN: case GV_PRIME_NEED_CAPABILITY: case GV_PRIME_CONTEXT:
                gc_probe_opaque("opaque-grounded-owner"); break;
            default: break;
            }
        }
    }
#undef PUSH
    free(stack);
}

bool gc_probe_shadow_begin(size_t choices) {
    const char *every = getenv("CETTA_GC_SHADOW_EVERY");
    if (!every) return false;
    unsigned long period = strtoul(every, NULL, 10);
    if (!period || ++polls % period != 0) return false;
    symbol_shadow_destroy(&shadow);
    free(candidates); free(seen_misses);
    assert(symbol_shadow_begin(g_symbols, &shadow));
    size_t bytes = ((size_t)shadow.entry_limit + 7u) / 8u;
    candidates = calloc(bytes, 1); seen_misses = calloc(bytes, 1);
    assert(candidates && seen_misses);
    for (SymbolId id = 1; id < shadow.entry_limit; id++)
        if (symbol_flags(g_symbols, id) & CETTA_SYMBOL_FLAG_RUNTIME_DATA)
            candidates[id >> 3u] |= 1u << (id & 7u);
    if (visited) memset(visited, 0, visited_cap * sizeof(*visited));
    visited_len = 0;
    scanning = true;
    scans++;
    if (choices) choice_scans++;
    if (choices > max_choices) max_choices = choices;
    return true;
}
void gc_probe_shadow_end(void) {
    SymbolShadowResult result;
    assert(symbol_shadow_finish(&shadow, &result));
    if (result.marked_data_bytes > marked_peak) marked_peak = result.marked_data_bytes;
    if (result.unmarked_data_bytes > unmarked_peak) unmarked_peak = result.unmarked_data_bytes;
    if (result.snapshot_outdated) outdated++;
    scanning = false;
}
const char *__real_symbol_bytes(const SymbolTable *, SymbolId);
uint32_t __real_symbol_len(const SymbolTable *, SymbolId);
static void observe(SymbolId id, const void *caller, const char *operation) {
    if (scanning || !shadow.marked || id >= shadow.entry_limit || !id) return;
    uint8_t bit = 1u << (id & 7u);
    if ((candidates[id >> 3u] & bit) && !(shadow.marked[id >> 3u] & bit) &&
        !(seen_misses[id >> 3u] & bit)) {
        seen_misses[id >> 3u] |= bit;
        misses++;
        if (misses <= 8) fprintf(stderr, "gc-shadow-missed-read: id=%u scan=%" PRIu64 "\n", id, scans);
        if (misses <= 8 && getenv("CETTA_GC_SHADOW_DIAG")) {
            Dl_info info = {0};
            dladdr(caller, &info);
            uintptr_t offset = (uintptr_t)caller - (uintptr_t)info.dli_fbase;
            fprintf(stderr, "gc-shadow-use: %s caller-offset=0x%" PRIxPTR " length=%u\n",
                    operation, offset, __real_symbol_len(shadow.table, id));
            if (misses == 1) {
                void *stack[18];
                int frames = backtrace(stack, 18);
                for (int i = 0; i < frames; i++) {
                    Dl_info frame = {0}; dladdr(stack[i], &frame);
                    if (frame.dli_fbase == info.dli_fbase)
                        fprintf(stderr, "gc-shadow-stack: 0x%" PRIxPTR "\n",
                                (uintptr_t)stack[i] - (uintptr_t)frame.dli_fbase);
                }
            }
        }
    }
}
uint64_t gc_probe_missed_reads(void) { return misses; }
bool gc_probe_is_marked(SymbolId id) {
    return shadow.marked && id < shadow.entry_limit &&
           (shadow.marked[id >> 3u] & (1u << (id & 7u)));
}
/* Finding the spelling anew through intern is legal after a hypothetical
 * sweep. It must not be confused with using an old retained numeric handle. */
void gc_probe_reacquired(SymbolTable *table, SymbolId id) {
    if (table == shadow.table && !scanning && shadow.marked && id < shadow.entry_limit)
        (void)symbol_shadow_mark(&shadow, id);
}
void gc_probe_observe_pin(SymbolTable *table, SymbolId id) {
    if (table == shadow.table) observe(id, __builtin_return_address(0), "pin");
}
const char *__wrap_symbol_bytes(const SymbolTable *table, SymbolId id) {
    if (table == shadow.table) observe(id, __builtin_return_address(0), "bytes");
    return __real_symbol_bytes(table, id);
}
uint32_t __wrap_symbol_len(const SymbolTable *table, SymbolId id) {
    if (table == shadow.table) observe(id, __builtin_return_address(0), "length");
    return __real_symbol_len(table, id);
}
uint64_t __real_symbol_hash_value(const SymbolTable *, SymbolId);
uint64_t __wrap_symbol_hash_value(const SymbolTable *table, SymbolId id) {
    if (table == shadow.table) observe(id, __builtin_return_address(0), "hash");
    return __real_symbol_hash_value(table, id);
}
SymbolId __real_symbol_intern_cstr(SymbolTable *, const char *);
SymbolId __wrap_symbol_intern_cstr(SymbolTable *table, const char *name) {
    SymbolId id = __real_symbol_intern_cstr(table, name); gc_probe_reacquired(table, id); return id;
}
SymbolId __real_symbol_intern_bytes(SymbolTable *, const uint8_t *, uint32_t);
SymbolId __wrap_symbol_intern_bytes(SymbolTable *table, const uint8_t *name, uint32_t len) {
    SymbolId id = __real_symbol_intern_bytes(table, name, len); gc_probe_reacquired(table, id); return id;
}
SymbolId __real_symbol_intern_span_hashed(SymbolTable *, const uint8_t *, uint32_t, uint64_t);
SymbolId __wrap_symbol_intern_span_hashed(SymbolTable *table, const uint8_t *name,
                                        uint32_t len, uint64_t hash) {
    SymbolId id = __real_symbol_intern_span_hashed(table, name, len, hash);
    gc_probe_reacquired(table, id); return id;
}
void gc_probe_shadow_summary(void) {
    if (!getenv("CETTA_GC_SHADOW_EVERY")) return;
    fprintf(stderr, "gc-shadow-probe: {\"polls\":%" PRIu64 ",\"scans\":%" PRIu64
            ",\"choice_scans\":%" PRIu64 ",\"max_choices\":%" PRIu64
            ",\"missed_reads\":%" PRIu64 ",\"opaque_visits\":%" PRIu64
            ",\"peak_marked_data_bytes\":%" PRIu64 ",\"peak_unmarked_data_bytes\":%" PRIu64
            ",\"outdated_snapshots\":%" PRIu64 ",\"opaque_owners\":[",
            polls, scans, choice_scans, max_choices, misses, opaque, marked_peak,
            unmarked_peak, outdated);
    for (size_t i = 0; i < opaque_name_count; i++)
        fprintf(stderr, "%s{\"owner\":\"%s\",\"visits\":%" PRIu64 "}",
                i ? "," : "", opaque_names[i], opaque_counts[i]);
    fputs("]}\n", stderr);
}

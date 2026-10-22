#ifndef CETTA_TERM_CANON_H
#define CETTA_TERM_CANON_H

#include "atom.h"
#include "var_index.h"

/* Results learned by one immutable graph walk, keyed by source and role.
 * Sources and images borrow the walk's complete graph/context lifetimes.
 * Distinct keys may have the same image. This is not an object-identity map,
 * a permission, or a cache for context-dependent/effectful calls.
 *
 * One writer owns a memo. A retained cursor must finish or be cancelled
 * before another writer starts. Readers may inspect the committed table
 * during growth; the replacement becomes authoritative only when complete.
 * Allocation and allocator retirement remain atomic services. */
typedef struct {
    const void *node;
    uint32_t aux;
    uintptr_t value;
} CettaNodeMemoSlot;

typedef struct {
    CettaNodeMemoSlot inline_slots[8];
    size_t inline_len;
    CettaNodeMemoSlot *slots;
    size_t cap, len;
    uint64_t revision;
} CettaNodeMemo;

typedef enum {
    CETTA_NODE_MEMO_PENDING,
    CETTA_NODE_MEMO_FOUND,
    CETTA_NODE_MEMO_ABSENT,
    CETTA_NODE_MEMO_STORED,
    CETTA_NODE_MEMO_REFUSED
} CettaNodeMemoStatus;

typedef struct {
    const CettaNodeMemo *memo;
    CettaNodeMemo *writer;
    CettaNodeMemoSlot *grown;
    CettaNodeMemoSlot carried;
    const void *node;
    uintptr_t value;
    size_t position, slot, inspected, grow_cap, clear_position, copy_position;
    size_t byte_limit, allocated_bytes;
    uint64_t revision, steps, probes;
    uint32_t aux;
    unsigned phase;
    CettaNodeMemoStatus status;
} CettaNodeMemoCursor;

/* Begin uses an empty cursor; cancel releases only an unpublished growth
 * buffer. Each advance inspects at most one table slot or initializes one
 * replacement slot. Absence, refusal and unfinished lookup are distinct.
 * byte_limit bounds simultaneous heap table storage, not embedded control. */
void cetta_node_memo_find_begin(CettaNodeMemoCursor *cursor,
    const CettaNodeMemo *memo, const void *node, uint32_t aux);
void cetta_node_memo_store_begin(CettaNodeMemoCursor *cursor,
    CettaNodeMemo *memo, const void *node, uint32_t aux, uintptr_t value,
    size_t byte_limit);
CettaNodeMemoStatus cetta_node_memo_advance(CettaNodeMemoCursor *cursor);
void cetta_node_memo_cancel(CettaNodeMemoCursor *cursor);
void cetta_node_memo_free(CettaNodeMemo *memo);

typedef bool (*CettaNodeMemoProbe)(void *context);
/* Synchronous clients may use a best-effort memo. A checked refusal never
 * publishes a partial entry; callers requiring sharing must propagate it. */
bool cetta_node_memo_find_checked(const CettaNodeMemo *memo, const void *node,
    uint32_t aux, uintptr_t *value, CettaNodeMemoProbe probe, void *context);
bool cetta_node_memo_find(const CettaNodeMemo *memo, const void *node,
    uint32_t aux, uintptr_t *value);
bool cetta_node_memo_store_checked(CettaNodeMemo *memo, const void *node,
    uint32_t aux, uintptr_t value, CettaNodeMemoProbe probe, void *context,
    size_t byte_limit);
void cetta_node_memo_store(CettaNodeMemo *memo, const void *node,
    uint32_t aux, uintptr_t value);

/*
 * Shared variable-remapping seam for canonical keys and re-materialization.
 *
 * Positive example:
 *   - TermUniverse and TableStore share one iterative graph walker while
 *     keeping their distinct variable policies.
 *
 * Negative example:
 *   - Two nearly identical VarId map implementations drifting apart in
 *     separate runtime subsystems.
 */

typedef struct {
    VarId source_id;
    Atom *mapped_var;
} CettaVarMapEntry;

typedef struct CettaVarMap {
    CettaVarMapEntry *items;
    uint32_t len;
    uint32_t cap;
    CettaVarIndex index;
} CettaVarMap;

void cetta_var_map_init(CettaVarMap *map);
void cetta_var_map_free(CettaVarMap *map);
bool cetta_var_map_reserve(CettaVarMap *map, uint32_t needed);
Atom *cetta_var_map_lookup(const CettaVarMap *map, VarId source_id);
bool cetta_var_map_add(CettaVarMap *map, VarId source_id, Atom *mapped_var);
bool cetta_var_map_clone(CettaVarMap *dst, const CettaVarMap *src);
bool cetta_var_map_clone_live(Arena *dst, CettaVarMap *out,
                              const CettaVarMap *src);

typedef Atom *(*CettaVarMapCreateFn)(Arena *dst, Atom *src_var,
                                     uint32_t ordinal, void *ctx);
Atom *cetta_var_map_get_or_add(CettaVarMap *map, Arena *dst, Atom *src_var,
                               CettaVarMapCreateFn create_var, void *ctx);

typedef Atom *(*CettaAtomRewriteVarFn)(Arena *dst, Atom *src_var, void *ctx);
Atom *cetta_atom_rewrite_vars(Arena *dst, Atom *src,
                              CettaAtomRewriteVarFn rewrite_var, void *ctx,
                              bool share_immutable);

/* Compile an authored variable inventory into frame-local execution syntax.
 * The map is an import boundary: equal authored identities share one slot;
 * distinct identities remain distinct even when their spellings agree.
 * Reuse the map across all terms belonging to the same frame. */
Atom *cetta_compile_frame_syntax(Arena *dst, Atom *src, CettaVarMap *inventory);
/* Import a live term: allocate coordinates for authored variables and preserve
 * identities that already belong to another activation. No substitution is applied. */
Atom *cetta_import_frame_syntax(Arena *dst, Atom *src, CettaVarMap *inventory,
                              CettaFrameIdentity identity);

/* Instantiate authored terms together in a fresh frame. Sharing between the
 * terms is preserved, while different source identities remain different even
 * when their spellings or local slots agree. The returned syntax owns its
 * identity. Roots are published together; failure leaves the original roots
 * unchanged and releases abandoned arena storage. This creates variables;
 * it does not reify an existing closure. */
bool cetta_instantiate_frame_terms(Arena *dst, Atom **terms, size_t count);
Atom *cetta_instantiate_frame_syntax(Arena *dst, Atom *source);
/* The same instantiation into new slots of a frame identity the caller owns
 * and keeps alive while the terms are in use.  Every call allocates new
 * slots, so terms instantiated by different calls share no variable; a
 * search that instantiates many rules therefore holds one identity rather
 * than one per instantiation. */
bool cetta_instantiate_frame_terms_within(Arena *dst, Atom **terms,
                                          size_t count,
                                          CettaFrameIdentity identity);

#endif /* CETTA_TERM_CANON_H */

#include "term_canon.h"

#include <stdlib.h>
#include <string.h>

enum {
    NODE_MEMO_INLINE, NODE_MEMO_TABLE, NODE_MEMO_RESERVE, NODE_MEMO_CLEAR,
    NODE_MEMO_SOURCE, NODE_MEMO_DESTINATION, NODE_MEMO_PUBLISH, NODE_MEMO_RETIRE
};

static size_t node_memo_slot(const void *node, uint32_t aux, size_t cap) {
    uint64_t key = (uint64_t)(uintptr_t)node ^ ((uint64_t)aux << 48u);
    key ^= key >> 33u;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 29u;
    return (size_t)key & (cap - 1u);
}

void cetta_node_memo_find_begin(CettaNodeMemoCursor *cursor,
    const CettaNodeMemo *memo, const void *node, uint32_t aux) {
    *cursor = (CettaNodeMemoCursor){.memo=memo, .node=node, .aux=aux,
        .revision=memo ? memo->revision : 0u,
        .status=memo && node && !memo->retiring ? CETTA_NODE_MEMO_PENDING : CETTA_NODE_MEMO_REFUSED};
}

void cetta_node_memo_store_begin(CettaNodeMemoCursor *cursor,
    CettaNodeMemo *memo, const void *node, uint32_t aux, uintptr_t value,
    size_t byte_limit) {
    cetta_node_memo_find_begin(cursor,memo,node,aux);
    cursor->writer=memo; cursor->value=value; cursor->byte_limit=byte_limit;
}

void cetta_node_memo_cancel(CettaNodeMemoCursor *cursor) {
    if (!cursor) return;
    cetta_segmented_array_free(&cursor->grown);
    *cursor=(CettaNodeMemoCursor){0};
}

bool cetta_node_memo_release_one(CettaNodeMemo *memo) {
    if (!memo) return true;
    memo->retiring=true;
    if (!cetta_segmented_array_release_one(&memo->slots)) return false;
    *memo=(CettaNodeMemo){0};
    return true;
}

void cetta_node_memo_free(CettaNodeMemo *memo) {
    while (!cetta_node_memo_release_one(memo)) {}
}

static CettaNodeMemoSlot *node_memo_entry(CettaNodeMemo *memo,size_t index) {
    return (CettaNodeMemoSlot *)cetta_node_memo_entry(memo,index);
}

static CettaNodeMemoStatus node_memo_observed(CettaNodeMemoCursor *cursor,
                                             const CettaNodeMemoSlot *entry) {
    if (!cursor->writer) {
        cursor->value=entry->value;
        return cursor->status=CETTA_NODE_MEMO_FOUND;
    }
    if (cursor->writer->revision==UINT64_MAX)
        return cursor->status=CETTA_NODE_MEMO_REFUSED;
    CettaNodeMemoSlot *target=cursor->phase==NODE_MEMO_INLINE
        ? &cursor->writer->inline_slots[cursor->position-1u]
        : node_memo_entry(cursor->writer,cursor->slot);
    target->value=cursor->value;
    cursor->revision=++cursor->writer->revision;
    return cursor->status=CETTA_NODE_MEMO_STORED;
}

static CettaNodeMemoStatus node_memo_absent(CettaNodeMemoCursor *cursor) {
    CettaNodeMemo *memo=cursor->writer;
    if (!memo) return cursor->status=CETTA_NODE_MEMO_ABSENT;
    if (memo->revision==UINT64_MAX)
        return cursor->status=CETTA_NODE_MEMO_REFUSED;
    size_t inline_cap=sizeof(memo->inline_slots)/sizeof(memo->inline_slots[0]);
    if (memo->inline_len<inline_cap) {
        memo->inline_slots[memo->inline_len++]=
            (CettaNodeMemoSlot){cursor->node,cursor->aux,cursor->value};
    } else if (memo->len>=memo->cap/2u) {
        cursor->phase=NODE_MEMO_RESERVE;
        return cursor->status;
    } else {
        *node_memo_entry(memo,cursor->slot)=
            (CettaNodeMemoSlot){cursor->node,cursor->aux,cursor->value};
        memo->len++;
    }
    cursor->revision=++memo->revision;
    return cursor->status=CETTA_NODE_MEMO_STORED;
}

CettaNodeMemoStatus cetta_node_memo_advance(CettaNodeMemoCursor *cursor) {
    if (cursor->status!=CETTA_NODE_MEMO_PENDING) return cursor->status;
    const CettaNodeMemo *memo=cursor->memo;
    if (!memo || memo->retiring || memo->revision!=cursor->revision || cursor->steps==UINT64_MAX)
        return cursor->status=CETTA_NODE_MEMO_REFUSED;
    cursor->steps++;
    switch (cursor->phase) {
    case NODE_MEMO_INLINE:
        if (cursor->position<memo->inline_len) {
            const CettaNodeMemoSlot *entry=&memo->inline_slots[cursor->position++];
            cursor->probes++;
            if (entry->node==cursor->node && entry->aux==cursor->aux)
                return node_memo_observed(cursor,entry);
            return cursor->status;
        }
        if (!memo->cap) return node_memo_absent(cursor);
        cursor->slot=node_memo_slot(cursor->node,cursor->aux,memo->cap);
        cursor->inspected=0u; cursor->phase=NODE_MEMO_TABLE;
        return cursor->status;
    case NODE_MEMO_TABLE: {
        if (cursor->inspected++==memo->cap)
            return cursor->status=CETTA_NODE_MEMO_REFUSED;
        const CettaNodeMemoSlot *entry=cetta_node_memo_entry(memo,cursor->slot);
        if (!entry) return cursor->status=CETTA_NODE_MEMO_REFUSED;
        cursor->probes++;
        if (!entry->node) return node_memo_absent(cursor);
        if (entry->node==cursor->node && entry->aux==cursor->aux)
            return node_memo_observed(cursor,entry);
        cursor->slot=(cursor->slot+1u)&(memo->cap-1u);
        return cursor->status;
    }
    case NODE_MEMO_RESERVE: {
        size_t cap=memo->cap ? memo->cap*2u : 64u;
        if (cap<memo->cap || cap>UINT32_MAX ||
            !cetta_segmented_array_init(&cursor->grown,sizeof(CettaNodeMemoSlot)))
            return cursor->status=CETTA_NODE_MEMO_REFUSED;
        cursor->grow_cap=cap;
        cursor->clear_position=cursor->copy_position=0u;
        cursor->phase=NODE_MEMO_CLEAR;
        return cursor->status;
    }
    case NODE_MEMO_CLEAR:
        if (cursor->clear_position<cursor->grow_cap) {
            if (memo->slots.live_bytes>cursor->byte_limit)
                return cursor->status=CETTA_NODE_MEMO_REFUSED;
            size_t before=cursor->grown.live_bytes;
            CettaSegmentedStep status=cetta_segmented_array_prepare_limit(&cursor->grown,
                (uint32_t)cursor->clear_position,cursor->byte_limit-memo->slots.live_bytes);
            cursor->allocated_bytes+=cursor->grown.live_bytes-before;
            if (status==CETTA_SEGMENTED_REFUSED) return cursor->status=CETTA_NODE_MEMO_REFUSED;
            if (status==CETTA_SEGMENTED_PENDING) return cursor->status;
            CettaNodeMemoSlot *slot=cetta_segmented_array_at(&cursor->grown,
                (uint32_t)cursor->clear_position++);
            *slot=(CettaNodeMemoSlot){0};
            return cursor->status;
        }
        cursor->phase=NODE_MEMO_SOURCE;
        return cursor->status;
    case NODE_MEMO_SOURCE:
        if (cursor->copy_position==memo->cap) {
            cursor->phase=NODE_MEMO_PUBLISH;
            return cursor->status;
        }
        cursor->carried=*cetta_node_memo_entry(memo,cursor->copy_position++);
        cursor->probes++;
        if (cursor->carried.node) {
            cursor->slot=node_memo_slot(cursor->carried.node,cursor->carried.aux,cursor->grow_cap);
            cursor->inspected=0u; cursor->phase=NODE_MEMO_DESTINATION;
        }
        return cursor->status;
    case NODE_MEMO_DESTINATION:
        if (cursor->inspected++==cursor->grow_cap)
            return cursor->status=CETTA_NODE_MEMO_REFUSED;
        cursor->probes++;
        CettaNodeMemoSlot *slot=cetta_segmented_array_at(&cursor->grown,(uint32_t)cursor->slot);
        if (!slot->node) {
            *slot=cursor->carried;
            cursor->phase=NODE_MEMO_SOURCE;
        } else cursor->slot=(cursor->slot+1u)&(cursor->grow_cap-1u);
        return cursor->status;
    case NODE_MEMO_PUBLISH: {
        if (cursor->writer->revision==UINT64_MAX)
            return cursor->status=CETTA_NODE_MEMO_REFUSED;
        CettaSegmentedArray old=cursor->writer->slots;
        cursor->writer->slots=cursor->grown;
        cursor->writer->cap=cursor->grow_cap;
        cursor->grown=old;
        cursor->revision=++cursor->writer->revision;
        cursor->phase=NODE_MEMO_RETIRE;
        return cursor->status;
    }
    case NODE_MEMO_RETIRE:
        if (cursor->grown.blocks) {
            cetta_segmented_array_release_one(&cursor->grown);
            return cursor->status;
        }
        cursor->slot=node_memo_slot(cursor->node,cursor->aux,cursor->writer->cap);
        cursor->inspected=0u; cursor->phase=NODE_MEMO_TABLE;
        return cursor->status;
    default:
        return cursor->status=CETTA_NODE_MEMO_REFUSED;
    }
}

bool cetta_node_memo_find_checked(const CettaNodeMemo *memo, const void *node,
    uint32_t aux, uintptr_t *value, CettaNodeMemoProbe probe, void *context) {
    CettaNodeMemoCursor cursor={0};
    cetta_node_memo_find_begin(&cursor,memo,node,aux);
    while (cursor.status==CETTA_NODE_MEMO_PENDING) {
        if (probe && !probe(context)) {cetta_node_memo_cancel(&cursor);return false;}
        cetta_node_memo_advance(&cursor);
    }
    bool found=cursor.status==CETTA_NODE_MEMO_FOUND;
    if (found && value) *value=cursor.value;
    cetta_node_memo_cancel(&cursor);
    return found;
}

bool cetta_node_memo_find(const CettaNodeMemo *memo, const void *node,
    uint32_t aux, uintptr_t *value) {
    return cetta_node_memo_find_checked(memo,node,aux,value,NULL,NULL);
}

bool cetta_node_memo_store_checked(CettaNodeMemo *memo, const void *node,
    uint32_t aux, uintptr_t value, CettaNodeMemoProbe probe, void *context,
    size_t byte_limit) {
    CettaNodeMemoCursor cursor={0};
    cetta_node_memo_store_begin(&cursor,memo,node,aux,value,byte_limit);
    while (cursor.status==CETTA_NODE_MEMO_PENDING) {
        if (probe && !probe(context)) {cetta_node_memo_cancel(&cursor);return false;}
        cetta_node_memo_advance(&cursor);
    }
    bool stored=cursor.status==CETTA_NODE_MEMO_STORED;
    cetta_node_memo_cancel(&cursor);
    return stored;
}

void cetta_node_memo_store(CettaNodeMemo *memo, const void *node,
    uint32_t aux, uintptr_t value) {
    (void)cetta_node_memo_store_checked(memo,node,aux,value,NULL,NULL,SIZE_MAX);
}

typedef struct {
    CettaAtomRewriteVarFn rewrite;
    void *context;
    bool share;
} RewriteMap;

static Atom *rewrite_map_leaf(Arena *dst, Atom *src, void *context) {
    RewriteMap *map = context;
    if (src->kind == ATOM_VAR)
        return map->rewrite(dst, src, map->context);
    return map->share ? atom_deep_copy_shared(dst, src) : atom_deep_copy(dst, src);
}

static Atom *frame_syntax_create_slot(
        Arena *dst, Atom *source, uint32_t ordinal, void *context) {
    (void)context;
    return ordinal ? atom_var_like(dst, source, var_id_from_base(ordinal)) : NULL;
}

static Atom *frame_syntax_rewrite_variable(Arena *dst, Atom *source, void *context) {
    CettaVarMap *inventory = context;
    if (inventory->len == UINT32_MAX &&
        !cetta_var_map_lookup(inventory, source->var_id))
        return NULL;
    return cetta_var_map_get_or_add(
        inventory, dst, source, frame_syntax_create_slot, NULL);
}

Atom *cetta_compile_frame_syntax(Arena *dst, Atom *src, CettaVarMap *inventory) {
    return inventory ? cetta_atom_rewrite_vars(
        dst, src, frame_syntax_rewrite_variable, inventory, true) : NULL;
}

typedef struct {
    CettaVarMap *inventory;
    CettaFrameIdentity identity;
} FrameSyntaxImport;

static Atom *frame_syntax_import_variable(Arena *dst, Atom *source, void *context) {
    FrameSyntaxImport *import = context;
    if (!cetta_frame_identity_is_none(var_epoch_suffix(source->var_id)))
        return atom_deep_copy_shared(dst, source);
    Atom *slot = frame_syntax_rewrite_variable(dst, source, import->inventory);
    return slot ? atom_var_like(dst, source, var_epoch_id(slot->var_id, import->identity))
                : NULL;
}

Atom *cetta_import_frame_syntax(Arena *dst, Atom *src, CettaVarMap *inventory,
                              CettaFrameIdentity identity) {
    FrameSyntaxImport import = {inventory, identity};
    return inventory && !cetta_frame_identity_is_none(identity)
        ? cetta_atom_rewrite_vars(
        dst, src, frame_syntax_import_variable, &import, true) : NULL;
}

static Atom *frame_instantiate_create_slot(
        Arena *dst, Atom *source, uint32_t ordinal, void *context) {
    (void)ordinal;
    CettaFrameIdentity identity = *(CettaFrameIdentity *)context;
    uint32_t slot;
    return cetta_frame_identity_new_slot(identity, 0u, &slot)
        ? atom_var_like(dst, source,
                       var_epoch_id(var_id_from_base(slot), identity))
        : NULL;
}

static Atom *frame_instantiate_variable(
        Arena *dst, Atom *source, void *context) {
    FrameSyntaxImport *instance = context;
    return cetta_var_map_get_or_add(instance->inventory, dst, source,
        frame_instantiate_create_slot, &instance->identity);
}

bool cetta_instantiate_frame_terms_within(Arena *dst, Atom **terms,
                                          size_t count,
                                          CettaFrameIdentity identity) {
    if (!dst || (count && !terms) || cetta_frame_identity_is_none(identity) ||
        count > SIZE_MAX / sizeof(Atom *))
        return false;
    if (!count)
        return true;
    ArenaMark mark = arena_mark(dst);
    size_t spare_before = dst->spare_bytes;
    Atom *inline_staged[8];
    Atom **staged = count <= 8u ? inline_staged
        : cetta_malloc(count * sizeof(*staged));
    CettaVarMap inventory;
    cetta_var_map_init(&inventory);
    FrameSyntaxImport instance = {&inventory, identity};
    RewriteMap map = {frame_instantiate_variable, &instance, true};
    AtomDeepCopySession *session =
        atom_deep_copy_session_new_mapped(dst, rewrite_map_leaf, &map);
    bool ok = session != NULL;
    for (size_t i = 0u; ok && i < count; i++) {
        staged[i] = atom_deep_copy_session_copy(session, terms[i]);
        ok = staged[i] != NULL;
    }
    atom_deep_copy_session_free(session);
    cetta_var_map_free(&inventory);
    if (ok) {
        memcpy(terms, staged, count * sizeof(*terms));
    } else {
        arena_reset(dst, mark);
        arena_release_spare(dst, spare_before);
    }
    if (staged != inline_staged)
        free(staged);
    return ok;
}

bool cetta_instantiate_frame_terms(Arena *dst, Atom **terms, size_t count) {
    if (!dst || (count && !terms))
        return false;
    CETTA_FRAME_IDENTITY_SCOPE(owner);
    CettaFrameIdentity identity;
    if (!cetta_frame_identity_scope_try(&owner, &identity))
        return false;
    return cetta_instantiate_frame_terms_within(dst, terms, count, identity);
}

Atom *cetta_instantiate_frame_syntax(Arena *dst, Atom *source) {
    return cetta_instantiate_frame_terms(dst, &source, 1u) ? source : NULL;
}

void cetta_var_map_init(CettaVarMap *map) {
    if (!map)
        return;
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
    cetta_var_index_init(&map->index);
}

void cetta_var_map_free(CettaVarMap *map) {
    if (!map)
        return;
    cetta_var_index_free(&map->index);
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
    cetta_var_index_init(&map->index);
}

bool cetta_var_map_reserve(CettaVarMap *map, uint32_t needed) {
    if (!map)
        return false;
    if (needed <= map->cap)
        return true;
    uint32_t next_cap = map->cap ? map->cap : 8u;
    while (next_cap < needed) {
        if (next_cap > UINT32_MAX / 2u) {
            next_cap = needed;
            break;
        }
        next_cap *= 2u;
    }
    if ((uint64_t)next_cap * sizeof(CettaVarMapEntry) > SIZE_MAX)
        return false;
    map->items = map->items
        ? cetta_realloc(map->items, sizeof(CettaVarMapEntry) * next_cap)
        : cetta_malloc(sizeof(CettaVarMapEntry) * next_cap);
    map->cap = next_cap;
    return true;
}

Atom *cetta_var_map_lookup(const CettaVarMap *map, VarId source_id) {
    if (!map)
        return NULL;
    size_t position = cetta_var_index_find_records(
        &map->index, map->items, sizeof(*map->items), map->len, source_id);
    return position == SIZE_MAX ? NULL : map->items[position].mapped_var;
}

bool cetta_var_map_add(CettaVarMap *map, VarId source_id, Atom *mapped_var) {
    Atom *existing = cetta_var_map_lookup(map, source_id);
    if (existing)
        return existing == mapped_var;
    if (!map || !mapped_var || map->len == UINT32_MAX ||
        !cetta_var_map_reserve(map, map->len + 1))
        return false;
    map->items[map->len].source_id = source_id;
    map->items[map->len].mapped_var = mapped_var;
    map->len++;
    if (!cetta_var_index_note_records(
            &map->index, map->items, sizeof(*map->items), map->len)) {
        map->len--;
        return false;
    }
    return true;
}

bool cetta_var_map_clone(CettaVarMap *dst, const CettaVarMap *src) {
    cetta_var_map_init(dst);
    if (!src || src->len == 0)
        return true;
    if (!cetta_var_map_reserve(dst, src->len))
        return false;
    memcpy(dst->items, src->items, sizeof(CettaVarMapEntry) * src->len);
    dst->len = src->len;
    if (!cetta_var_index_note_records(
            &dst->index, dst->items, sizeof(*dst->items), dst->len)) {
        cetta_var_map_free(dst);
        return false;
    }
    return true;
}

bool cetta_var_map_clone_live(Arena *dst, CettaVarMap *out,
                              const CettaVarMap *src) {
    cetta_var_map_init(out);
    if (!src || src->len == 0)
        return true;
    if (!dst || !cetta_var_map_reserve(out, src->len))
        return false;
    for (uint32_t i = 0; i < src->len; i++) {
        Atom *mapped = src->items[i].mapped_var
            ? atom_deep_copy(dst, src->items[i].mapped_var)
            : NULL;
        if (src->items[i].mapped_var && !mapped) {
            cetta_var_map_free(out);
            return false;
        }
        out->items[i].source_id = src->items[i].source_id;
        out->items[i].mapped_var = mapped;
    }
    out->len = src->len;
    if (!cetta_var_index_note_records(
            &out->index, out->items, sizeof(*out->items), out->len)) {
        cetta_var_map_free(out);
        return false;
    }
    return true;
}

Atom *cetta_var_map_get_or_add(CettaVarMap *map, Arena *dst, Atom *src_var,
                               CettaVarMapCreateFn create_var, void *ctx) {
    if (!map || !dst || !src_var || src_var->kind != ATOM_VAR || !create_var)
        return NULL;
    Atom *existing = cetta_var_map_lookup(map, src_var->var_id);
    if (existing)
        return existing;
    Atom *created = create_var(dst, src_var, map->len + 1, ctx);
    if (!created || !cetta_var_map_add(map, src_var->var_id, created))
        return NULL;
    return created;
}

Atom *cetta_atom_rewrite_vars(Arena *dst, Atom *src,
                              CettaAtomRewriteVarFn rewrite_var, void *ctx,
                              bool share_immutable) {
    if (!dst || !src || !rewrite_var)
        return NULL;
    RewriteMap map = {rewrite_var, ctx, share_immutable};
    return atom_deep_copy_mapped(dst, src, rewrite_map_leaf, &map);
}

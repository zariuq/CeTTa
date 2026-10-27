#include "library_dag.h"
#include "eval.h"
#include "native_handle.h"
#include "tensor.h"
#include "term_universe.h"
#include "u64_index.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Payloads and edge records live only in the existing exact structural store.
 * The two index vectors give its records dense chronological public ids. */
typedef struct {
    Arena arena;
    TermUniverse universe;
    AtomId leaf, node;
    AtomId *ids;
    size_t count, capacity, edges;
    size_t *numbers;
    size_t number_capacity;
    size_t *roots;
    size_t root_count;
} Dag;

static const char *const dag_kind = "dag";

static Atom *fault(Arena *arena, const char *reason) {
    return atom_expr2(arena, atom_symbol(arena, "Dag:Fault"), atom_symbol(arena, reason));
}

static void destroy(void *value) {
    Dag *dag = value;
    if (!dag) return;
    term_universe_free(&dag->universe);
    arena_free(&dag->arena);
    free(dag->ids); free(dag->numbers); free(dag->roots); free(dag);
}

static bool grow(void **items, size_t *capacity, size_t needed, size_t width) {
    if (needed <= *capacity) return true;
    size_t next = *capacity ? *capacity : 16;
    while (next < needed) {
        if (next > SIZE_MAX / 2) { next = needed; break; }
        next *= 2;
    }
    if (next > SIZE_MAX / width) return false;
    void *more = realloc(*items, next * width);
    if (!more) return false;
    *items = more; *capacity = next;
    return true;
}

static bool natural(Atom *atom, size_t *value) {
    if (!atom || atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_INT ||
        atom->ground.ival < 0 || (uint64_t)atom->ground.ival > SIZE_MAX) return false;
    *value = (size_t)atom->ground.ival;
    return true;
}

static bool sequence(Atom *atom, Atom *const **items, size_t *length) {
    if (!atom || atom->kind != ATOM_EXPR) return false;
    *items = atom->expr.elems; *length = atom->expr.len;
    return true;
}

static const char *payload(Dag *dag, Atom *atom, AtomId *id) {
    CettaTensor *tensor = cetta_tensor_from_atom(atom);
    if (!tensor || cetta_tensor_dtype(tensor) != CETTA_TENSOR_U8 ||
        cetta_tensor_rank(tensor) != 1 || cetta_tensor_count(tensor) != cetta_tensor_stored(tensor))
        return "InvalidPayload";
    size_t size = cetta_tensor_count(tensor);
    unsigned char *bytes = size ? malloc(size) : NULL;
    if (size && !bytes) return "Capacity";
    CettaTensorStatus copied = cetta_tensor_copy_values(tensor, bytes, size);
    if (copied == CETTA_TENSOR_ENCODING) {
        copied = CETTA_TENSOR_OK;
        for (size_t i = 0; i < size; i++) {
            if (eval_native_interrupted()) { free(bytes); return "Interrupted"; }
            bool present; CettaTensorScalar scalar;
            copied = cetta_tensor_get(tensor, i, &present, &scalar);
            if (copied != CETTA_TENSOR_OK || !present) { copied = CETTA_TENSOR_VALUE; break; }
            bytes[i] = scalar.u8;
        }
    }
    if (copied != CETTA_TENSOR_OK) { free(bytes); return "InvalidPayload"; }
    *id = tu_intern_string_n(&dag->universe, size ? (char *)bytes : "", size);
    free(bytes);
    return *id == CETTA_ATOM_ID_NONE ? "Capacity" : NULL;
}

static Atom *read_payload(Arena *arena, Dag *dag, AtomId id) {
    size_t size = tu_string_len(&dag->universe, id);
    const char *bytes = tu_string_cstr(&dag->universe, id);
    CettaTensor *tensor = NULL;
    CettaTensorStatus status = cetta_tensor_new(CETTA_TENSOR_U8, &size, 1, bytes, size, &tensor);
    if (status != CETTA_TENSOR_OK) return NULL;
    Atom *answer = cetta_tensor_atom(arena, tensor);
    cetta_tensor_release(tensor);
    return answer;
}

static const char *intern(Dag *dag, bool leaf, Atom *bytes, const size_t *children,
                         size_t count, size_t *number) {
    if (count > UINT32_MAX - 2u || count > SIZE_MAX / sizeof(AtomId) - 2u) return "Capacity";
    AtomId bytes_id;
    const char *error = payload(dag, bytes, &bytes_id);
    if (error) return error;
    AtomId *fields = malloc((count + 2) * sizeof(*fields));
    if (!fields) return "Capacity";
    fields[0] = leaf ? dag->leaf : dag->node;
    fields[1] = bytes_id;
    for (size_t i = 0; i < count; i++) {
        if (children[i] >= dag->count) { free(fields); return "InvalidReference"; }
        fields[i + 2] = dag->ids[children[i]];
    }
    AtomId id = tu_expr_from_ids(&dag->universe, fields, (CettaExprLen)count + 2u);
    free(fields);
    if (id == CETTA_ATOM_ID_NONE || id >= SIZE_MAX) return "Capacity";
    size_t previous = dag->number_capacity;
    if (!grow((void **)&dag->numbers, &dag->number_capacity, (size_t)id + 1u, sizeof(*dag->numbers)))
        return "Capacity";
    for (size_t i = previous; i < dag->number_capacity; i++) dag->numbers[i] = SIZE_MAX;
    if (dag->numbers[id] != SIZE_MAX) { *number = dag->numbers[id]; return NULL; }
    if (dag->count >= INT64_MAX || count > (size_t)INT64_MAX - dag->edges ||
        !grow((void **)&dag->ids, &dag->capacity, dag->count + 1u, sizeof(*dag->ids))) return "Capacity";
    *number = dag->count;
    dag->numbers[id] = dag->count;
    dag->ids[dag->count++] = id;
    dag->edges += count;
    return NULL;
}

typedef struct {
    Atom *bytes;
    Atom *const *children;
    size_t count, next;
    size_t *resolved;
    uint32_t memo_slot;
    bool leaf;
} Frame;

typedef struct {
    CettaU64Index index;
    size_t *values;
    size_t count, capacity;
} SourceMemo;

static const char *memo_slot(SourceMemo *memo, Atom *source, uint32_t *slot, bool *fresh) {
    uint64_t key = (uint64_t)(uintptr_t)source;
    if (cetta_u64_index_find(&memo->index, key, slot)) { *fresh = false; return NULL; }
    if (memo->count >= UINT32_MAX ||
        !grow((void **)&memo->values, &memo->capacity, memo->count + 1u, sizeof(*memo->values)))
        return "Capacity";
    *slot = (uint32_t)memo->count;
    if (cetta_u64_index_insert(&memo->index, key, *slot) != CETTA_GSLT_U32_INDEX_INSERTED_V1) return "Capacity";
    memo->values[memo->count++] = SIZE_MAX;
    *fresh = true;
    return NULL;
}

static const char *frame(Atom *tree, Frame *out) {
    if (!tree || tree->kind != ATOM_EXPR || tree->expr.len < 2u) return "InvalidTree";
    *out = (Frame){.bytes = tree->expr.elems[1]};
    if (atom_is_symbol(tree->expr.elems[0], "Binary:Leaf") && tree->expr.len == 2u)
        out->leaf = true;
    else if (!(atom_is_symbol(tree->expr.elems[0], "Binary:Node") && tree->expr.len == 3u &&
               sequence(tree->expr.elems[2], &out->children, &out->count))) return "InvalidTree";
    if (out->count > SIZE_MAX / sizeof(*out->resolved)) return "Capacity";
    out->resolved = out->count ? malloc(out->count * sizeof(*out->resolved)) : NULL;
    return out->count && !out->resolved ? "Capacity" : NULL;
}

static const char *add_tree(Dag *dag, SourceMemo *memo, Atom *tree, size_t *root) {
    Frame *frames = NULL;
    size_t length = 0, capacity = 0;
    uint32_t slot;
    bool fresh;
    const char *error = memo_slot(memo, tree, &slot, &fresh);
    if (error) return error;
    if (!fresh) {
        if (memo->values[slot] == SIZE_MAX) return "Cycle";
        *root = memo->values[slot]; return NULL;
    }
    if (!grow((void **)&frames, &capacity, 1, sizeof(*frames))) return "Capacity";
    error = frame(tree, &frames[0]);
    if (!error) { frames[0].memo_slot = slot; length = 1; }
    while (!error && length) {
        if (eval_native_interrupted()) { error = "Interrupted"; break; }
        Frame *current = &frames[length - 1];
        if (current->next < current->count) {
            Atom *child = current->children[current->next];
            error = memo_slot(memo, child, &slot, &fresh);
            if (error) break;
            if (!fresh) {
                if (memo->values[slot] == SIZE_MAX) { error = "Cycle"; break; }
                current->resolved[current->next++] = memo->values[slot];
                continue;
            }
            if (!grow((void **)&frames, &capacity, length + 1u, sizeof(*frames))) { error = "Capacity"; break; }
            error = frame(child, &frames[length]);
            if (!error) { frames[length].memo_slot = slot; length++; }
        } else {
            size_t value;
            error = intern(dag, current->leaf, current->bytes, current->resolved, current->count, &value);
            if (error) break;
            memo->values[current->memo_slot] = value;
            free(current->resolved);
            length--;
            if (length) frames[length - 1].resolved[frames[length - 1].next++] = value;
            else *root = value;
        }
    }
    for (size_t i = 0; i < length; i++) free(frames[i].resolved);
    free(frames);
    return error;
}

static Dag *create(size_t roots) {
    if (roots > SIZE_MAX / sizeof(size_t)) return NULL;
    Dag *dag = calloc(1, sizeof(*dag));
    if (!dag) return NULL;
    arena_init_detached(&dag->arena);
    arena_set_block_growth(&dag->arena, 4096);
    term_universe_init(&dag->universe);
    term_universe_set_persistent_arena(&dag->universe, &dag->arena);
    dag->leaf = tu_intern_symbol(&dag->universe, symbol_intern_cstr(g_symbols, "DagLeaf"));
    dag->node = tu_intern_symbol(&dag->universe, symbol_intern_cstr(g_symbols, "DagNode"));
    dag->roots = roots ? malloc(roots * sizeof(*dag->roots)) : NULL;
    dag->root_count = roots;
    if (dag->leaf == CETTA_ATOM_ID_NONE || dag->node == CETTA_ATOM_ID_NONE || (roots && !dag->roots)) {
        destroy(dag); return NULL;
    }
    return dag;
}

/* Private index capacity and structural-store allocations are charged once;
 * no unfolded Atom tree or second payload copy is retained by a published DAG. */
static bool charge(size_t *total, size_t count, size_t width) {
    if (count > SIZE_MAX / width || count * width > SIZE_MAX - *total) return false;
    *total += count * width;
    return true;
}

static size_t retained(Dag *dag) {
    TermUniverse *u = &dag->universe;
    size_t total = sizeof(*dag);
    bool valid = charge(&total, dag->capacity, sizeof(*dag->ids)) &&
        charge(&total, dag->number_capacity, sizeof(*dag->numbers)) &&
        charge(&total, dag->root_count, sizeof(*dag->roots)) && charge(&total, u->blob_cap, 1) &&
        charge(&total, u->cap, sizeof(*u->entries)) &&
        charge(&total, u->intern_mask + 1u, sizeof(uint64_t) + sizeof(AtomId)) &&
        charge(&total, u->ptr_mask + 1u, sizeof(void *) + sizeof(AtomId)) &&
        charge(&total, dag->arena.reserved_bytes, 1) && charge(&total, dag->arena.external_bytes, 1) &&
        charge(&total, dag->arena.symbol_cache_bytes, 1);
    return valid ? total : SIZE_MAX;
}

static Atom *publish(struct CettaLibraryContext *context, Arena *arena, Dag *dag) {
    uint64_t id;
    size_t bytes = retained(dag);
    if (bytes > INT64_MAX || !cetta_native_handle_alloc_sized(context, dag_kind, dag, destroy, bytes, &id)) {
        destroy(dag); return fault(arena, "Capacity");
    }
    Atom *handle = cetta_native_handle_owned_atom(context, arena, dag_kind, id);
    if (!handle) { cetta_native_handle_close(context, dag_kind, id); return fault(arena, "Capacity"); }
    return handle;
}

static Atom *record(Arena *arena, Dag *dag, size_t index, Atom **values) {
    AtomId id = dag->ids[index];
    size_t count = tu_arity(&dag->universe, id) - 2u;
    bool leaf = tu_child(&dag->universe, id, 0) == dag->leaf;
    Atom *bytes = read_payload(arena, dag, tu_child(&dag->universe, id, 1));
    if (!bytes) return NULL;
    Atom **children = count ? arena_alloc(arena, count * sizeof(*children)) : NULL;
    for (size_t i = 0; i < count; i++) {
        AtomId child = tu_child(&dag->universe, id, (CettaExprIndex)i + 2u);
        size_t number = dag->numbers[child];
        children[i] = values ? values[number] : atom_int(arena, (int64_t)number);
    }
    return leaf ? atom_expr2(arena, atom_symbol(arena, "Binary:Leaf"), bytes)
        : atom_expr3(arena, atom_symbol(arena, "Binary:Node"), bytes, atom_expr(arena, children, (CettaExprLen)count));
}

static Atom *roots(Arena *arena, Dag *dag) {
    Atom **items = dag->root_count ? arena_alloc(arena, dag->root_count * sizeof(*items)) : NULL;
    for (size_t i = 0; i < dag->root_count; i++) items[i] = atom_int(arena, (int64_t)dag->roots[i]);
    return atom_expr(arena, items, (CettaExprLen)dag->root_count);
}

static Atom *from_records(struct CettaLibraryContext *context, Arena *arena, Atom *wire) {
    Atom *const *source_roots, *const *records;
    size_t root_count, count;
    if (!wire || wire->kind != ATOM_EXPR || wire->expr.len != 3u ||
        !atom_is_symbol(wire->expr.elems[0], "Dag:Forest") ||
        !sequence(wire->expr.elems[1], &source_roots, &root_count) ||
        !sequence(wire->expr.elems[2], &records, &count)) return fault(arena, "InvalidRecords");
    if (count > SIZE_MAX / sizeof(size_t)) return fault(arena, "Capacity");
    Dag *dag = create(root_count);
    size_t *mapping = count ? malloc(count * sizeof(*mapping)) : NULL;
    if (!dag || (count && !mapping)) { destroy(dag); free(mapping); return fault(arena, "Capacity"); }
    const char *error = NULL;
    for (size_t i = 0; !error && i < count; i++) {
        if (eval_native_interrupted()) { error = "Interrupted"; break; }
        Frame item;
        error = frame(records[i], &item);
        if (error) break;
        for (size_t j = 0; j < item.count; j++) {
            size_t child;
            if (!natural(item.children[j], &child) || child >= i) { error = "InvalidReference"; break; }
            item.resolved[j] = mapping[child];
        }
        if (!error) error = intern(dag, item.leaf, item.bytes, item.resolved, item.count, &mapping[i]);
        free(item.resolved);
    }
    for (size_t i = 0; !error && i < root_count; i++) {
        size_t root;
        if (!natural(source_roots[i], &root) || root >= count) error = "InvalidRoot";
        else dag->roots[i] = mapping[root];
    }
    free(mapping);
    if (error) { destroy(dag); return fault(arena, error); }
    return publish(context, arena, dag);
}

bool cetta_dag_module_call(struct CettaLibraryContext *context, struct Space *space,
        Arena *arena, Atom *head, Atom **args, uint32_t count, CettaCallOutcome *out) {
    (void)space;
    if (!head || head->kind != ATOM_SYMBOL) return false;
    const char *name = atom_name_cstr(head);
    bool build = !strcmp(name, "dag:from-forest"), import = !strcmp(name, "dag:from-records");
    bool export = !strcmp(name, "dag:export"), get_roots = !strcmp(name, "dag:roots");
    bool get_node = !strcmp(name, "dag:node"), unfold = !strcmp(name, "dag:unfold");
    bool stats = !strcmp(name, "dag:stats"), close = !strcmp(name, "dag:close");
    if (!(build || import || export || get_roots || get_node || unfold || stats || close) ||
        count != (get_node || unfold ? 2u : 1u)) return false;
    Atom *answer = NULL;
    if (import) answer = from_records(context, arena, args[0]);
    else if (build) {
        Atom *const *trees; size_t length;
        if (!sequence(args[0], &trees, &length)) return false;
        Dag *dag = create(length);
        SourceMemo memo = {0};
        cetta_u64_index_init(&memo.index);
        const char *error = dag ? NULL : "Capacity";
        for (size_t i = 0; !error && i < length; i++) error = add_tree(dag, &memo, trees[i], &dag->roots[i]);
        cetta_u64_index_free(&memo.index);
        free(memo.values);
        if (error) { destroy(dag); answer = fault(arena, error); }
        else answer = publish(context, arena, dag);
    } else {
        uint64_t owned_id;
        if (!cetta_native_handle_arg(args[0], dag_kind, &owned_id)) return false;
        Dag *dag = cetta_native_handle_get_owned(context, args[0], dag_kind);
        if (!dag) answer = fault(arena, "Released");
        else if (close) {
            cetta_native_handle_close(context, dag_kind, owned_id);
            answer = atom_unit(arena);
        } else if (get_roots) answer = roots(arena, dag);
        else if (stats) {
            Atom *fields[] = {atom_symbol(arena, "Dag:Stats"), atom_int(arena, (int64_t)dag->count),
                atom_int(arena, (int64_t)dag->edges), atom_int(arena, (int64_t)retained(dag))};
            answer = atom_expr(arena, fields, 4);
        } else if (export) {
            Atom **records = dag->count ? arena_alloc(arena, dag->count * sizeof(*records)) : NULL;
            for (size_t i = 0; i < dag->count && !eval_native_interrupted(); i++) {
                records[i] = record(arena, dag, i, NULL);
                if (!records[i]) { answer = fault(arena, "Capacity"); break; }
            }
            if (!answer && !eval_native_interrupted()) answer = atom_expr3(arena,
                atom_symbol(arena, "Dag:Forest"), roots(arena, dag), atom_expr(arena, records, (CettaExprLen)dag->count));
        } else {
            size_t number;
            if (!natural(args[1], &number) || number >= dag->count) answer = fault(arena, "InvalidReference");
            else if (get_node) answer = record(arena, dag, number, NULL);
            else {
                Atom **values = arena_alloc(arena, (number + 1u) * sizeof(*values));
                for (size_t i = 0; i <= number && !eval_native_interrupted(); i++) {
                    values[i] = record(arena, dag, i, values);
                    if (!values[i]) { answer = fault(arena, "Capacity"); break; }
                }
                if (!answer && !eval_native_interrupted()) answer = values[number];
            }
        }
    }
    *out = eval_native_interrupted() ? cetta_call_interrupted()
        : cetta_call_completed_value(answer ? answer : fault(arena, "Capacity"));
    return true;
}

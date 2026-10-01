#include "term_graph.h"

#include "petta_semantics.h"
#include "symbol.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    CettaTermGraphKind kind;
    uint32_t arity;
    uint32_t first_child;
    SymbolId name;
    Atom *leaf;
    /* A parameter's index. */
    uint32_t param;
} TermGraphNode;

struct CettaTermGraph {
    atomic_uint_fast64_t references;
    Arena leaves;
    TermGraphNode *nodes;
    CettaTermGraphRef *refs;
    uint32_t node_len;
    uint32_t node_cap;
    uint32_t *children;
    uint32_t child_len;
    uint32_t child_cap;
    bool *infinite;
    /* The parameters a node reaches, ascending: param_ids[param_first[n]]
     * on, param_len[n] of them. */
    uint32_t param_count;
    uint32_t *param_first;
    uint32_t *param_len;
    uint32_t *param_ids;
    bool sealed;
};

CettaTermGraph *term_graph_new(void) {
    CettaTermGraph *graph = cetta_malloc(sizeof(*graph));
    if (!graph)
        return NULL;
    *graph = (CettaTermGraph){0};
    atomic_init(&graph->references, 1u);
    arena_init_detached(&graph->leaves);
    /* A graph's leaves are few: a small block, not a full one per term. */
    arena_set_block_capacity(&graph->leaves, 1024u);
    return graph;
}

void term_graph_retain(void *graph) {
    CettaTermGraph *g = graph;
    if (g)
        atomic_fetch_add_explicit(&g->references, 1u, memory_order_relaxed);
}

void term_graph_release(void *graph) {
    CettaTermGraph *g = graph;
    if (!g ||
        atomic_fetch_sub_explicit(&g->references, 1u,
                                  memory_order_acq_rel) != 1u)
        return;
    arena_free(&g->leaves);
    free(g->nodes);
    free(g->refs);
    free(g->children);
    free(g->infinite);
    free(g->param_first);
    free(g->param_len);
    free(g->param_ids);
    free(g);
}

static uint32_t term_graph_add(CettaTermGraph *graph, TermGraphNode node) {
    if (!graph || graph->sealed || graph->node_len == UINT32_MAX - 1u)
        return CETTA_TERM_GRAPH_NO_NODE;
    if (node.arity > UINT32_MAX - graph->child_len)
        return CETTA_TERM_GRAPH_NO_NODE;
    if (graph->node_len == graph->node_cap) {
        uint32_t next = graph->node_cap ? graph->node_cap * 2u : 16u;
        if (next <= graph->node_cap)
            return CETTA_TERM_GRAPH_NO_NODE;
        TermGraphNode *nodes = cetta_realloc(
            graph->nodes, sizeof(*nodes) * (size_t)next);
        if (!nodes)
            return CETTA_TERM_GRAPH_NO_NODE;
        graph->nodes = nodes;
        graph->node_cap = next;
    }
    uint32_t needed = graph->child_len + node.arity;
    if (needed > graph->child_cap) {
        uint32_t next = graph->child_cap ? graph->child_cap : 16u;
        while (next < needed) {
            if (next > UINT32_MAX / 2u)
                return CETTA_TERM_GRAPH_NO_NODE;
            next *= 2u;
        }
        uint32_t *children = cetta_realloc(
            graph->children, sizeof(*children) * (size_t)next);
        if (!children)
            return CETTA_TERM_GRAPH_NO_NODE;
        graph->children = children;
        graph->child_cap = next;
    }
    node.first_child = graph->child_len;
    for (uint32_t index = 0u; index < node.arity; index++)
        graph->children[graph->child_len + index] = CETTA_TERM_GRAPH_NO_NODE;
    graph->child_len = needed;
    graph->nodes[graph->node_len] = node;
    return graph->node_len++;
}

uint32_t term_graph_add_leaf(CettaTermGraph *graph, Atom *leaf) {
    Atom *copy = graph && leaf ? atom_deep_copy(&graph->leaves, leaf) : NULL;
    if (!copy)
        return CETTA_TERM_GRAPH_NO_NODE;
    return term_graph_add(graph, (TermGraphNode){
        .kind = CETTA_TERM_GRAPH_LEAF, .leaf = copy});
}

uint32_t term_graph_add_param(CettaTermGraph *graph, uint32_t index) {
    if (!graph || index == UINT32_MAX)
        return CETTA_TERM_GRAPH_NO_NODE;
    uint32_t node = term_graph_add(graph, (TermGraphNode){
        .kind = CETTA_TERM_GRAPH_PARAM, .param = index});
    if (node != CETTA_TERM_GRAPH_NO_NODE && index >= graph->param_count)
        graph->param_count = index + 1u;
    return node;
}

uint32_t term_graph_add_nil(CettaTermGraph *graph) {
    return term_graph_add(graph, (TermGraphNode){
        .kind = CETTA_TERM_GRAPH_NIL});
}

uint32_t term_graph_add_cell(CettaTermGraph *graph) {
    return term_graph_add(graph, (TermGraphNode){
        .kind = CETTA_TERM_GRAPH_CELL, .arity = 2u});
}

uint32_t term_graph_add_compound(CettaTermGraph *graph, SymbolId name,
                                 uint32_t arity) {
    if (name == SYMBOL_ID_NONE)
        return CETTA_TERM_GRAPH_NO_NODE;
    return term_graph_add(graph, (TermGraphNode){
        .kind = CETTA_TERM_GRAPH_COMPOUND, .arity = arity, .name = name});
}

bool term_graph_set_child(CettaTermGraph *graph, uint32_t node,
                          uint32_t index, uint32_t child) {
    if (!graph || graph->sealed || node >= graph->node_len ||
        child >= graph->node_len || index >= graph->nodes[node].arity)
        return false;
    graph->children[graph->nodes[node].first_child + index] = child;
    return true;
}

static int term_graph_compare_ids(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return a < b ? -1 : a > b;
}

/*
 * The parameters each node reaches.  The nodes of one strongly connected
 * component reach the same ones, and Tarjan's order numbers a component
 * after every component it reaches, so one pass in that order gathers each
 * component's own parameters and those of the components below it.
 */
static bool term_graph_seal_params(CettaTermGraph *graph,
                                   const uint32_t *component,
                                   uint32_t components) {
    uint32_t n = graph->node_len;
    size_t slots = (size_t)(n ? n : 1u);
    graph->param_first = cetta_malloc(sizeof(uint32_t) * slots);
    graph->param_len = cetta_malloc(sizeof(uint32_t) * slots);
    if (!graph->param_first || !graph->param_len)
        return false;
    if (graph->param_count == 0u) {
        memset(graph->param_first, 0, sizeof(uint32_t) * slots);
        memset(graph->param_len, 0, sizeof(uint32_t) * slots);
        return true;
    }
    uint32_t *member_first = cetta_malloc(
        sizeof(uint32_t) * ((size_t)components + 1u));
    uint32_t *members = cetta_malloc(sizeof(uint32_t) * slots);
    uint32_t *set_first = cetta_malloc(
        sizeof(uint32_t) * (components ? components : 1u));
    uint32_t *set_len = cetta_malloc(
        sizeof(uint32_t) * (components ? components : 1u));
    uint32_t *gather = NULL;
    size_t gather_cap = 0u;
    size_t ids_cap = 0u;
    size_t ids_len = 0u;
    bool ok = member_first && members && set_first && set_len;
    if (ok) {
        memset(member_first, 0, sizeof(uint32_t) * ((size_t)components + 1u));
        for (uint32_t node = 0u; node < n; node++)
            member_first[component[node] + 1u]++;
        for (uint32_t id = 0u; id < components; id++)
            member_first[id + 1u] += member_first[id];
        uint32_t *fill = cetta_malloc(
            sizeof(uint32_t) * (components ? components : 1u));
        ok = fill != NULL;
        if (ok) {
            memcpy(fill, member_first,
                   sizeof(uint32_t) * (components ? components : 1u));
            for (uint32_t node = 0u; node < n; node++)
                members[fill[component[node]]++] = node;
            free(fill);
        }
    }
    for (uint32_t id = 0u; ok && id < components; id++) {
        size_t length = 0u;
        for (uint32_t m = member_first[id]; ok && m < member_first[id + 1u];
             m++) {
            const TermGraphNode *node = &graph->nodes[members[m]];
            size_t need = length + 1u;
            for (uint32_t edge = 0u; edge < node->arity; edge++) {
                uint32_t child = graph->children[node->first_child + edge];
                if (component[child] != id)
                    need += set_len[component[child]];
            }
            if (need > gather_cap) {
                size_t next = gather_cap ? gather_cap : 16u;
                while (next < need)
                    next *= 2u;
                uint32_t *grown = cetta_realloc(gather,
                                                sizeof(*gather) * next);
                if (!grown) {
                    ok = false;
                    break;
                }
                gather = grown;
                gather_cap = next;
            }
            if (node->kind == CETTA_TERM_GRAPH_PARAM)
                gather[length++] = node->param;
            for (uint32_t edge = 0u; edge < node->arity; edge++) {
                uint32_t child = graph->children[node->first_child + edge];
                uint32_t below = component[child];
                if (below == id)
                    continue;
                memcpy(gather + length,
                       graph->param_ids + set_first[below],
                       sizeof(uint32_t) * set_len[below]);
                length += set_len[below];
            }
        }
        if (!ok)
            break;
        if (length > 1u)
            qsort(gather, length, sizeof(*gather), term_graph_compare_ids);
        size_t unique = 0u;
        for (size_t index = 0u; index < length; index++) {
            if (unique == 0u || gather[unique - 1u] != gather[index])
                gather[unique++] = gather[index];
        }
        if (ids_len + unique > ids_cap) {
            size_t next = ids_cap ? ids_cap : 16u;
            while (next < ids_len + unique)
                next *= 2u;
            uint32_t *grown = cetta_realloc(graph->param_ids,
                                            sizeof(uint32_t) * next);
            if (!grown) {
                ok = false;
                break;
            }
            graph->param_ids = grown;
            ids_cap = next;
        }
        if (unique)
            memcpy(graph->param_ids + ids_len, gather,
                   sizeof(uint32_t) * unique);
        set_first[id] = (uint32_t)ids_len;
        set_len[id] = (uint32_t)unique;
        ids_len += unique;
    }
    for (uint32_t node = 0u; ok && node < n; node++) {
        graph->param_first[node] = set_first[component[node]];
        graph->param_len[node] = set_len[component[node]];
    }
    free(member_first);
    free(members);
    free(set_first);
    free(set_len);
    free(gather);
    return ok;
}

/*
 * A node's unfolding is infinite exactly when the node reaches a cycle.
 * Tarjan's algorithm, iteratively, emits each strongly connected component
 * after every component it reaches, so one pass in emission order decides
 * each component: infinite if it has a cycle (two nodes, or a node that is
 * its own child) or reaches an infinite one.
 */
bool term_graph_seal(CettaTermGraph *graph) {
    if (!graph || graph->sealed)
        return false;
    uint32_t n = graph->node_len;
    for (uint32_t index = 0u; index < graph->child_len; index++) {
        if (graph->children[index] == CETTA_TERM_GRAPH_NO_NODE)
            return false;
    }
    graph->refs = cetta_malloc(sizeof(*graph->refs) * (size_t)(n ? n : 1u));
    graph->infinite = cetta_malloc(sizeof(*graph->infinite) * (size_t)(n ? n : 1u));
    uint32_t *order = cetta_malloc(sizeof(*order) * (size_t)(n ? n : 1u));
    uint32_t *low = cetta_malloc(sizeof(*low) * (size_t)(n ? n : 1u));
    uint32_t *component = cetta_malloc(sizeof(*component) * (size_t)(n ? n : 1u));
    uint32_t *stack = cetta_malloc(sizeof(*stack) * (size_t)(n ? n : 1u));
    uint32_t *call_node = cetta_malloc(sizeof(*call_node) * (size_t)(n ? n : 1u));
    uint32_t *call_edge = cetta_malloc(sizeof(*call_edge) * (size_t)(n ? n : 1u));
    bool *on_stack = cetta_malloc(sizeof(*on_stack) * (size_t)(n ? n : 1u));
    bool *component_infinite = cetta_malloc(
        sizeof(*component_infinite) * (size_t)(n ? n : 1u));
    bool ok = graph->refs && graph->infinite && order && low && component &&
              stack && call_node && call_edge && on_stack &&
              component_infinite;
    if (ok) {
        for (uint32_t index = 0u; index < n; index++) {
            graph->refs[index] = (CettaTermGraphRef){graph, index};
            order[index] = CETTA_TERM_GRAPH_NO_NODE;
            on_stack[index] = false;
        }
        uint32_t next_order = 0u;
        uint32_t stack_len = 0u;
        uint32_t components = 0u;
        for (uint32_t start = 0u; start < n; start++) {
            if (order[start] != CETTA_TERM_GRAPH_NO_NODE)
                continue;
            uint32_t depth = 0u;
            call_node[0] = start;
            call_edge[0] = 0u;
            order[start] = low[start] = next_order++;
            stack[stack_len++] = start;
            on_stack[start] = true;
            depth = 1u;
            while (depth > 0u) {
                uint32_t node = call_node[depth - 1u];
                TermGraphNode *record = &graph->nodes[node];
                if (call_edge[depth - 1u] < record->arity) {
                    uint32_t child = graph->children[
                        record->first_child + call_edge[depth - 1u]++];
                    if (order[child] == CETTA_TERM_GRAPH_NO_NODE) {
                        order[child] = low[child] = next_order++;
                        stack[stack_len++] = child;
                        on_stack[child] = true;
                        call_node[depth] = child;
                        call_edge[depth] = 0u;
                        depth++;
                    } else if (on_stack[child] && order[child] < low[node]) {
                        low[node] = order[child];
                    }
                    continue;
                }
                if (low[node] == order[node]) {
                    /* The component of `node`: pop it, and see whether it
                     * has a cycle or reaches an infinite component. */
                    uint32_t id = components++;
                    uint32_t size = 0u;
                    bool infinite = false;
                    uint32_t member;
                    uint32_t base = stack_len;
                    do {
                        member = stack[--base];
                        size++;
                    } while (member != node);
                    for (uint32_t index = base; index < stack_len; index++) {
                        member = stack[index];
                        on_stack[member] = false;
                        component[member] = id;
                    }
                    for (uint32_t index = base; index < stack_len; index++) {
                        member = stack[index];
                        TermGraphNode *m = &graph->nodes[member];
                        for (uint32_t edge = 0u; edge < m->arity; edge++) {
                            uint32_t child =
                                graph->children[m->first_child + edge];
                            if (component[child] == id) {
                                if (size > 1u || child == member)
                                    infinite = true;
                            } else if (component_infinite[component[child]]) {
                                infinite = true;
                            }
                        }
                    }
                    component_infinite[id] = infinite;
                    stack_len = base;
                }
                depth--;
                if (depth > 0u) {
                    uint32_t parent = call_node[depth - 1u];
                    if (low[node] < low[parent])
                        low[parent] = low[node];
                }
            }
        }
        for (uint32_t index = 0u; index < n; index++)
            graph->infinite[index] = component_infinite[component[index]];
        ok = term_graph_seal_params(graph, component, components);
        graph->sealed = ok;
    }
    free(order);
    free(low);
    free(component);
    free(stack);
    free(call_node);
    free(call_edge);
    free(on_stack);
    free(component_infinite);
    return ok;
}

uint32_t term_graph_node_count(const CettaTermGraph *graph) {
    return graph ? graph->node_len : 0u;
}

CettaTermGraphKind term_graph_kind(const CettaTermGraph *graph,
                                   uint32_t node) {
    return graph->nodes[node].kind;
}

Atom *term_graph_leaf(const CettaTermGraph *graph, uint32_t node) {
    return graph->nodes[node].leaf;
}

SymbolId term_graph_name(const CettaTermGraph *graph, uint32_t node) {
    return graph->nodes[node].name;
}

uint32_t term_graph_arity(const CettaTermGraph *graph, uint32_t node) {
    return graph->nodes[node].arity;
}

uint32_t term_graph_child(const CettaTermGraph *graph, uint32_t node,
                          uint32_t index) {
    return graph->children[graph->nodes[node].first_child + index];
}

bool term_graph_infinite(const CettaTermGraph *graph, uint32_t node) {
    return graph->sealed && graph->infinite[node];
}

uint32_t term_graph_param(const CettaTermGraph *graph, uint32_t node) {
    return graph->nodes[node].param;
}

uint32_t term_graph_param_count(const CettaTermGraph *graph) {
    return graph ? graph->param_count : 0u;
}

const uint32_t *term_graph_node_params(const CettaTermGraph *graph,
                                       uint32_t node, uint32_t *count) {
    *count = graph->param_len[node];
    return *count ? graph->param_ids + graph->param_first[node] : NULL;
}

const CettaTermGraphRef *term_graph_ref(const CettaTermGraph *graph,
                                        uint32_t node) {
    return graph && graph->sealed && node < graph->node_len
        ? &graph->refs[node] : NULL;
}

/* ── Values ─────────────────────────────────────────────────────────────── */

/* The carrier tag, shared per arena epoch like PeTTa's cons tag. */
static Atom *term_graph_carrier_tag(Arena *arena) {
    static _Thread_local Atom *tag = NULL;
    static _Thread_local uint32_t tag_arena = 0u;
    static _Thread_local uint64_t tag_epoch = 0u;
    if (!tag || tag_arena != arena->identity ||
        tag_epoch != arena->reset_epoch) {
        tag = atom_internal_tag(arena, CETTA_INTERNAL_TAG_RATIONAL);
        tag_arena = arena->identity;
        tag_epoch = arena->reset_epoch;
    }
    return tag;
}

Atom *term_graph_node_value(Arena *arena, CettaTermGraph *graph,
                            uint32_t node, Atom *const *values) {
    uint32_t count = 0u;
    const uint32_t *params = term_graph_node_params(graph, node, &count);
    Atom *node_atom = atom_term_graph(arena, graph, node);
    if (!node_atom || count == 0u)
        return node_atom;
    Atom **items = arena_alloc(arena, sizeof(*items) * ((size_t)count + 2u));
    items[0] = term_graph_carrier_tag(arena);
    items[1] = node_atom;
    for (uint32_t index = 0u; index < count; index++) {
        items[index + 2u] = values ? values[params[index]] : NULL;
        if (!items[index + 2u])
            return NULL;
    }
    return items[0] ? atom_expr(arena, items, (CettaExprLen)count + 2u) : NULL;
}

bool term_graph_value_node(const Atom *atom, const CettaTermGraphRef **ref,
                           Atom *const **args, uint32_t *nargs) {
    if (atom_is_rational_node(atom)) {
        *ref = atom->ground.ptr;
        *args = NULL;
        *nargs = 0u;
        return true;
    }
    if (!atom_is_rational_value(atom))
        return false;
    *ref = atom->expr.elems[1]->ground.ptr;
    *args = atom->expr.elems + 2u;
    *nargs = (uint32_t)(atom->expr.len - 2u);
    return true;
}

/* A parameter's value in a carrier of `node`: the carrier's arguments follow
 * the node's parameters in ascending order. */
static Atom *term_graph_carrier_param(const CettaTermGraph *graph,
                                      uint32_t node, Atom *const *args,
                                      uint32_t nargs, uint32_t param) {
    uint32_t count = 0u;
    const uint32_t *params = term_graph_node_params(graph, node, &count);
    uint32_t high = count < nargs ? count : nargs;
    uint32_t low = 0u;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        if (params[mid] < param)
            low = mid + 1u;
        else
            high = mid;
    }
    return low < count && low < nargs && params[low] == param
        ? args[low] : NULL;
}

Atom *term_graph_value_param(const Atom *value, uint32_t param) {
    const CettaTermGraphRef *ref = NULL;
    Atom *const *args = NULL;
    uint32_t nargs = 0u;
    if (!term_graph_value_node(value, &ref, &args, &nargs) || nargs == 0u)
        return NULL;
    return term_graph_carrier_param(ref->graph, ref->node, args, nargs,
                                    param);
}

/* The values of a node's parameters from its carrier's arguments, indexed
 * by parameter; NULL for a node that reaches none. */
static Atom **term_graph_value_params(Arena *arena,
                                      const CettaTermGraphRef *ref,
                                      Atom *const *args, uint32_t nargs) {
    CettaTermGraph *graph = ref->graph;
    if (nargs == 0u || graph->param_count == 0u)
        return NULL;
    Atom **values = arena_alloc(arena,
                                sizeof(*values) * (size_t)graph->param_count);
    memset(values, 0, sizeof(*values) * (size_t)graph->param_count);
    uint32_t count = 0u;
    const uint32_t *params = term_graph_node_params(graph, ref->node, &count);
    for (uint32_t index = 0u; index < count && index < nargs; index++)
        values[params[index]] = args[index];
    return values;
}

/* ── One level open ─────────────────────────────────────────────────────── */

static Atom *term_graph_unfold(Arena *arena, CettaTermGraph *graph,
                               uint32_t node, uint32_t depth,
                               Atom *const *values);

/* A child as the atom it is below the opened level: its value if its
 * unfolding is infinite, else its ordinary atom. */
static Atom *term_graph_child_atom(Arena *arena, CettaTermGraph *graph,
                                   uint32_t node, uint32_t depth,
                                   Atom *const *values) {
    if (graph->infinite[node])
        return term_graph_node_value(arena, graph, node, values);
    return term_graph_unfold(arena, graph, node, depth + 1u, values);
}

static Atom *term_graph_unfold(Arena *arena, CettaTermGraph *graph,
                               uint32_t node, uint32_t depth,
                               Atom *const *values) {
    /* Nesting below the opened level, as the Prolog converter bounds it. */
    if (depth > 1024u)
        return NULL;
    TermGraphNode *record = &graph->nodes[node];
    switch (record->kind) {
    case CETTA_TERM_GRAPH_LEAF:
        return atom_deep_copy(arena, record->leaf);
    case CETTA_TERM_GRAPH_PARAM:
        /* The value itself, so a variable keeps its identity. */
        return values ? values[record->param] : NULL;
    case CETTA_TERM_GRAPH_NIL:
        return atom_expr(arena, NULL, 0u);
    case CETTA_TERM_GRAPH_COMPOUND: {
        Atom **items = cetta_malloc(
            sizeof(*items) * ((size_t)record->arity + 1u));
        if (!items)
            return NULL;
        items[0] = atom_symbol_id(arena, record->name);
        bool ok = items[0] != NULL;
        for (uint32_t index = 0u; ok && index < record->arity; index++) {
            items[index + 1u] = term_graph_child_atom(
                arena, graph,
                graph->children[record->first_child + index], depth, values);
            ok = items[index + 1u] != NULL;
        }
        Atom *body = ok
            ? atom_expr(arena, items, (CettaExprLen)record->arity + 1u)
            : NULL;
        free(items);
        return body ? atom_petta_prolog_compound(arena, body) : NULL;
    }
    case CETTA_TERM_GRAPH_CELL: {
        /* Follow the spine while it is cells not met before on it.  Ending
         * in the empty list, it is the expression of its heads; otherwise
         * its cells keep their tail. */
        size_t length = 0u;
        size_t capacity = 8u;
        uint32_t *heads = cetta_malloc(sizeof(*heads) * capacity);
        uint32_t *spine = cetta_malloc(sizeof(*spine) * capacity);
        if (!heads || !spine) {
            free(heads);
            free(spine);
            return NULL;
        }
        uint32_t cursor = node;
        bool revisited = false;
        while (graph->nodes[cursor].kind == CETTA_TERM_GRAPH_CELL) {
            /* Only a cell that reaches a cycle can come round again. */
            for (size_t index = 0u;
                 graph->infinite[cursor] && index < length; index++) {
                if (spine[index] == cursor) {
                    revisited = true;
                    break;
                }
            }
            if (revisited)
                break;
            if (length == capacity) {
                capacity *= 2u;
                uint32_t *grown_heads = cetta_realloc(
                    heads, sizeof(*heads) * capacity);
                uint32_t *grown_spine = grown_heads
                    ? cetta_realloc(spine, sizeof(*spine) * capacity)
                    : NULL;
                if (!grown_heads || !grown_spine) {
                    free(grown_heads ? grown_heads : heads);
                    free(spine);
                    return NULL;
                }
                heads = grown_heads;
                spine = grown_spine;
            }
            TermGraphNode *cell = &graph->nodes[cursor];
            spine[length] = cursor;
            heads[length++] = graph->children[cell->first_child];
            cursor = graph->children[cell->first_child + 1u];
        }
        Atom *result = NULL;
        Atom **items = cetta_malloc(sizeof(*items) * (length ? length : 1u));
        bool ok = items != NULL;
        for (size_t index = 0u; ok && index < length; index++) {
            items[index] = term_graph_child_atom(
                arena, graph, heads[index], depth, values);
            ok = items[index] != NULL;
        }
        if (ok && !revisited &&
            graph->nodes[cursor].kind == CETTA_TERM_GRAPH_NIL) {
            result = atom_expr(arena, items, (CettaExprLen)length);
        } else if (ok) {
            Atom *tail = revisited || graph->infinite[cursor]
                ? term_graph_node_value(arena, graph, cursor, values)
                : term_graph_unfold(arena, graph, cursor, depth + 1u, values);
            result = tail;
            for (size_t index = length; result && index > 0u; index--) {
                result = petta_semantics_open_cons_value(
                    arena, items[index - 1u], result);
            }
        }
        free(items);
        free(heads);
        free(spine);
        return result;
    }
    }
    return NULL;
}

Atom *term_graph_open(Arena *arena, CettaTermGraph *graph, uint32_t node,
                      Atom *const *values) {
    if (!arena || !graph || !graph->sealed || node >= graph->node_len)
        return NULL;
    return term_graph_unfold(arena, graph, node, 0u, values);
}

Atom *term_graph_open_value(Arena *arena, Atom *atom) {
    const CettaTermGraphRef *ref = NULL;
    Atom *const *args = NULL;
    uint32_t nargs = 0u;
    if (!term_graph_value_node(atom, &ref, &args, &nargs))
        return atom;
    return term_graph_open(arena, ref->graph, ref->node,
                           term_graph_value_params(arena, ref, args, nargs));
}

/* ── Equality ───────────────────────────────────────────────────────────── */

/* An atom read as Prolog cells.  `VIEW_ATOM` is the atom; `VIEW_SUFFIX` the
 * list of an expression's elements from `index` on; `VIEW_NODE` a graph
 * node, with the carrier whose arguments are its parameters' values when it
 * reaches any. */
typedef enum {
    VIEW_ATOM = 0,
    VIEW_SUFFIX,
    VIEW_NODE,
} TermViewKind;

typedef struct {
    TermViewKind kind;
    uint32_t index;
    const void *ptr;
    const Atom *env;
} TermView;

typedef enum {
    SHAPE_LEAF = 0,
    SHAPE_NIL,
    SHAPE_CELL,
    SHAPE_COMPOUND,
} TermShapeKind;

typedef struct {
    TermShapeKind kind;
    Atom *leaf;
    SymbolId name;
    uint32_t arity;
    /* Where the children are read. */
    TermView base;
} TermShape;

static TermView term_view_atom(Atom *atom) {
    const CettaTermGraphRef *ref = NULL;
    Atom *const *args = NULL;
    uint32_t nargs = 0u;
    if (term_graph_value_node(atom, &ref, &args, &nargs))
        return (TermView){VIEW_NODE, ref->node, ref->graph,
                          nargs ? atom : NULL};
    return (TermView){VIEW_ATOM, 0u, atom, NULL};
}

/* A parameter is read as its value.  A value never holds the carrier it is
 * an argument of, so this ends. */
static TermView term_view_resolve(TermView view) {
    while (view.kind == VIEW_NODE) {
        const CettaTermGraph *graph = view.ptr;
        const TermGraphNode *node = &graph->nodes[view.index];
        if (node->kind != CETTA_TERM_GRAPH_PARAM || !view.env)
            break;
        const Atom *env = view.env;
        const CettaTermGraphRef *ref = env->expr.elems[1]->ground.ptr;
        Atom *value = term_graph_carrier_param(
            graph, ref->node, env->expr.elems + 2u,
            (uint32_t)(env->expr.len - 2u), node->param);
        if (!value)
            break;
        view = term_view_atom(value);
    }
    return view;
}

static TermShape term_view_shape(TermView view) {
    view = term_view_resolve(view);
    if (view.kind == VIEW_NODE) {
        const CettaTermGraph *graph = view.ptr;
        const TermGraphNode *node = &graph->nodes[view.index];
        switch (node->kind) {
        case CETTA_TERM_GRAPH_LEAF:
            return (TermShape){.kind = SHAPE_LEAF, .leaf = node->leaf};
        case CETTA_TERM_GRAPH_PARAM:
            /* A parameter without a value: nothing equals it. */
            return (TermShape){.kind = SHAPE_LEAF, .leaf = NULL};
        case CETTA_TERM_GRAPH_NIL:
            return (TermShape){.kind = SHAPE_NIL};
        case CETTA_TERM_GRAPH_CELL:
            return (TermShape){.kind = SHAPE_CELL, .arity = 2u, .base = view};
        case CETTA_TERM_GRAPH_COMPOUND:
            return (TermShape){.kind = SHAPE_COMPOUND, .name = node->name,
                               .arity = node->arity, .base = view};
        }
    }
    Atom *atom = (Atom *)view.ptr;
    if (view.kind == VIEW_SUFFIX) {
        return view.index >= atom->expr.len
            ? (TermShape){.kind = SHAPE_NIL}
            : (TermShape){.kind = SHAPE_CELL, .arity = 2u, .base = view};
    }
    if (atom->kind != ATOM_EXPR)
        return (TermShape){.kind = SHAPE_LEAF, .leaf = atom};
    if (atom->expr.len == 0u)
        return (TermShape){.kind = SHAPE_NIL};
    Atom *body = NULL;
    if (atom_petta_prolog_compound_body(atom, &body) && body) {
        return (TermShape){.kind = SHAPE_COMPOUND,
                           .name = body->expr.elems[0]->sym_id,
                           .arity = (uint32_t)(body->expr.len - 1u),
                           .base = (TermView){VIEW_ATOM, 1u, body, NULL}};
    }
    if (petta_semantics_is_open_cons_value(atom)) {
        return (TermShape){.kind = SHAPE_CELL, .arity = 2u,
                           .base = (TermView){VIEW_ATOM, 1u, atom, NULL}};
    }
    return (TermShape){.kind = SHAPE_CELL, .arity = 2u,
                       .base = (TermView){VIEW_SUFFIX, 0u, atom, NULL}};
}

/* The `index`th child of a shape.  A node's child keeps the node's carrier
 * only when it reaches a parameter, so a ground node is one view however it
 * is reached. */
static TermView term_shape_child(const TermShape *shape, uint32_t index) {
    TermView base = shape->base;
    if (base.kind == VIEW_NODE) {
        const CettaTermGraph *graph = base.ptr;
        uint32_t child = term_graph_child(graph, base.index, index);
        return (TermView){VIEW_NODE, child, graph,
                          graph->param_len[child] ? base.env : NULL};
    }
    Atom *atom = (Atom *)base.ptr;
    if (base.kind == VIEW_SUFFIX) {
        /* A cell of the elements from base.index: its head and the rest. */
        return index == 0u
            ? term_view_atom(atom->expr.elems[base.index])
            : (TermView){VIEW_SUFFIX, base.index + 1u, atom, NULL};
    }
    /* A compound's arguments or a cons cell's fields, from base.index. */
    return term_view_atom(atom->expr.elems[base.index + index]);
}

/* ── Hash ───────────────────────────────────────────────────────────────── */

static uint32_t term_graph_hash_mix(uint32_t h, uint32_t v) {
    h ^= v + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h;
}

/*
 * The first nodes of an atom's unfolding in breadth-first order, their
 * shapes and labels: two atoms with the same unfolding have the same prefix
 * whatever part of either is open, so the hash agrees with every equality
 * that compares unfoldings (RationalTermGraph.observe_hash_compatible).
 */
uint32_t term_graph_view_hash(Atom *atom, uint32_t (*leaf_hash)(Atom *)) {
    enum { TERM_GRAPH_HASH_NODES = 64u };
    TermView queue[TERM_GRAPH_HASH_NODES];
    uint32_t head = 0u;
    uint32_t tail = 0u;
    uint32_t h = 0x5bd1e995u;
    if (!atom || !leaf_hash)
        return 0u;
    queue[tail++] = term_view_atom(atom);
    while (head < tail) {
        TermShape shape = term_view_shape(queue[head++]);
        h = term_graph_hash_mix(h, (uint32_t)shape.kind);
        switch (shape.kind) {
        case SHAPE_LEAF:
            h = term_graph_hash_mix(h, shape.leaf ? leaf_hash(shape.leaf) : 0u);
            continue;
        case SHAPE_NIL:
            continue;
        case SHAPE_COMPOUND: {
            uint64_t name = symbol_hash_value(g_symbols, shape.name);
            h = term_graph_hash_mix(h, (uint32_t)name);
            h = term_graph_hash_mix(h, (uint32_t)(name >> 32));
            h = term_graph_hash_mix(h, shape.arity);
            break;
        }
        case SHAPE_CELL:
            break;
        }
        for (uint32_t index = 0u;
             index < shape.arity && tail < TERM_GRAPH_HASH_NODES; index++)
            queue[tail++] = term_shape_child(&shape, index);
    }
    return h;
}

typedef struct {
    TermView left;
    TermView right;
} TermViewPair;

typedef struct {
    TermViewPair *slots;
    bool *used;
    size_t cap;
    size_t len;
} TermPairSet;

static uint64_t term_view_hash(TermView view) {
    uint64_t h = (uint64_t)(uintptr_t)view.ptr * 0x9E3779B97F4A7C15ull;
    h ^= ((uint64_t)view.index << 3) ^ (uint64_t)view.kind;
    h ^= (uint64_t)(uintptr_t)view.env * 0x94D049BB133111EBull;
    h ^= h >> 29;
    return h * 0xBF58476D1CE4E5B9ull;
}

static bool term_view_same(TermView a, TermView b) {
    return a.kind == b.kind && a.index == b.index && a.ptr == b.ptr &&
           a.env == b.env;
}

static bool term_pair_set_grow(TermPairSet *set);

/* Add a pair; false when it was there already. `*failed` on out of memory. */
static bool term_pair_set_add(TermPairSet *set, TermViewPair pair,
                              bool *failed) {
    if ((set->len + 1u) * 2u > set->cap && !term_pair_set_grow(set)) {
        *failed = true;
        return false;
    }
    uint64_t h = term_view_hash(pair.left) * 31u ^ term_view_hash(pair.right);
    size_t mask = set->cap - 1u;
    for (size_t slot = (size_t)h & mask;; slot = (slot + 1u) & mask) {
        if (!set->used[slot]) {
            set->used[slot] = true;
            set->slots[slot] = pair;
            set->len++;
            return true;
        }
        if (term_view_same(set->slots[slot].left, pair.left) &&
            term_view_same(set->slots[slot].right, pair.right))
            return false;
    }
}

static bool term_pair_set_grow(TermPairSet *set) {
    size_t next = set->cap ? set->cap * 2u : 64u;
    TermViewPair *slots = cetta_malloc(sizeof(*slots) * next);
    bool *used = cetta_malloc(sizeof(*used) * next);
    if (!slots || !used) {
        free(slots);
        free(used);
        return false;
    }
    memset(used, 0, sizeof(*used) * next);
    TermPairSet grown = {slots, used, next, 0u};
    bool failed = false;
    for (size_t index = 0u; index < set->cap; index++) {
        if (set->used[index])
            (void)term_pair_set_add(&grown, set->slots[index], &failed);
    }
    free(set->slots);
    free(set->used);
    *set = grown;
    return !failed;
}

/*
 * Build a bisimulation from the root pair: each pair met is recorded once,
 * its shapes compared, and its children's pairs followed.  Reaching every
 * pair without a mismatch, the recorded pairs are a bisimulation that holds
 * the roots (RationalTermGraph.checkCertificate_sound); a mismatch reachable
 * from the roots refutes them.  The pairs are finite: a finite atom has
 * finitely many views and a graph finitely many nodes.
 */
bool term_graph_value_eq(Atom *left, Atom *right,
                         bool (*leaf_eq)(Atom *, Atom *)) {
    if (!left || !right || !leaf_eq)
        return false;
    TermPairSet seen = {0};
    size_t stack_len = 0u;
    size_t stack_cap = 64u;
    TermViewPair *stack = cetta_malloc(sizeof(*stack) * stack_cap);
    if (!stack)
        return false;
    stack[stack_len++] = (TermViewPair){term_view_atom(left),
                                        term_view_atom(right)};
    bool equal = true;
    bool failed = false;
    while (equal && stack_len > 0u) {
        TermViewPair pair = stack[--stack_len];
        pair.left = term_view_resolve(pair.left);
        pair.right = term_view_resolve(pair.right);
        /* A view is its own unfolding. */
        if (term_view_same(pair.left, pair.right))
            continue;
        if (!term_pair_set_add(&seen, pair, &failed)) {
            if (failed)
                equal = false;
            continue;
        }
        TermShape a = term_view_shape(pair.left);
        TermShape b = term_view_shape(pair.right);
        if (a.kind != b.kind) {
            equal = false;
            break;
        }
        switch (a.kind) {
        case SHAPE_NIL:
            continue;
        case SHAPE_LEAF:
            equal = a.leaf && b.leaf && leaf_eq(a.leaf, b.leaf);
            continue;
        case SHAPE_COMPOUND:
            if (a.name != b.name || a.arity != b.arity) {
                equal = false;
                continue;
            }
            break;
        case SHAPE_CELL:
            break;
        }
        for (uint32_t index = 0u; index < a.arity; index++) {
            if (stack_len == stack_cap) {
                size_t next = stack_cap * 2u;
                TermViewPair *grown = cetta_realloc(
                    stack, sizeof(*stack) * next);
                if (!grown) {
                    equal = false;
                    break;
                }
                stack = grown;
                stack_cap = next;
            }
            stack[stack_len++] = (TermViewPair){
                term_shape_child(&a, index), term_shape_child(&b, index)};
        }
    }
    free(stack);
    free(seen.slots);
    free(seen.used);
    return equal && !failed;
}

/* ── Assumptions ────────────────────────────────────────────────────────── */

static size_t term_graph_assumption_slot(const void *left, const void *right,
                                         size_t mask) {
    uint64_t h = (uint64_t)(uintptr_t)left * 0x9E3779B97F4A7C15ull;
    h ^= (uint64_t)(uintptr_t)right + 0x632BE59BD9B4E019ull + (h << 6) +
         (h >> 2);
    h ^= h >> 31;
    return (size_t)h & mask;
}

bool term_graph_assume(CettaTermGraphAssumptions *assumptions,
                       const void *left, const void *right, bool *fresh) {
    *fresh = false;
    if ((assumptions->len + 1u) * 2u > assumptions->cap) {
        size_t next = assumptions->cap ? assumptions->cap * 2u : 32u;
        const void **slots = cetta_malloc(sizeof(*slots) * next * 2u);
        if (!slots)
            return false;
        memset(slots, 0, sizeof(*slots) * next * 2u);
        for (size_t index = 0u; index < assumptions->cap; index++) {
            const void *l = assumptions->slots[index * 2u];
            if (!l)
                continue;
            const void *r = assumptions->slots[index * 2u + 1u];
            size_t slot = term_graph_assumption_slot(l, r, next - 1u);
            while (slots[slot * 2u])
                slot = (slot + 1u) & (next - 1u);
            slots[slot * 2u] = l;
            slots[slot * 2u + 1u] = r;
        }
        free(assumptions->slots);
        assumptions->slots = slots;
        assumptions->cap = next;
    }
    size_t mask = assumptions->cap - 1u;
    for (size_t slot = term_graph_assumption_slot(left, right, mask);;
         slot = (slot + 1u) & mask) {
        if (!assumptions->slots[slot * 2u]) {
            assumptions->slots[slot * 2u] = left;
            assumptions->slots[slot * 2u + 1u] = right;
            assumptions->len++;
            *fresh = true;
            return true;
        }
        if (assumptions->slots[slot * 2u] == left &&
            assumptions->slots[slot * 2u + 1u] == right)
            return true;
    }
}

static uint64_t term_graph_carrier_hash(const Atom *carrier,
                                        uint64_t context) {
    uint64_t h = context * 0x9E3779B97F4A7C15ull;
    for (CettaExprIndex index = 1u; index < carrier->expr.len; index++) {
        const void *part = index == 1u ? carrier->expr.elems[1]->ground.ptr
                                       : (const void *)carrier->expr.elems[index];
        h ^= (uint64_t)(uintptr_t)part + 0x632BE59BD9B4E019ull + (h << 6) +
             (h >> 2);
    }
    return h ^ (h >> 31);
}

static bool term_graph_carrier_same(const Atom *a, uint64_t a_context,
                                    const Atom *b, uint64_t b_context) {
    if (a_context != b_context || a->expr.len != b->expr.len ||
        a->expr.elems[1]->ground.ptr != b->expr.elems[1]->ground.ptr)
        return false;
    for (CettaExprIndex index = 2u; index < a->expr.len; index++) {
        if (a->expr.elems[index] != b->expr.elems[index])
            return false;
    }
    return true;
}

const void *term_graph_assumption_key(CettaTermGraphAssumptions *assumptions,
                                      const Atom *value, uint64_t context) {
    if (atom_is_rational_node(value))
        return value->ground.ptr;
    if ((assumptions->carrier_len + 1u) * 2u > assumptions->carrier_cap) {
        size_t next = assumptions->carrier_cap
            ? assumptions->carrier_cap * 2u : 32u;
        const Atom **carriers = cetta_malloc(sizeof(*carriers) * next);
        uint64_t *contexts = cetta_malloc(sizeof(*contexts) * next);
        if (!carriers || !contexts) {
            free(carriers);
            free(contexts);
            return NULL;
        }
        memset(carriers, 0, sizeof(*carriers) * next);
        for (size_t index = 0u; index < assumptions->carrier_cap; index++) {
            const Atom *carrier = assumptions->carriers[index];
            if (!carrier)
                continue;
            uint64_t c = assumptions->carrier_contexts[index];
            size_t slot = (size_t)term_graph_carrier_hash(carrier, c) &
                          (next - 1u);
            while (carriers[slot])
                slot = (slot + 1u) & (next - 1u);
            carriers[slot] = carrier;
            contexts[slot] = c;
        }
        free(assumptions->carriers);
        free(assumptions->carrier_contexts);
        assumptions->carriers = carriers;
        assumptions->carrier_contexts = contexts;
        assumptions->carrier_cap = next;
    }
    size_t mask = assumptions->carrier_cap - 1u;
    for (size_t slot = (size_t)term_graph_carrier_hash(value, context) & mask;;
         slot = (slot + 1u) & mask) {
        const Atom *carrier = assumptions->carriers[slot];
        if (!carrier) {
            assumptions->carriers[slot] = value;
            assumptions->carrier_contexts[slot] = context;
            assumptions->carrier_len++;
            return value;
        }
        if (term_graph_carrier_same(carrier,
                                    assumptions->carrier_contexts[slot],
                                    value, context))
            return carrier;
    }
}

void term_graph_assumptions_free(CettaTermGraphAssumptions *assumptions) {
    free(assumptions->slots);
    free(assumptions->carriers);
    free(assumptions->carrier_contexts);
    *assumptions = (CettaTermGraphAssumptions){0};
}

/* ── Printing ───────────────────────────────────────────────────────────── */

static void term_graph_print_node(CettaTermGraph *graph, uint32_t node,
                                  FILE *out, bool petta, bool *on_path);

static void term_graph_print_child(CettaTermGraph *graph, uint32_t node,
                                   FILE *out, bool petta, bool *on_path) {
    if (on_path[node]) {
        fputs("...", out);
        return;
    }
    term_graph_print_node(graph, node, out, petta, on_path);
}

static void term_graph_print_node(CettaTermGraph *graph, uint32_t node,
                                  FILE *out, bool petta, bool *on_path) {
    TermGraphNode *record = &graph->nodes[node];
    switch (record->kind) {
    case CETTA_TERM_GRAPH_LEAF:
        if (petta)
            atom_print_petta(record->leaf, out);
        else
            atom_print(record->leaf, out);
        return;
    case CETTA_TERM_GRAPH_PARAM:
        fprintf(out, "$_%u", record->param);
        return;
    case CETTA_TERM_GRAPH_NIL:
        fputs("()", out);
        return;
    case CETTA_TERM_GRAPH_COMPOUND:
        on_path[node] = true;
        fputc('(', out);
        fputs(symbol_bytes(g_symbols, record->name), out);
        for (uint32_t index = 0u; index < record->arity; index++) {
            fputc(' ', out);
            term_graph_print_child(
                graph, graph->children[record->first_child + index],
                out, petta, on_path);
        }
        fputc(')', out);
        on_path[node] = false;
        return;
    case CETTA_TERM_GRAPH_CELL: {
        /* A spine is its elements; a spine that meets a cell of its own
         * path, or ends in something other than (), shows the rest after a
         * bar. */
        fputc('(', out);
        uint32_t cursor = node;
        uint32_t marked = 0u;
        bool first = true;
        while (graph->nodes[cursor].kind == CETTA_TERM_GRAPH_CELL &&
               !on_path[cursor]) {
            on_path[cursor] = true;
            marked++;
            TermGraphNode *cell = &graph->nodes[cursor];
            if (!first)
                fputc(' ', out);
            first = false;
            term_graph_print_child(graph, graph->children[cell->first_child],
                                   out, petta, on_path);
            cursor = graph->children[cell->first_child + 1u];
        }
        if (graph->nodes[cursor].kind != CETTA_TERM_GRAPH_NIL ||
            on_path[cursor]) {
            fputs(" | ", out);
            term_graph_print_child(graph, cursor, out, petta, on_path);
        }
        fputc(')', out);
        cursor = node;
        for (uint32_t index = 0u; index < marked; index++) {
            on_path[cursor] = false;
            cursor = graph->children[graph->nodes[cursor].first_child + 1u];
        }
        return;
    }
    }
}

void term_graph_print(const CettaTermGraphRef *ref, FILE *out, bool petta) {
    if (!ref || !ref->graph || !out)
        return;
    CettaTermGraph *graph = ref->graph;
    bool *on_path = cetta_malloc(
        sizeof(*on_path) * (size_t)(graph->node_len ? graph->node_len : 1u));
    if (!on_path) {
        fputs("...", out);
        return;
    }
    memset(on_path, 0, sizeof(*on_path) * (size_t)graph->node_len);
    term_graph_print_node(graph, ref->node, out, petta, on_path);
    free(on_path);
}

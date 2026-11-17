#include "native_match_view.h"
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

typedef struct {
    NmHostValue key;
    size_t value;
    unsigned state;
} Entry;
typedef struct { Entry *entry; size_t capacity, used; } Map;
typedef struct {
    NmVariableKey key;
    NmHostValue witness;
    uint64_t binding;
    size_t original;
} Variable;
typedef struct {
    NmHostValue value;
    NmViewNode node;
    size_t edge, next, output_edge;
    bool entered;
} Frame;

struct NmPreparedMatch {
    NmTermView source;
    NmUnifyInput input;
    NmNode *nodes;
    NmHostValue *origins;
    uint64_t *edges, *initial;
    NmVariableKey *keys;
    Variable *variables;
    size_t node_count, node_capacity, origin_capacity, source_node_count;
    size_t edge_count, edge_capacity, variable_count, variable_capacity;
};
typedef struct {
    NmPreparedMatch *graph;
    const NmTermView *view;
    NmImportLimits limits;
    NmImportStats stats;
    Map values, variables;
    Frame *frames;
    size_t frame_count, frame_capacity;
    NmViewStatus status;
} Builder;

static bool grow(void **pointer, size_t *capacity, size_t needed, size_t width) {
    if (needed<=*capacity) return true;
    size_t next=*capacity ? *capacity : 16;
    while (next<needed) {
        if (next>SIZE_MAX/2) { next=needed;break; }
        next*=2;
    }
    if (next>SIZE_MAX/width) return false;
    void *value=realloc(*pointer,next*width);
    if (!value) return false;
    *pointer=value;*capacity=next;return true;
}
static uint64_t hash(NmHostValue value) {
    uint64_t h=(value.syntax*UINT64_C(0x9E3779B97F4A7C15))^
        (value.context*UINT64_C(0xC2B2AE3D27D4EB4F));
    h^=h>>31;h*=UINT64_C(0x165667B19E3779F9);return h^(h>>32);
}
static bool key_equal(NmHostValue a, NmHostValue b) {
    return a.syntax==b.syntax && a.context==b.context;
}
static Entry *find(Map *map, NmHostValue key) {
    if (!map->capacity) return NULL;
    size_t mask=map->capacity-1;
    for (size_t p=(size_t)hash(key)&mask;;p=(p+1)&mask) {
        Entry *e=map->entry+p;
        if (!e->state) return NULL;
        if (key_equal(e->key,key)) return e;
    }
}
static Entry *insert(Map *map, NmHostValue key, unsigned state, size_t value) {
    if (map->used>=map->capacity/2) {
        size_t next=map->capacity ? map->capacity*2 : 16;
        if (next<map->capacity || next>SIZE_MAX/sizeof(Entry)) return NULL;
        Entry *entries=calloc(next,sizeof *entries);
        if (!entries) return NULL;
        for (size_t i=0;i<map->capacity;++i) if (map->entry[i].state) {
            size_t p=(size_t)hash(map->entry[i].key)&(next-1);
            while (entries[p].state) p=(p+1)&(next-1);
            entries[p]=map->entry[i];
        }
        free(map->entry);map->entry=entries;map->capacity=next;
    }
    size_t p=(size_t)hash(key)&(map->capacity-1);
    while (map->entry[p].state) p=(p+1)&(map->capacity-1);
    map->entry[p]=(Entry){key,value,state};++map->used;return map->entry+p;
}
static bool resource(Builder *b) { b->status=NM_VIEW_RESOURCE;return false; }
/* Pass a real void-pointer object to grow; a cast from T ** would violate
 * C's aliasing rules even when all data-pointer representations agree. */
#define RESERVE(builder, pointer, capacity, needed) do { \
    void *replacement_=(pointer); \
    if (!grow(&replacement_,&(capacity),(needed),sizeof *(pointer))) return resource(builder); \
    (pointer)=replacement_; \
} while (0)
static bool tick(Builder *b) {
    if (b->stats.callbacks==b->limits.callbacks) return resource(b);
    ++b->stats.callbacks;return true;
}
static bool read_status(Builder *b, NmViewStatus status) {
    if (status==NM_VIEW_READY) return true;
    b->status=status==NM_VIEW_DEFER || status==NM_VIEW_RESOURCE || status==NM_VIEW_INTERRUPTED ?
        status : NM_VIEW_FAULT;
    return false;
}
static bool append_node(Builder *b, NmNode node, NmHostValue origin, uint64_t *id) {
    NmPreparedMatch *g=b->graph;
    if (g->node_count>=b->limits.nodes || g->node_count>=UINT32_MAX) return resource(b);
    RESERVE(b,g->nodes,g->node_capacity,g->node_count+1);
    RESERVE(b,g->origins,g->origin_capacity,g->node_count+1);
    *id=g->node_count;g->nodes[g->node_count]=node;g->origins[g->node_count]=origin;
    ++g->node_count;return true;
}
static bool variable_slot(Builder *b, NmVariableKey key, NmHostValue witness, uint64_t *slot) {
    Entry *entry=find(&b->variables,(NmHostValue){key.variable,key.scope});
    if (entry) { *slot=entry->value;return true; }
    NmPreparedMatch *g=b->graph;
    if (g->variable_count>=b->limits.variables || g->variable_count>=UINT32_MAX-1u) return resource(b);
    RESERVE(b,g->variables,g->variable_capacity,g->variable_count+1);
    *slot=g->variable_count;
    g->variables[g->variable_count]=(Variable){key,witness,0,g->variable_count};
    ++g->variable_count;
    if (!insert(&b->variables,(NmHostValue){key.variable,key.scope},2,(size_t)*slot)) return resource(b);
    return true;
}
static bool push(Builder *b, NmHostValue value, size_t output_edge) {
    if (!b->limits.nodes || b->values.used>=b->limits.nodes-1 || b->values.used>=UINT32_MAX-1u)
        return resource(b);
    RESERVE(b,b->frames,b->frame_capacity,b->frame_count+1);
    if (!insert(&b->values,value,1,0)) return resource(b);
    b->frames[b->frame_count++]=(Frame){.value=value,.output_edge=output_edge};return true;
}
static bool finish(Builder *b, uint64_t id, uint64_t *root) {
    Frame f=b->frames[--b->frame_count];
    /* Descendant insertion may have moved the map; resolve its entry anew. */
    Entry *entry=find(&b->values,f.value);
    if (!entry || entry->state!=1) { b->status=NM_VIEW_FAULT;return false; }
    entry->state=2;entry->value=(size_t)id;
    if (f.output_edge==SIZE_MAX) *root=id;
    else b->graph->edges[f.output_edge]=id;
    return true;
}
static bool import_value(Builder *b, NmHostValue value, uint64_t *root) {
    Entry *existing=find(&b->values,value);
    if (existing) {
        if (existing->state!=2) { b->status=NM_VIEW_DEFER;return false; }
        *root=existing->value;return true;
    }
    if (!push(b,value,SIZE_MAX)) return false;
    while (b->frame_count) {
        size_t top=b->frame_count-1;
        if (!b->frames[top].entered) {
            NmViewNode node={0};
            if (!tick(b)) return false;
            ++b->stats.inspections;
            if (!read_status(b,b->view->inspect(b->view->context,b->frames[top].value,&node))) return false;
            if (node.kind==NM_NODE_CONSTANT || node.kind==NM_NODE_VARIABLE) {
                NmNode native={.kind=node.kind};uint64_t id;
                if (node.kind==NM_NODE_CONSTANT) native.a=node.constant;
                else {
                    if (!variable_slot(b,node.variable,b->frames[top].value,&native.a)) return false;
                    native.b=node.variable.variable;native.c=node.variable.scope;
                }
                if (!append_node(b,native,b->frames[top].value,&id) || !finish(b,id,root)) return false;
                continue;
            }
            if (node.kind!=NM_NODE_TUPLE) { b->status=NM_VIEW_DEFER;return false; }
            if (node.children && !b->view->child) { b->status=NM_VIEW_FAULT;return false; }
            NmPreparedMatch *g=b->graph;
            if (node.children>b->limits.edges-g->edge_count) return resource(b);
            size_t needed=g->edge_count+(size_t)node.children;
            RESERVE(b,g->edges,g->edge_capacity,needed);
            b->frames[top].node=node;b->frames[top].entered=true;b->frames[top].edge=g->edge_count;
            g->edge_count=needed;
        }
        Frame frame=b->frames[top];
        if (frame.next<frame.node.children) {
            NmHostValue child;
            if (!tick(b)) return false;
            ++b->stats.children;
            if (!read_status(b,b->view->child(b->view->context,frame.value,frame.next,&child))) return false;
            size_t edge=frame.edge+frame.next;
            ++b->frames[top].next;
            Entry *entry=find(&b->values,child);
            if (entry) {
                if (entry->state!=2) { b->status=NM_VIEW_DEFER;return false; }
                b->graph->edges[edge]=entry->value;
            } else if (!push(b,child,edge)) return false;
        } else {
            NmNode node={NM_NODE_TUPLE,frame.edge,frame.node.children,0};uint64_t id;
            if (!append_node(b,node,frame.value,&id) || !finish(b,id,root)) return false;
        }
    }
    return true;
}

static int variable_compare(const void *a, const void *b) {
    const Variable *x=a,*y=b;
    if (x->key.scope!=y->key.scope) return x->key.scope<y->key.scope ? -1 : 1;
    return x->key.variable<y->key.variable ? -1 : x->key.variable>y->key.variable;
}

static bool import_constraints(Builder *b, const NmHostConstraint *constraints, size_t count) {
    NmPreparedMatch *g=b->graph;
    if (count>b->limits.edges/2) return resource(b);
    RESERVE(b,g->edges,g->edge_capacity,2*count);
    g->edge_count=2*count;
    for (size_t i=0;i<count;++i) {
        uint64_t l,r;
        if (!import_value(b,constraints[i].left,&l) || !import_value(b,constraints[i].right,&r))
            return false;
        /* Import may move the edge vector; retain coordinates, not pointers. */
        g->edges[i]=l;g->edges[count+i]=r;
    }
    return true;
}

void nm_match_prepared_free(NmPreparedMatch *g) {
    if (!g) return;
    free(g->nodes);free(g->origins);free(g->edges);free(g->initial);free(g->keys);free(g->variables);free(g);
}

static NmViewStatus prepare_match(const NmTermView *view, const NmHostConstraint *constraints,
                                  size_t count, bool conjunction, NmImportLimits limits,
                                  NmPreparedMatch **out, NmImportStats *stats) {
    if (out) *out=NULL;
    if (stats) memset(stats,0,sizeof *stats);
    if (!out || !view || !view->inspect || (count && !constraints)) return NM_VIEW_FAULT;
    Builder b={.view=view,.limits=limits,.status=NM_VIEW_READY};
    b.graph=calloc(1,sizeof *b.graph);
    if (!b.graph) return NM_VIEW_RESOURCE;
    uint64_t unused,l=0,r=0;
    bool ok=append_node(&b,(NmNode){0},(NmHostValue){0},&unused);
    if (ok) ok=conjunction ? import_constraints(&b,constraints,count) :
        import_value(&b,constraints[0].left,&l) && import_value(&b,constraints[0].right,&r);
    for (size_t i=0;ok && i<b.graph->variable_count;++i) if (view->binding) {
        bool bound=false;NmHostValue value;
        if (!tick(&b)) { ok=false;break; }
        ++b.stats.bindings;
        if (!read_status(&b,view->binding(view->context,b.graph->variables[i].witness,&bound,&value))) {
            ok=false;break;
        }
        if (bound) {
            uint64_t node;
            ok=import_value(&b,value,&node);
            /* Import may grow the variable vector, so retain only its index. */
            if (ok) b.graph->variables[i].binding=node;
        }
    }
    NmPreparedMatch *g=b.graph;
    g->source_node_count=g->node_count;
    if (ok && conjunction)
        ok=append_node(&b,(NmNode){NM_NODE_TUPLE,0,count,0},(NmHostValue){0},&l) &&
           append_node(&b,(NmNode){NM_NODE_TUPLE,count,count,0},(NmHostValue){0},&r);
    size_t *renumber=NULL;
    if (ok && g->variable_count) {
        if (g->variable_count>SIZE_MAX/sizeof *g->keys ||
            g->variable_count>SIZE_MAX/sizeof *g->initial ||
            g->variable_count>SIZE_MAX/sizeof *renumber) ok=resource(&b);
        else {
            g->keys=malloc(g->variable_count*sizeof *g->keys);
            g->initial=malloc(g->variable_count*sizeof *g->initial);
            renumber=malloc(g->variable_count*sizeof *renumber);
            if (!g->keys || !g->initial || !renumber) ok=resource(&b);
        }
        if (ok) {
            if (g->variable_count>1) qsort(g->variables,g->variable_count,sizeof *g->variables,variable_compare);
            for (size_t i=0;i<g->variable_count;++i) {
                renumber[g->variables[i].original]=i;
                g->keys[i]=g->variables[i].key;g->initial[i]=g->variables[i].binding;
            }
            for (size_t i=1;i<g->node_count;++i)
                if (g->nodes[i].kind==NM_NODE_VARIABLE) g->nodes[i].a=renumber[g->nodes[i].a];
        }
    }
    free(renumber);free(b.values.entry);free(b.variables.entry);free(b.frames);
    if (stats) *stats=b.stats;
    if (!ok) { nm_match_prepared_free(g);return b.status; }
    g->source=*view;
    g->input=(NmUnifyInput){g->nodes,g->node_count,g->edges,g->edge_count,
        g->initial,g->variable_count,g->keys,g->variable_count,l,r};
    *out=g;return NM_VIEW_READY;
}

NmViewStatus nm_match_prepare(const NmTermView *view, NmHostValue left, NmHostValue right,
                              NmImportLimits limits, NmPreparedMatch **out, NmImportStats *stats) {
    NmHostConstraint constraint={left,right};
    return prepare_match(view,&constraint,1,false,limits,out,stats);
}
NmViewStatus nm_match_prepare_constraints(const NmTermView *view,
                                          const NmHostConstraint *constraints, size_t count,
                                          NmImportLimits limits, NmPreparedMatch **out,
                                          NmImportStats *stats) {
    return prepare_match(view,constraints,count,true,limits,out,stats);
}

const NmUnifyInput *nm_match_input(const NmPreparedMatch *g) { return g ? &g->input : NULL; }
bool nm_match_origin(const NmPreparedMatch *g,uint64_t node,NmHostValue *origin) {
    if (!g || !origin || !node || node>=g->source_node_count) return false;
    *origin=g->origins[node];return true;
}

bool nm_match_project(const NmPreparedMatch *g, const NmUnifyResult *result,
                       const NmUnifyWork *work, size_t slot, NmBindingProjection *projection) {
    if (!g || !result || !work || !projection || result->outcome!=NM_UNIFIED ||
        slot>=g->variable_count || work->bindings_words<g->variable_count || !work->bindings) return false;
    uint64_t node=work->bindings[slot];
    if (node>=g->source_node_count || (g->initial[slot] && node!=g->initial[slot])) return false;
    *projection=(NmBindingProjection){g->variables[slot].witness,node ? g->origins[node] : (NmHostValue){0},
        g->keys[slot],node!=0,!g->initial[slot] && node!=0};
    return true;
}

typedef struct {
    NmVariableKey key;
    NmHostValue value;
    bool bound,changed;
} SolutionEntry;
struct NmSolution {
    atomic_size_t references;
    NmTermView source;
    NmSolution *parent;
    size_t count, depth,changes;
    SolutionEntry entries[];
};
static NmViewStatus solution_inspect(void *context, NmHostValue value, NmViewNode *node) {
    const NmSolution *s=context;
    return s->source.inspect(s->source.context,value,node);
}
static NmViewStatus solution_child(void *context, NmHostValue value, uint64_t index, NmHostValue *child) {
    const NmSolution *s=context;
    return s->source.child ? s->source.child(s->source.context,value,index,child) : NM_VIEW_FAULT;
}
static int compare_key(NmVariableKey a, NmVariableKey b) {
    if (a.scope!=b.scope) return a.scope<b.scope ? -1 : 1;
    return a.variable<b.variable ? -1 : a.variable>b.variable;
}
static NmViewStatus solution_binding(void *context, NmHostValue variable, bool *bound, NmHostValue *value) {
    const NmSolution *s=context;NmViewNode node;
    NmViewStatus status=s->source.inspect(s->source.context,variable,&node);
    if (status!=NM_VIEW_READY) return status;
    if (node.kind!=NM_NODE_VARIABLE) return NM_VIEW_FAULT;
    /* Keep lookup iterative even when many private refinements are retained. */
    for (const NmSolution *at=s;at;at=at->parent) {
        size_t low=0,high=at->count;
        while (low<high) {
            size_t middle=low+(high-low)/2;
            int order=compare_key(at->entries[middle].key,node.variable);
            if (!order) {
                *bound=at->entries[middle].bound;*value=at->entries[middle].value;return NM_VIEW_READY;
            }
            if (order<0) low=middle+1;else high=middle;
        }
    }
    if (s->source.binding) return s->source.binding(s->source.context,variable,bound,value);
    *bound=false;*value=(NmHostValue){0};return NM_VIEW_READY;
}
NmTermView nm_solution_view(const NmSolution *s) {
    return s ? (NmTermView){(void *)(uintptr_t)s,solution_inspect,solution_child,solution_binding} :
        (NmTermView){0};
}
bool nm_solution_retain(NmSolution *s) {
    if (!s) return false;
    size_t n=atomic_load_explicit(&s->references,memory_order_relaxed);
    do {
        if (n==SIZE_MAX) return false;
    } while (!atomic_compare_exchange_weak_explicit(&s->references,&n,n+1,
                 memory_order_relaxed,memory_order_relaxed));
    return true;
}
void nm_solution_free(NmSolution *s) {
    while (s && atomic_fetch_sub_explicit(&s->references,1,memory_order_acq_rel)==1) {
        NmSolution *parent=s->parent;free(s);s=parent;
    }
}
NmViewStatus nm_solution_capture(const NmPreparedMatch *g, const NmUnifyResult *result,
                                 const NmUnifyWork *work, NmSolution **out) {
    if (out) *out=NULL;
    if (!out || !g || !g->source.inspect || !result || result->outcome!=NM_UNIFIED || !work)
        return NM_VIEW_FAULT;
    size_t count=g->variable_count;
    if (count>(SIZE_MAX-sizeof(NmSolution))/sizeof(SolutionEntry)) return NM_VIEW_RESOURCE;
    NmSolution *s=malloc(sizeof *s+count*sizeof *s->entries);
    if (!s) return NM_VIEW_RESOURCE;
    atomic_init(&s->references,1);s->source=g->source;s->count=count;s->depth=1;s->changes=0;s->parent=NULL;
    for (size_t i=0;i<count;++i) {
        NmBindingProjection p;
        if (!nm_match_project(g,result,work,i,&p)) { free(s);return NM_VIEW_FAULT; }
        s->entries[i]=(SolutionEntry){p.key,p.value,p.bound,p.changed};
        if (p.changed) ++s->changes;
    }
    if (s->changes!=result->payload) { free(s);return NM_VIEW_FAULT; }
    if (g->source.inspect==solution_inspect && g->source.child==solution_child &&
        g->source.binding==solution_binding && g->source.context) {
        NmSolution *parent=g->source.context;
        if (parent->depth==SIZE_MAX || parent->changes>SIZE_MAX-s->changes || !nm_solution_retain(parent)) {
            free(s);return NM_VIEW_RESOURCE;
        }
        s->parent=parent;s->source=parent->source;s->depth=parent->depth+1;
        s->changes+=parent->changes;
    }
    *out=s;return NM_VIEW_READY;
}
size_t nm_solution_entries(const NmSolution *s) { return s ? s->count : 0; }
size_t nm_solution_depth(const NmSolution *s) { return s ? s->depth : 0; }
size_t nm_solution_changes(const NmSolution *s) { return s ? s->changes : 0; }
bool nm_solution_visit_changes(const NmSolution *s,
                                bool (*visit)(void *context,NmVariableKey key,NmHostValue value),
                                void *context) {
    if (!s || !visit) return false;
    for (const NmSolution *at=s;at;at=at->parent)
        for (size_t i=0;i<at->count;++i) if (at->entries[i].changed &&
            !visit(context,at->entries[i].key,at->entries[i].value)) return false;
    return true;
}

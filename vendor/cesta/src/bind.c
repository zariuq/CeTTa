/* C name binding over the AST (N1570 §6.2.1–6.2.3).

   Ordinary identifiers follow the profile's scope program: the same lowered
   hook data that classifies typedef names during parsing is executed here over
   the tree, so a tree from any presentation gets the parser's scopes. Tags
   share those scopes (§6.2.1p4), labels have function scope, and members are
   resolved per record during elaboration. Every identifier occurrence that
   denotes an entity is recorded with the declaration visible at that point. */

#include "sema.h"

#include <stdlib.h>
#include <string.h>

static void *checked(void *p) {
    if (!p) { fputs("cesta: out of memory\n",stderr); exit(2); }
    return p;
}

/* ---- pointer and text maps ------------------------------------------------ */

static size_t pointer_hash(const void *key) {
    uint64_t x = (uint64_t)(uintptr_t)key;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
    return (size_t)x;
}
void *ce_pointer_get(const CePointerMap *map, const void *key) {
    if (!map->capacity) return NULL;
    for (size_t i = pointer_hash(key) & (map->capacity-1);; i = (i+1) & (map->capacity-1)) {
        if (!map->keys[i]) return NULL;
        if (map->keys[i] == key) return map->values[i];
    }
}
void ce_pointer_put(CePointerMap *map, const void *key, void *value) {
    if ((map->len+1)*2 > map->capacity) {
        CePointerMap grown = {0};
        grown.capacity = map->capacity ? map->capacity*2 : 64;
        grown.keys = checked(calloc(grown.capacity,sizeof(*grown.keys)));
        grown.values = checked(calloc(grown.capacity,sizeof(*grown.values)));
        for (size_t i = 0; i < map->capacity; ++i)
            if (map->keys[i]) ce_pointer_put(&grown,map->keys[i],map->values[i]);
        free(map->keys); free(map->values); *map = grown;
    }
    size_t i = pointer_hash(key) & (map->capacity-1);
    while (map->keys[i] && map->keys[i] != key) i = (i+1) & (map->capacity-1);
    if (!map->keys[i]) { map->keys[i] = key; ++map->len; }
    map->values[i] = value;
}
void ce_pointer_map_free(CePointerMap *map) {
    free(map->keys); free(map->values); *map = (CePointerMap){0};
}

/* A text-keyed table of (space, name) slots; slots are never removed. */
typedef struct { CeText key; unsigned space; void *value; bool used; } TextSlot;
typedef struct { TextSlot *slots; size_t len, capacity; } TextMap;
static size_t text_hash(CeText key, unsigned space) {
    uint64_t h = 1469598103934665603ULL ^ space;
    for (size_t i = 0; i < key.len; ++i) { h ^= key.data[i]; h *= 1099511628211ULL; }
    return (size_t)h;
}
static TextSlot *text_slot(TextMap *map, CeText key, unsigned space) {
    if ((map->len+1)*2 > map->capacity) {
        TextMap grown = {checked(calloc(map->capacity ? map->capacity*2 : 256,sizeof(TextSlot))),0,
                         map->capacity ? map->capacity*2 : 256};
        for (size_t i = 0; i < map->capacity; ++i) if (map->slots[i].used) {
            TextSlot *s = text_slot(&grown,map->slots[i].key,map->slots[i].space);
            s->value = map->slots[i].value;
        }
        free(map->slots); *map = grown;
    }
    size_t mask = map->capacity-1;
    for (size_t i = text_hash(key,space) & mask;; i = (i+1) & mask) {
        TextSlot *s = &map->slots[i];
        if (!s->used) { *s = (TextSlot){key,space,NULL,true}; ++map->len; return s; }
        if (s->space == space && ce_equal(s->key,key)) return s;
    }
}

/* ---- shared helpers ------------------------------------------------------- */

unsigned ce_sema_kind(const CeSema *sema, const CeTree *tree) {
    return tree && tree->production >= 0 ? sema->kinds[tree->production] : CE_K_NONE;
}
CeText ce_sema_name(CeSema *sema, const CeTree *token) {
    return ce_name_key(&sema->arena,token->spelling);
}
CeEntity *ce_sema_entity(CeSema *sema, unsigned kind, unsigned space, CeText name,
                         const CeTree *declaration) {
    CeEntity *entity = ce_alloc(&sema->arena,sizeof(*entity));
    *entity = (CeEntity){.kind=kind,.space=space,.name=name,.declaration=declaration,
                         .id=sema->nentities+1};
    if (sema->nentities == sema->entity_capacity) {
        sema->entity_capacity = sema->entity_capacity ? sema->entity_capacity*2 : 256;
        sema->entities = ce_resize(sema->entities,sema->entity_capacity,sizeof(*sema->entities));
    }
    sema->entities[sema->nentities++] = entity;
    if (declaration) ce_pointer_put(&sema->entity_of,declaration,entity);
    return entity;
}
const CeUse *ce_sema_use(const CeSema *sema, const CeTree *node) {
    size_t index = (size_t)(uintptr_t)ce_pointer_get(&sema->use_of,node);
    return index ? &sema->uses[index-1] : NULL;
}
static unsigned folded_depth;
static void record_use(CeSema *sema, const CeTree *node, CeEntity *entity,
                       const CeTree *visible, unsigned role) {
    if (sema->nuses == sema->use_capacity) {
        sema->use_capacity = sema->use_capacity ? sema->use_capacity*2 : 1024;
        sema->uses = ce_resize(sema->uses,sema->use_capacity,sizeof(*sema->uses));
    }
    sema->uses[sema->nuses++] = (CeUse){node,entity,visible,role,folded_depth > 0};
    ce_pointer_put(&sema->use_of,node,(void *)(uintptr_t)sema->nuses);
}

/* Declarator derivations (§6.7.6): grouping and attribute wrappers are not. */
static bool derivation(unsigned k) {
    return k == CE_K_PointerDeclarator || k == CE_K_ArrayDeclarator ||
        k == CE_K_StaticArrayDeclarator || k == CE_K_QualifiedStaticArrayDeclarator ||
        k == CE_K_VariableArrayDeclarator || k == CE_K_FunctionDeclarator ||
        k == CE_K_OldFunctionDeclarator;
}
/* The child of a declarator node on the path to its identifier. */
static const CeTree *toward_name(const CeSema *sema, const CeTree *t) {
    switch (ce_sema_kind(sema,t)) {
    case CE_K_PointerDeclarator: return t->children[1];
    case CE_K_Declarator: case CE_K_SomeDeclarator: case CE_K_ParenthesizedDeclarator:
    case CE_K_ArrayDeclarator: case CE_K_StaticArrayDeclarator:
    case CE_K_QualifiedStaticArrayDeclarator: case CE_K_VariableArrayDeclarator:
    case CE_K_FunctionDeclarator: case CE_K_OldFunctionDeclarator:
    case CE_K_GNUAttributedDeclarator: case CE_K_GNUAsmDeclarator: return t->children[0];
    default: return NULL;
    }
}
const CeTree *ce_declarator_name(const CeSema *sema, const CeTree *t) {
    for (size_t depth = 0; t && depth < 1000000; ++depth) {
        if (ce_sema_kind(sema,t) == CE_K_NameDeclarator) return t->children[0];
        t = toward_name(sema,t);
    }
    return NULL;
}
/* The derivation nearest the identifier is the outermost constructor of its
   type: `*f(int)` declares a function, `(*f)(int)` a pointer. */
static const CeTree *nearest_derivation(const CeSema *sema, const CeTree *t) {
    const CeTree *nearest = NULL;
    for (size_t depth = 0; t && depth < 1000000; ++depth) {
        unsigned k = ce_sema_kind(sema,t);
        if (k == CE_K_NameDeclarator) return nearest;
        if (derivation(k)) nearest = t;
        t = toward_name(sema,t);
    }
    return nearest;
}
static bool declares_function(const CeSema *sema, const CeTree *declarator) {
    unsigned k = ce_sema_kind(sema,nearest_derivation(sema,declarator));
    return k == CE_K_FunctionDeclarator || k == CE_K_OldFunctionDeclarator;
}

/* ---- the binder ----------------------------------------------------------- */

typedef struct Bound {
    struct Chain *chain;
    bool is_typedef;
    CeEntity *entity;
    const CeTree *declaration;
    struct Bound *shadowed;
} Bound;
/* Every (space, name) has one stable chain whose top is the innermost binding. */
typedef struct Chain { CeText key; unsigned space; Bound *top; } Chain;
enum { MARK_BLOCK = 1, MARK_PROTOTYPE, MARK_FUNCTION };
typedef struct { size_t height; unsigned kind; } Mark;
typedef struct { Bound **items; size_t len; } Saved;
typedef struct Context {
    bool is_typedef, deferred, external, internal, old_parameters;
    bool function_base;
    Saved pending;
    struct Context *previous;
} Context;
typedef struct { const CeTree *node; Saved parameters; } Recorded;

typedef struct {
    CeSema *sema;
    const CeGrammar *g;
    CeError *error;
    TextMap names;                /* (space, key) -> innermost Bound */
    Bound **stack;
    size_t height, stack_capacity;
    Mark *marks;
    size_t nmarks, mark_capacity;
    Context *contexts;
    Saved pending;
    Recorded *functions;
    size_t nfunctions, function_capacity;
    TextMap linkage;              /* internal/external entities by name */
    TextMap builtins;
    TextMap labels;
    unsigned **slot_actions;      /* production -> (child slot << 8 | action), 0-terminated */
    unsigned **after_actions;     /* production -> actions after the node, 0-terminated */
    int **child_symbols;          /* production -> RHS symbol of each tree child */
    int *transparent_hooked;      /* symbol -> hooked transparent production, or -1 */
    unsigned attribute_depth;
    bool old_parameter_declarations;
} Binder;

static Chain *chain(Binder *b, CeText key, unsigned space) {
    TextSlot *slot = text_slot(&b->names,key,space);
    if (!slot->value) {
        Chain *c = ce_alloc(&b->sema->arena,sizeof(*c));
        *c = (Chain){key,space,NULL}; slot->value = c;
    }
    return slot->value;
}
static Bound *lookup(Binder *b, CeText key, unsigned space) {
    return chain(b,key,space)->top;
}
static Bound *bind(Binder *b, CeText key, unsigned space, CeEntity *entity,
                   const CeTree *declaration, bool is_typedef) {
    Chain *c = chain(b,key,space);
    Bound *bound = ce_alloc(&b->sema->arena,sizeof(*bound));
    *bound = (Bound){c,is_typedef,entity,declaration,c->top};
    c->top = bound;
    if (b->height == b->stack_capacity) {
        b->stack_capacity = b->stack_capacity ? b->stack_capacity*2 : 1024;
        b->stack = ce_resize(b->stack,b->stack_capacity,sizeof(*b->stack));
    }
    b->stack[b->height++] = bound;
    return bound;
}
static void unbind_to(Binder *b, size_t height) {
    while (b->height > height) {
        Bound *bound = b->stack[--b->height];
        bound->chain->top = bound->shadowed;
    }
}
static void push_mark(Binder *b, unsigned kind) {
    if (b->nmarks == b->mark_capacity) {
        b->mark_capacity = b->mark_capacity ? b->mark_capacity*2 : 64;
        b->marks = ce_resize(b->marks,b->mark_capacity,sizeof(*b->marks));
    }
    b->marks[b->nmarks++] = (Mark){b->height,kind};
}
static bool pop_mark(Binder *b, unsigned kind, bool restore) {
    if (!b->nmarks || b->marks[b->nmarks-1].kind != kind)
        return ce_error(b->error,0,"binding scope program has unbalanced context hooks");
    Mark mark = b->marks[--b->nmarks];
    if (restore) unbind_to(b,mark.height);
    return true;
}
static unsigned current_scope(const Binder *b) {
    if (b->nmarks && b->marks[b->nmarks-1].kind == MARK_PROTOTYPE) return CE_SCOPE_PROTOTYPE;
    return b->nmarks ? CE_SCOPE_BLOCK : CE_SCOPE_FILE;
}
/* A binding made in the innermost scope, i.e. since the innermost mark.
   A function's parameters and its body's outermost block share a scope. */
static Bound *lookup_current(Binder *b, CeText key, unsigned space) {
    Bound *bound = lookup(b,key,space);
    if (!bound) return NULL;
    size_t base = b->nmarks ? b->marks[b->nmarks-1].height : 0;
    if (b->nmarks >= 2 && b->marks[b->nmarks-1].kind == MARK_BLOCK &&
        b->marks[b->nmarks-2].kind == MARK_FUNCTION)
        base = b->marks[b->nmarks-2].height;
    for (size_t i = b->height; i > base; --i) if (b->stack[i-1] == bound) return bound;
    return NULL;
}
static Saved save_since(Binder *b, size_t height) {
    Saved saved = {NULL,b->height-height};
    if (saved.len) {
        saved.items = ce_alloc(&b->sema->arena,saved.len*sizeof(*saved.items));
        memcpy(saved.items,b->stack+height,saved.len*sizeof(*saved.items));
    }
    return saved;
}
static void install(Binder *b, Saved saved) {
    for (size_t i = 0; i < saved.len; ++i) {
        Bound *s = saved.items[i];
        bind(b,s->chain->key,s->chain->space,s->entity,s->declaration,s->is_typedef);
    }
}

/* ---- declaration specifiers ---------------------------------------------- */

static void specifier_flags(Binder *b, const CeTree *t, Context *c) {
    if (!t) return;
    if (t->production == -2) { for (size_t i = 0; i < t->len; ++i) specifier_flags(b,t->children[i],c); return; }
    switch (ce_sema_kind(b->sema,t)) {
    case CE_K_UniqueDeclarationSpecifiers:
        if (ce_sema_kind(b->sema,t->children[0]) == CE_K_GNUAutoType) c->deferred = true;
        if (ce_sema_kind(b->sema,t->children[0]) == CE_K_TypedefType) {
            Bound *type = lookup(b,ce_sema_name(b->sema,t->children[0]->children[0]),CE_SPACE_ORDINARY);
            c->function_base = type && type->entity->function_typedef;
        }
        specifier_flags(b,t->children[1],c); break;
    case CE_K_PrimitiveDeclarationSpecifiers: specifier_flags(b,t->children[1],c); break;
    case CE_K_PrefixedDeclarationSpecifiers:
        specifier_flags(b,t->children[0],c); specifier_flags(b,t->children[1],c); break;
    case CE_K_GNUAttributeSpecifiers: specifier_flags(b,t->children[1],c); break;
    case CE_K_NonTypeSpecifierTail: case CE_K_StorageSpecifier: specifier_flags(b,t->children[0],c); break;
    case CE_K_Typedef: c->is_typedef = true; break;
    case CE_K_Extern: c->external = true; break;
    case CE_K_Static: c->internal = true; break;
    default: break;
    }
}

/* ---- ordinary declarations and linkage (§6.2.2) -------------------------- */

static CeEntity *linked(Binder *b, unsigned linkage, CeText key, unsigned kind,
                        const CeTree *declaration) {
    TextSlot *slot = text_slot(&b->linkage,key,linkage);
    if (slot->value) return slot->value;
    CeEntity *entity = ce_sema_entity(b->sema,kind,CE_SPACE_ORDINARY,key,declaration);
    entity->linkage = linkage; entity->scope = CE_SCOPE_FILE;
    slot->value = entity;
    return entity;
}

static bool declare_ordinary(Binder *b, const CeTree *declarator, bool parameter) {
    const CeTree *name = ce_declarator_name(b->sema,declarator);
    if (!name) return ce_error(b->error,declarator ? declarator->offset : 0,"binding cannot find the declared name");
    CeText key = ce_sema_name(b->sema,name);
    unsigned scope = current_scope(b);
    CeEntity *entity;
    if (parameter) {
        if (lookup_current(b,key,CE_SPACE_ORDINARY))
            return ce_error(b->error,name->offset,"duplicate parameter name %.*s",(int)key.len,key.data);
        entity = ce_sema_entity(b->sema,CE_ENTITY_PARAMETER,CE_SPACE_ORDINARY,key,name);
        entity->scope = CE_SCOPE_PROTOTYPE;
        record_use(b->sema,name,entity,name,CE_USE_DECLARATION);
        bind(b,key,CE_SPACE_ORDINARY,entity,name,false);
        return true;
    }
    Context *c = b->contexts;
    if (!c) return ce_error(b->error,name->offset,"declarator binding has no declaration context");
    bool function = declares_function(b->sema,declarator) ||
        (c->function_base && !nearest_derivation(b->sema,declarator));
    if (c->old_parameters) {
        /* §6.9.1p6: the declaration of an identifier-list parameter. */
        Bound *visible = lookup(b,key,CE_SPACE_ORDINARY);
        if (visible && visible->entity->kind == CE_ENTITY_PARAMETER) {
            ce_pointer_put(&b->sema->entity_of,name,visible->entity);
            record_use(b->sema,name,visible->entity,name,CE_USE_DECLARATION);
            bind(b,key,CE_SPACE_ORDINARY,visible->entity,name,false);
            return true;
        }
    }
    if (c->is_typedef) {
        Bound *same = lookup_current(b,key,CE_SPACE_ORDINARY);
        if (same && same->entity->kind != CE_ENTITY_TYPEDEF)
            return ce_error(b->error,name->offset,"typedef conflicts with an ordinary declaration");
        entity = same && same->entity->kind == CE_ENTITY_TYPEDEF ? same->entity :
            ce_sema_entity(b->sema,CE_ENTITY_TYPEDEF,CE_SPACE_ORDINARY,key,name);
        entity->scope = scope;
        entity->function_typedef = function;
    } else if (scope == CE_SCOPE_PROTOTYPE) {
        entity = ce_sema_entity(b->sema,CE_ENTITY_PARAMETER,CE_SPACE_ORDINARY,key,name);
        entity->scope = scope;
    } else if (scope == CE_SCOPE_FILE || function || c->external) {
        Bound *same = lookup_current(b,key,CE_SPACE_ORDINARY);
        if (same && (same->entity->kind == CE_ENTITY_TYPEDEF || same->entity->linkage == CE_LINKAGE_NONE))
            return ce_error(b->error,name->offset,"declaration conflicts with an ordinary name in this scope");
        /* §6.2.2p3–5: static gives internal linkage at file scope; extern and
           function declarations take the linkage of a visible prior
           declaration that has linkage, and otherwise external linkage. */
        unsigned linkage = CE_LINKAGE_EXTERNAL;
        Bound *prior = lookup(b,key,CE_SPACE_ORDINARY);
        if (c->internal && scope == CE_SCOPE_FILE) linkage = CE_LINKAGE_INTERNAL;
        else if (c->internal) return ce_error(b->error,name->offset,"a block-scope function declaration cannot be static");
        else if ((c->external || function) && prior && prior->entity->linkage != CE_LINKAGE_NONE)
            linkage = prior->entity->linkage;
        if (prior && prior->entity->linkage != CE_LINKAGE_NONE && prior->entity->linkage != linkage)
            return ce_error(b->error,name->offset,"conflicting linkage for %.*s",(int)key.len,key.data);
        entity = linked(b,linkage,key,function ? CE_ENTITY_FUNCTION : CE_ENTITY_OBJECT,name);
    } else {
        if (lookup_current(b,key,CE_SPACE_ORDINARY))
            return ce_error(b->error,name->offset,"duplicate local declaration of %.*s",(int)key.len,key.data);
        entity = ce_sema_entity(b->sema,CE_ENTITY_OBJECT,CE_SPACE_ORDINARY,key,name);
        entity->scope = scope;
    }
    if (entity->declaration != name) ce_pointer_put(&b->sema->entity_of,name,entity);
    record_use(b->sema,name,entity,name,CE_USE_DECLARATION);
    if (c->deferred) {
        /* GNU __auto_type: the name becomes visible after its declaration. */
        Bound *pending = ce_alloc(&b->sema->arena,sizeof(*pending));
        *pending = (Bound){chain(b,key,CE_SPACE_ORDINARY),c->is_typedef,entity,name,NULL};
        c->pending.items = ce_resize(c->pending.items,c->pending.len+1,sizeof(*c->pending.items));
        c->pending.items[c->pending.len++] = pending;
        return true;
    }
    bind(b,key,CE_SPACE_ORDINARY,entity,name,c->is_typedef);
    return true;
}

static bool declare_enumerator(Binder *b, const CeTree *node, bool old_parameter) {
    const CeTree *name = node->production == -1 ? node : node->children[0];
    CeText key = ce_sema_name(b->sema,name);
    CeEntity *entity = ce_sema_entity(b->sema,old_parameter ? CE_ENTITY_PARAMETER : CE_ENTITY_ENUMERATOR,
                                      CE_SPACE_ORDINARY,key,name);
    entity->scope = old_parameter ? CE_SCOPE_PROTOTYPE : current_scope(b);
    record_use(b->sema,name,entity,name,CE_USE_DECLARATION);
    bind(b,key,CE_SPACE_ORDINARY,entity,name,false);
    return true;
}

/* ---- the scope program ---------------------------------------------------- */

static bool action(Binder *b, unsigned act, const CeTree *node, int expected) {
    switch (act) {
    case CE_SCOPE_PUSH: push_mark(b,MARK_BLOCK); return true;
    case CE_SCOPE_POP: return pop_mark(b,MARK_BLOCK,true);
    case CE_SCOPE_PROTOTYPE_OPEN: push_mark(b,MARK_PROTOTYPE); return true;
    case CE_SCOPE_PROTOTYPE_CLOSE:
        if (!b->nmarks || b->marks[b->nmarks-1].kind != MARK_PROTOTYPE)
            return ce_error(b->error,0,"prototype close has no saved context");
        b->pending = save_since(b,b->marks[b->nmarks-1].height);
        return pop_mark(b,MARK_PROTOTYPE,true);
    case CE_SCOPE_CONTEXT_KEEP: return pop_mark(b,MARK_PROTOTYPE,false);
    case CE_SCOPE_DECL_OPEN: {
        Context *c = ce_alloc(&b->sema->arena,sizeof(*c));
        *c = (Context){.previous=b->contexts,.old_parameters=b->old_parameter_declarations};
        specifier_flags(b,node,c);
        b->contexts = c; return true;
    }
    case CE_SCOPE_DECL_CLOSE:
        if (!b->contexts) return ce_error(b->error,0,"declaration close has no specifier context");
        install(b,b->contexts->pending);
        b->contexts = b->contexts->previous; return true;
    case CE_SCOPE_BIND_DECLARATOR: return declare_ordinary(b,node,false);
    case CE_SCOPE_BIND_PARAMETER: return declare_ordinary(b,node,true);
    case CE_SCOPE_BIND_ENUMERATOR:
        return declare_enumerator(b,node,node->production == -1 && expected >= 0 &&
                                  ce_is(b->g->symbols[expected].name,"OldParameterName"));
    case CE_SCOPE_RECORD_FUNCTION:
        if (b->nfunctions == b->function_capacity) {
            b->function_capacity = b->function_capacity ? b->function_capacity*2 : 64;
            b->functions = ce_resize(b->functions,b->function_capacity,sizeof(*b->functions));
        }
        b->functions[b->nfunctions++] = (Recorded){node,b->pending}; return true;
    case CE_SCOPE_FUNCTION_OPEN: {
        if (!action(b,CE_SCOPE_DECL_CLOSE,NULL,-1)) return false;
        push_mark(b,MARK_FUNCTION);
        /* §6.9.1p7: the parameters of the derivation nearest the name. */
        const CeTree *nearest = nearest_derivation(b->sema,node);
        for (size_t i = b->nfunctions; nearest && i; --i) if (b->functions[i-1].node == nearest) {
            install(b,b->functions[i-1].parameters); break;
        }
        return true;
    }
    case CE_SCOPE_FUNCTION_CLOSE: return pop_mark(b,MARK_FUNCTION,true);
    }
    return ce_error(b->error,0,"unknown scope program action");
}

static void prepare_program(Binder *b) {
    const CeGrammar *g = b->g;
    b->slot_actions = checked(calloc(g->nproductions,sizeof(*b->slot_actions)));
    b->after_actions = checked(calloc(g->nproductions,sizeof(*b->after_actions)));
    b->child_symbols = checked(calloc(g->nproductions,sizeof(*b->child_symbols)));
    b->transparent_hooked = checked(malloc(g->nsymbols*sizeof(*b->transparent_hooked)));
    for (size_t i = 0; i < g->nsymbols; ++i) b->transparent_hooked[i] = -1;
    unsigned *auxiliary = checked(calloc(g->nsymbols,sizeof(*auxiliary)));
    for (size_t i = 0; i < g->nhooks; ++i)
        if (!g->hooks[i].after_reduce) auxiliary[g->productions[g->hooks[i].production].lhs] = g->hooks[i].action;
    for (size_t p = 0; p < g->nproductions; ++p) {
        const CeProduction *rule = &g->productions[p];
        unsigned *slots = checked(calloc(rule->len+1,sizeof(*slots))); size_t ns = 0, child = 0;
        int *children = checked(calloc(rule->len+1,sizeof(*children)));
        for (size_t i = 0; i < rule->len; ++i) {
            int symbol = rule->rhs[i];
            if (g->symbols[symbol].erased && auxiliary[symbol]) slots[ns++] = (unsigned)(child << 8) | auxiliary[symbol];
            else if (g->symbols[symbol].kind != CE_LITERAL && !g->symbols[symbol].erased) children[child++] = symbol;
        }
        b->slot_actions[p] = slots; b->child_symbols[p] = children;
        unsigned *after = checked(calloc(g->nhooks+1,sizeof(*after))); size_t na = 0;
        for (size_t i = 0; i < g->nhooks; ++i)
            if (g->hooks[i].after_reduce && g->hooks[i].production == (int)p) after[na++] = g->hooks[i].action;
        b->after_actions[p] = after;
        if (ce_is(rule->label,"_") && (ns || na)) b->transparent_hooked[rule->lhs] = (int)p;
    }
    free(auxiliary);
}
static void free_program(Binder *b) {
    for (size_t p = 0; b->slot_actions && p < b->g->nproductions; ++p) {
        free(b->slot_actions[p]); free(b->after_actions[p]); free(b->child_symbols[p]);
    }
    free(b->slot_actions); free(b->after_actions); free(b->child_symbols); free(b->transparent_hooked);
}

/* ---- tags (§6.7.2.3) and labels (§6.2.1p3) -------------------------------- */

static unsigned record_kind(Binder *b, const CeTree *kind) {
    while (ce_sema_kind(b->sema,kind) == CE_K_GNUAttributedStructKind) kind = kind->children[0];
    return ce_sema_kind(b->sema,kind) == CE_K_Union ? CE_ENTITY_UNION : CE_ENTITY_STRUCT;
}
static bool tag_definition(Binder *b, const CeTree *node, unsigned kind, const CeTree *name) {
    CeEntity *entity;
    if (name) {
        CeText key = ce_sema_name(b->sema,name);
        Bound *same = lookup_current(b,key,CE_SPACE_TAG);
        if (same && same->entity->kind != kind)
            return ce_error(b->error,node->offset,"tag %.*s redeclared as a different kind",(int)key.len,key.data);
        if (same && same->entity->defined)
            return ce_error(b->error,node->offset,"tag %.*s is defined twice in one scope",(int)key.len,key.data);
        if (same) entity = same->entity;
        else {
            entity = ce_sema_entity(b->sema,kind,CE_SPACE_TAG,key,name);
            entity->scope = current_scope(b);
            bind(b,key,CE_SPACE_TAG,entity,name,false);
        }
        record_use(b->sema,name,entity,name,CE_USE_TAG);
    } else {
        entity = ce_sema_entity(b->sema,kind,CE_SPACE_TAG,(CeText){NULL,0},NULL);
        entity->scope = current_scope(b);
    }
    entity->defined = true; entity->definition = node;
    ce_pointer_put(&b->sema->tag_of,node,entity);
    return true;
}
static bool tag_reference(Binder *b, const CeTree *node, unsigned kind, const CeTree *name, bool alone) {
    CeText key = ce_sema_name(b->sema,name);
    Bound *visible = alone ? lookup_current(b,key,CE_SPACE_TAG) : lookup(b,key,CE_SPACE_TAG);
    CeEntity *entity;
    if (visible) {
        if (visible->entity->kind != kind)
            return ce_error(b->error,node->offset,"tag %.*s used as a different kind",(int)key.len,key.data);
        entity = visible->entity;
    } else {
        /* §6.7.2.3p7–8: a new incomplete type in the current scope. */
        entity = ce_sema_entity(b->sema,kind,CE_SPACE_TAG,key,name);
        entity->scope = current_scope(b);
        bind(b,key,CE_SPACE_TAG,entity,name,false);
    }
    record_use(b->sema,name,entity,visible ? visible->declaration : name,CE_USE_TAG);
    ce_pointer_put(&b->sema->tag_of,node,entity);
    return true;
}

static void collect_labels(Binder *b, const CeTree *t) {
    if (!t || t->production == -1) return;
    unsigned kind = ce_sema_kind(b->sema,t);
    if (kind == CE_K_Label || kind == CE_K_GNUAttributedLabel ||
        kind == CE_K_GNULabelDeclaration || kind == CE_K_GNUAttributedLabelDeclaration) {
        CeText key = ce_sema_name(b->sema,t->children[0]);
        CeEntity *entity = ce_sema_entity(b->sema,CE_ENTITY_LABEL,CE_SPACE_LABEL,key,t->children[0]);
        entity->scope = CE_SCOPE_FUNCTION; entity->defined = true;
        record_use(b->sema,t->children[0],entity,t->children[0],CE_USE_DECLARATION);
        text_slot(&b->labels,key,CE_SPACE_LABEL)->value = entity;
    }
    for (size_t i = 0; i < t->len; ++i) collect_labels(b,t->children[i]);
}
static bool label_use(Binder *b, const CeTree *token) {
    CeText key = ce_sema_name(b->sema,token);
    CeEntity *entity = b->labels.capacity ? text_slot(&b->labels,key,CE_SPACE_LABEL)->value : NULL;
    if (!entity) return ce_error(b->error,token->offset,"label %.*s is not defined in this function",(int)key.len,key.data);
    record_use(b->sema,token,entity,entity->declaration,CE_USE_LABEL);
    return true;
}

/* Identifiers that GCC and Clang predeclare in the reserved name space. */
static bool builtin_name(CeText key) {
    static const char *prefixes[] = {"__builtin_","__sync_","__atomic_","__c11_atomic_"};
    for (size_t i = 0; i < sizeof(prefixes)/sizeof(*prefixes); ++i) {
        size_t n = strlen(prefixes[i]);
        if (key.len > n && !memcmp(key.data,prefixes[i],n)) return true;
    }
    return ce_is(key,"__func__") || ce_is(key,"__FUNCTION__") || ce_is(key,"__PRETTY_FUNCTION__");
}
static bool reference(Binder *b, const CeTree *token, bool typedef_name) {
    CeText key = ce_sema_name(b->sema,token);
    Bound *visible = lookup(b,key,CE_SPACE_ORDINARY);
    if (!visible) {
        /* Attribute arguments may be attribute words rather than names. */
        if (b->attribute_depth) return true;
        if (typedef_name || !builtin_name(key))
            return ce_error(b->error,token->offset,"%.*s is not declared",(int)key.len,key.data);
        TextSlot *slot = text_slot(&b->builtins,key,CE_SPACE_ORDINARY);
        if (!slot->value) {
            CeEntity *entity = ce_sema_entity(b->sema,CE_ENTITY_BUILTIN,CE_SPACE_ORDINARY,key,NULL);
            entity->linkage = CE_LINKAGE_EXTERNAL; slot->value = entity;
        }
        record_use(b->sema,token,slot->value,NULL,CE_USE_REFERENCE);
        return true;
    }
    if (typedef_name != visible->is_typedef)
        return ce_error(b->error,token->offset,"%.*s: binding disagrees with the parser's typedef classification",
                        (int)key.len,key.data);
    record_use(b->sema,token,visible->entity,visible->declaration,
               b->attribute_depth ? CE_USE_ATTRIBUTE : typedef_name ? CE_USE_TYPEDEF_NAME : CE_USE_REFERENCE);
    return true;
}

/* `struct-or-union identifier ;` declares the tag in the current scope even
   when an outer declaration is visible (§6.7.2.3p7). */
static const CeTree *tag_alone(Binder *b, const CeTree *declaration) {
    if (ce_sema_kind(b->sema,declaration->children[1]) != CE_K_NoInitDeclarators) return NULL;
    const CeTree *specifiers = declaration->children[0];
    if (ce_sema_kind(b->sema,specifiers) != CE_K_UniqueDeclarationSpecifiers) return NULL;
    const CeTree *tail = specifiers->children[1], *unique = specifiers->children[0];
    if (tail->production != -2 || tail->len || ce_sema_kind(b->sema,unique) != CE_K_StructType) return NULL;
    const CeTree *reference = unique->children[0];
    return ce_sema_kind(b->sema,reference) == CE_K_StructReference ? reference : NULL;
}

/* ---- the walk ------------------------------------------------------------- */

static bool visit(Binder *b, const CeTree *t, int expected, size_t depth);

static bool slot_events(Binder *b, const CeTree *t, size_t slot) {
    for (const unsigned *a = b->slot_actions[t->production]; *a; ++a)
        if ((*a >> 8) == slot && !action(b,*a & 0xff,slot ? t->children[slot-1] : NULL,-1)) return false;
    return true;
}
static bool after_events(Binder *b, const CeTree *t) {
    for (const unsigned *a = b->after_actions[t->production]; *a; ++a)
        if (!action(b,*a,t,-1)) return false;
    return true;
}
static bool visit_child(Binder *b, const CeTree *t, size_t i, size_t depth) {
    return visit(b,t->children[i],b->child_symbols[t->production][i],depth+1);
}
/* The production's scope program, interleaved with its children. */
static bool visit_production(Binder *b, const CeTree *t, size_t depth) {
    for (size_t i = 0; i <= t->len; ++i) {
        if (!slot_events(b,t,i)) return false;
        if (i < t->len && !visit_child(b,t,i,depth)) return false;
    }
    return after_events(b,t);
}

static bool visit_node(Binder *b, const CeTree *t, size_t depth) {
    if (t->production == -1) return true;
    if (t->production == -2) {
        int element = b->g->symbols[t->symbol].list_element;
        for (size_t i = 0; i < t->len; ++i) if (!visit(b,t->children[i],element,depth+1)) return false;
        return true;
    }
    switch (ce_sema_kind(b->sema,t)) {
    case CE_K_Var: return reference(b,t->children[0],false);
    case CE_K_TypedefType: return reference(b,t->children[0],true);
    case CE_K_StructDefinition: case CE_K_GNUEmptyStruct: {
        const CeTree *ident = t->children[1];
        const CeTree *name = ce_sema_kind(b->sema,ident) == CE_K_SomeIdent ? ident->children[0] : NULL;
        if (!visit_child(b,t,0,depth) || !tag_definition(b,t,record_kind(b,t->children[0]),name)) return false;
        for (size_t i = 2; i < t->len; ++i) if (!visit_child(b,t,i,depth)) return false;
        return true;
    }
    case CE_K_StructReference:
        return visit_child(b,t,0,depth) && tag_reference(b,t,record_kind(b,t->children[0]),t->children[1],false);
    case CE_K_EnumDefinition: case CE_K_EnumDefinitionComma: {
        const CeTree *ident = t->children[0];
        const CeTree *name = ce_sema_kind(b->sema,ident) == CE_K_SomeIdent ? ident->children[0] : NULL;
        if (!tag_definition(b,t,CE_ENTITY_ENUM,name)) return false;
        for (size_t i = 1; i < t->len; ++i) if (!visit_child(b,t,i,depth)) return false;
        return true;
    }
    case CE_K_EnumReference: return tag_reference(b,t,CE_ENTITY_ENUM,t->children[0],false);
    case CE_K_GNUEnumDefinition: case CE_K_GNUEnumDefinitionComma: {
        const CeTree *ident = t->children[1];
        const CeTree *name = ce_sema_kind(b->sema,ident) == CE_K_SomeIdent ? ident->children[0] : NULL;
        if (!visit_child(b,t,0,depth) || !tag_definition(b,t,CE_ENTITY_ENUM,name)) return false;
        return visit_child(b,t,2,depth);
    }
    case CE_K_GNUEnumReference:
        return visit_child(b,t,0,depth) && tag_reference(b,t,CE_ENTITY_ENUM,t->children[1],false);
    case CE_K_Goto: case CE_K_GNULabelAddress: case CE_K_GNUAsmLabel: return label_use(b,t->children[0]);
    case CE_K_Label: return visit_child(b,t,1,depth);
    case CE_K_GNULabelDeclaration: return visit_child(b,t,1,depth);
    case CE_K_GNUAttributedLabelDeclaration: return visit_child(b,t,1,depth) && visit_child(b,t,2,depth);
    case CE_K_GNUAttributedLabel: return visit_child(b,t,1,depth) && visit_child(b,t,2,depth);
    case CE_K_Member: case CE_K_PointerMember: case CE_K_GNUOffsetNestedMember: return visit_child(b,t,0,depth);
    case CE_K_MemberDesignator: case CE_K_GNUOffsetMember: return true;
    case CE_K_ArrayDesignator: case CE_K_GNUArrayRangeDesignator: {
        /* Designator indices position initializers within the object. */
        ++folded_depth; bool ok = visit_production(b,t,depth); --folded_depth; return ok;
    }
    case CE_K_GNUOldArrayInitializer: {
        ++folded_depth; bool ok = visit_child(b,t,0,depth); --folded_depth;
        return ok && visit_child(b,t,1,depth);
    }
    case CE_K_GNUAsmNamedOperand: return visit_child(b,t,1,depth) && visit_child(b,t,2,depth);
    case CE_K_GNUAttribute: {
        ++b->attribute_depth; bool ok = visit_production(b,t,depth); --b->attribute_depth; return ok;
    }
    case CE_K_ArrayDeclarator: case CE_K_StaticArrayDeclarator: case CE_K_QualifiedStaticArrayDeclarator:
    case CE_K_NestedAbstractArray: case CE_K_NestedAbstractStaticArray:
    case CE_K_NestedAbstractQualifiedStaticArray: case CE_K_AbstractArray: case CE_K_AbstractStaticArray:
    case CE_K_AbstractQualifiedStaticArray: {
        /* Array sizes are constant expressions inside a type. */
        for (size_t i = 0; i <= t->len; ++i) {
            if (!slot_events(b,t,i)) return false;
            if (i == t->len) break;
            bool size = i+1 == t->len;   /* the size is every array form's last child */
            folded_depth += size;
            bool ok = visit_child(b,t,i,depth);
            folded_depth -= size;
            if (!ok) return false;
        }
        return after_events(b,t);
    }
    case CE_K_Declaration: {
        const CeTree *reference = tag_alone(b,t);
        if (!reference) break;
        if (!slot_events(b,t,0) || !visit(b,reference->children[0],-1,depth+1) ||
            !tag_reference(b,reference,record_kind(b,reference->children[0]),reference->children[1],true))
            return false;
        for (size_t i = 1; i <= t->len; ++i) {
            if (!slot_events(b,t,i)) return false;
            if (i < t->len && !visit_child(b,t,i,depth)) return false;
        }
        return after_events(b,t);
    }
    case CE_K_FunctionDefinition: {
        b->labels.len = 0; free(b->labels.slots); b->labels = (TextMap){0};
        collect_labels(b,t->children[3]);
        for (size_t i = 0; i <= t->len; ++i) {
            if (!slot_events(b,t,i)) return false;
            if (i == t->len) break;
            b->old_parameter_declarations = i == 2;
            bool ok = visit_child(b,t,i,depth);
            b->old_parameter_declarations = false;
            if (!ok) return false;
        }
        return after_events(b,t);
    }
    default: break;
    }
    return visit_production(b,t,depth);
}

/* A child whose grammar symbol reaches the node through a transparent
   production runs that production's scope program around it. */
static bool visit(Binder *b, const CeTree *t, int expected, size_t depth) {
    if (depth > 100000) return ce_error(b->error,t->offset,"syntax tree is too deep to bind");
    int hooked = expected >= 0 && expected != t->symbol ? b->transparent_hooked[expected] : -1;
    if (hooked < 0) return visit_node(b,t,depth);
    int inner = b->child_symbols[hooked][0];
    for (const unsigned *a = b->slot_actions[hooked]; *a; ++a)
        if ((*a >> 8) == 0 && !action(b,*a & 0xff,NULL,expected)) return false;
    if (!visit(b,t,inner,depth+1)) return false;
    for (const unsigned *a = b->slot_actions[hooked]; *a; ++a)
        if ((*a >> 8) == 1 && !action(b,*a & 0xff,t,expected)) return false;
    for (const unsigned *a = b->after_actions[hooked]; *a; ++a)
        if (!action(b,*a,t,expected)) return false;
    return true;
}

bool ce_bind(CeSema *sema, const CeTree *unit, CeError *error) {
    Binder b = {.sema=sema,.g=sema->grammar,.error=error};
    prepare_program(&b);
    /* Target-predeclared typedef names (profile `initial typedef`). */
    for (size_t i = 0; i < b.g->ninitial_typedefs; ++i) {
        CeText key = ce_name_key(&sema->arena,b.g->initial_typedefs[i]);
        CeEntity *entity = ce_sema_entity(sema,CE_ENTITY_TYPEDEF,CE_SPACE_ORDINARY,key,NULL);
        entity->scope = CE_SCOPE_FILE;
        bind(&b,key,CE_SPACE_ORDINARY,entity,NULL,true);
    }
    bool ok = visit(&b,unit,-1,0);
    if (ok && (b.nmarks || b.contexts)) ok = ce_error(error,0,"binding scope program leaves an open context");
    free_program(&b);
    free(b.names.slots); free(b.linkage.slots); free(b.builtins.slots); free(b.labels.slots);
    free(b.stack); free(b.marks); free(b.functions);
    return ok;
}

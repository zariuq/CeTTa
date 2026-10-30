#include "cesta.h"

#include <stdlib.h>
#include <string.h>

typedef struct Binding {
    CeText name;
    bool is_typedef;
    struct Binding *previous;
} Binding;
typedef struct Mark {
    Binding *base;
    unsigned kind;
    struct Mark *previous;
} Mark;
typedef struct Declaration {
    bool is_typedef, deferred;
    Binding *pending;
    struct Declaration *previous;
} Declaration;
typedef struct { Binding *first, *stop; } Parameters;
typedef struct { int node; Parameters parameters; } Function;
typedef struct { CeText name; Parameters parameters; bool function; } Declarator;

struct CeScope {
    const CeGrammar *grammar;
    const CeForest *forest;
    CeArena arena;
    Binding *bindings;
    Mark *marks;
    Declaration *declarations;
    Parameters pending;
    Function *functions;
    size_t nfunctions, capacity;
};

static void bind(CeScope *scope, CeText name, bool is_typedef);

/* A UCN and the corresponding source character denote the same C name.
   Scope keys normalize their encoding; syntax trees retain the spelling. */
CeText ce_name_key(CeArena *arena, CeText name) {
    if (!memchr(name.data,'\\',name.len)) return name;
    unsigned char *key = ce_alloc(arena,name.len+1); size_t at = 0;
    for (size_t i = 0; i < name.len;) {
        if (name.data[i] != '\\' || i+1 >= name.len ||
            (name.data[i+1] != 'u' && name.data[i+1] != 'U')) {
            key[at++] = name.data[i++]; continue;
        }
        size_t digits = name.data[i+1] == 'u' ? 4 : 8;
        uint32_t scalar = 0; bool valid = i+2+digits <= name.len;
        for (size_t j = 0; valid && j < digits; ++j) {
            unsigned c = name.data[i+2+j], h;
            if (c >= '0' && c <= '9') h = c-'0';
            else if (c >= 'a' && c <= 'f') h = c-'a'+10;
            else if (c >= 'A' && c <= 'F') h = c-'A'+10;
            else { valid = false; break; }
            scalar = scalar*16+h;
        }
        if (!valid || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) {
            key[at++] = name.data[i++]; continue;
        }
        i += 2+digits;
        if (scalar < 0x80) key[at++] = (unsigned char)scalar;
        else if (scalar < 0x800) {
            key[at++] = (unsigned char)(0xc0 | (scalar >> 6));
            key[at++] = (unsigned char)(0x80 | (scalar & 0x3f));
        } else if (scalar < 0x10000) {
            key[at++] = (unsigned char)(0xe0 | (scalar >> 12));
            key[at++] = (unsigned char)(0x80 | ((scalar >> 6) & 0x3f));
            key[at++] = (unsigned char)(0x80 | (scalar & 0x3f));
        } else {
            key[at++] = (unsigned char)(0xf0 | (scalar >> 18));
            key[at++] = (unsigned char)(0x80 | ((scalar >> 12) & 0x3f));
            key[at++] = (unsigned char)(0x80 | ((scalar >> 6) & 0x3f));
            key[at++] = (unsigned char)(0x80 | (scalar & 0x3f));
        }
    }
    return (CeText){key,at};
}

bool ce_name_equal(CeText first, CeText second) {
    if (ce_equal(first,second)) return true;
    CeArena scratch = {0};
    bool equal = ce_equal(ce_name_key(&scratch,first),ce_name_key(&scratch,second));
    ce_arena_free(&scratch);
    return equal;
}

CeScope *ce_scope_new(const CeGrammar *g, const CeForest *forest) {
    CeScope *scope = calloc(1,sizeof(*scope));
    if (!scope) { fputs("cesta: out of memory\n",stderr); exit(2); }
    scope->grammar = g; scope->forest = forest;
    for (size_t i = 0; i < g->ninitial_typedefs; ++i) bind(scope,g->initial_typedefs[i],true);
    return scope;
}
void ce_scope_free(CeScope *scope) {
    if (!scope) return;
    ce_arena_free(&scope->arena); free(scope->functions); free(scope);
}
bool ce_scope_typedef(const CeScope *scope, CeText name) {
    CeArena arena = {0}; name = ce_name_key(&arena,name); bool result = false;
    for (const Binding *b = scope->bindings; b; b = b->previous)
        if (ce_equal(b->name,name)) { result = b->is_typedef; break; }
    ce_arena_free(&arena); return result;
}
bool ce_scope_closed(const CeScope *scope, CeError *error) {
    return !scope || (!scope->marks && !scope->declarations) ||
        ce_error(error,0,"C scope profile leaves an open context at acceptance");
}
static void bind(CeScope *scope, CeText name, bool is_typedef) {
    if (!name.len) return;
    Binding *binding = ce_alloc(&scope->arena,sizeof(*binding));
    *binding = (Binding){ce_name_key(&scope->arena,name),is_typedef,scope->bindings}; scope->bindings = binding;
}
static void push(CeScope *scope, unsigned kind) {
    Mark *mark = ce_alloc(&scope->arena,sizeof(*mark));
    *mark = (Mark){scope->bindings,kind,scope->marks}; scope->marks = mark;
}
static bool pop(CeScope *scope, unsigned kind, bool restore, CeError *error) {
    Mark *mark = scope->marks;
    if (!mark || mark->kind != kind) return ce_error(error,0,"C scope profile has unbalanced context hooks");
    if (restore) scope->bindings = mark->base;
    scope->marks = mark->previous; return true;
}

static void prefix(const CeForest *forest, int id, int *children, size_t *len) {
    if (id < 0) return;
    CePacked p = forest->nodes[id].alternatives[0];
    prefix(forest,p.left,children,len);
    if (p.right >= 0) children[(*len)++] = p.right;
}
static int *children(const CeScope *scope, int node, size_t *len) {
    *len = 0;
    const CeForestNode *n = &scope->forest->nodes[node];
    if (!n->len) return NULL;
    int production = n->alternatives[0].production;
    if (production < 0) return NULL;
    int *out = ce_resize(NULL,scope->grammar->productions[production].len,sizeof(*out));
    prefix(scope->forest,n->alternatives[0].left,out,len); return out;
}
static bool declaration_property(const CeScope *scope, int node, bool deferred) {
    size_t capacity = 32, len = 1;
    int *pending = ce_resize(NULL,capacity,sizeof(*pending)); pending[0] = node;
    bool found = false;
    while (len && !found) {
        int id = pending[--len];
        const CeForestNode *n = &scope->forest->nodes[id];
        if (!n->len) continue;
        CeText sort = scope->grammar->symbols[n->symbol].name;
        if (deferred && ce_is(sort,"UniqueTypeSpecifier")) {
            int production = n->alternatives[0].production;
            for (size_t i = 0; i < scope->grammar->ndeferred_specifiers; ++i)
                found |= scope->grammar->deferred_specifiers[i] == production;
            continue;
        }
        /* Only this declaration's storage-class spine contributes its flag. */
        if (ce_is(sort,"StorageClass")) {
            const CeProduction *p = &scope->grammar->productions[n->alternatives[0].production];
            found = !deferred && ce_is(p->label,"Typedef"); continue;
        }
        if (!ce_is(sort,"DeclarationSpecifiers") && !ce_is(sort,"NonTypeSpecifier") &&
            !ce_is(sort,"PrimitiveOrNonTypeSpecifier") && !ce_is(sort,"[NonTypeSpecifier]") &&
            !ce_is(sort,"[PrimitiveOrNonTypeSpecifier]")) continue;
        size_t count; int *ids = children(scope,id,&count);
        for (size_t i = 0; i < count; ++i) {
            if (len == capacity) { capacity *= 2; pending = ce_resize(pending,capacity,sizeof(*pending)); }
            pending[len++] = ids[i];
        }
        free(ids);
    }
    free(pending); return found;
}
static CeText first_name(const CeScope *scope, int node) {
    const CeForestNode *n = &scope->forest->nodes[node];
    if (!n->len) return n->symbol == scope->grammar->name_token ? n->spelling : (CeText){NULL,0};
    size_t len; int *ids = children(scope,node,&len);
    CeText name = {NULL,0};
    for (size_t i = 0; i < len && !name.len; ++i) name = first_name(scope,ids[i]);
    free(ids); return name;
}
static Declarator declarator(const CeScope *scope, int node, size_t depth) {
    Declarator result = {{NULL,0},{NULL,NULL},false};
    const CeForestNode *n = &scope->forest->nodes[node];
    if (!n->len || depth > 4096) return result;
    int production = n->alternatives[0].production;
    if (production < 0) return result;
    const CeProduction *p = &scope->grammar->productions[production];
    if (ce_is(p->label,"NameDeclarator")) { result.name = first_name(scope,node); return result; }
    size_t len; int *ids = children(scope,node,&len);
    for (size_t i = 0; i < len && !result.name.len; ++i) {
        int symbol = scope->forest->nodes[ids[i]].symbol;
        if (scope->grammar->symbols[symbol].kind != CE_NONTERMINAL || scope->grammar->symbols[symbol].erased) continue;
        /* Parameter and initializer subtrees do not name the declared object. */
        if (ce_is(scope->grammar->symbols[symbol].name,"ParameterTypeList") ||
            ce_is(scope->grammar->symbols[symbol].name,"OptionalIdentifiers") ||
            ce_is(scope->grammar->symbols[symbol].name,"Initializer")) continue;
        result = declarator(scope,ids[i],depth+1);
    }
    free(ids);
    if (result.name.len && !result.function)
        for (size_t i = scope->nfunctions; i; --i) if (scope->functions[i-1].node == node) {
            result.parameters = scope->functions[i-1].parameters; result.function = true; break;
        }
    return result;
}
static void install(CeScope *scope, Parameters parameters) {
    Binding **items = NULL; size_t len = 0, capacity = 0;
    for (Binding *b = parameters.first; b != parameters.stop; b = b->previous) {
        if (len == capacity) { capacity = capacity ? capacity*2 : 16; items = ce_resize(items,capacity,sizeof(*items)); }
        items[len++] = b;
    }
    while (len) { Binding *b = items[--len]; bind(scope,b->name,b->is_typedef); }
    free(items);
}

bool ce_scope_hook(CeScope *scope, unsigned action, int node, CeError *error) {
    if (action == CE_SCOPE_PUSH) { push(scope,CE_SCOPE_PUSH); return true; }
    if (action == CE_SCOPE_POP) return pop(scope,CE_SCOPE_PUSH,true,error);
    if (action == CE_SCOPE_PROTOTYPE_OPEN) { push(scope,CE_SCOPE_PROTOTYPE_OPEN); return true; }
    if (action == CE_SCOPE_PROTOTYPE_CLOSE) {
        if (!scope->marks || scope->marks->kind != CE_SCOPE_PROTOTYPE_OPEN)
            return ce_error(error,0,"prototype close has no saved context");
        scope->pending = (Parameters){scope->bindings,scope->marks->base};
        return pop(scope,CE_SCOPE_PROTOTYPE_OPEN,true,error);
    }
    if (action == CE_SCOPE_CONTEXT_KEEP) return pop(scope,CE_SCOPE_PROTOTYPE_OPEN,false,error);
    if (action == CE_SCOPE_DECL_CLOSE) {
        if (!scope->declarations) return ce_error(error,0,"declaration close has no specifier context");
        install(scope,(Parameters){scope->declarations->pending,NULL});
        scope->declarations = scope->declarations->previous; return true;
    }
    if (action == CE_SCOPE_FUNCTION_CLOSE) return pop(scope,CE_SCOPE_FUNCTION_OPEN,true,error);
    if (node < 0) return ce_error(error,0,"C scope hook requires a preceding syntax value");
    if (action == CE_SCOPE_DECL_OPEN) {
        Declaration *d = ce_alloc(&scope->arena,sizeof(*d));
        *d = (Declaration){declaration_property(scope,node,false),declaration_property(scope,node,true),NULL,scope->declarations}; scope->declarations = d; return true;
    }
    if (action == CE_SCOPE_BIND_ENUMERATOR) { bind(scope,first_name(scope,node),false); return true; }
    if (action == CE_SCOPE_RECORD_FUNCTION) {
        if (scope->nfunctions == scope->capacity) {
            scope->capacity = scope->capacity ? scope->capacity*2 : 16;
            scope->functions = ce_resize(scope->functions,scope->capacity,sizeof(*scope->functions));
        }
        scope->functions[scope->nfunctions++] = (Function){node,scope->pending}; return true;
    }
    Declarator d = declarator(scope,node,0);
    if (!d.name.len) return ce_error(error,0,"binding hook cannot find the declared name");
    if (action == CE_SCOPE_BIND_DECLARATOR) {
        if (!scope->declarations) return ce_error(error,0,"declarator binding has no declaration context");
        if (scope->declarations->deferred) {
            Binding *b = ce_alloc(&scope->arena,sizeof(*b));
            *b = (Binding){d.name,scope->declarations->is_typedef,scope->declarations->pending};
            scope->declarations->pending = b; return true;
        }
        bind(scope,d.name,scope->declarations->is_typedef); return true;
    }
    if (action == CE_SCOPE_BIND_PARAMETER) { bind(scope,d.name,false); return true; }
    if (action == CE_SCOPE_FUNCTION_OPEN) {
        if (!ce_scope_hook(scope,CE_SCOPE_DECL_CLOSE,-1,error)) return false;
        push(scope,CE_SCOPE_FUNCTION_OPEN);
        if (d.function) install(scope,d.parameters);
        return true;
    }
    return ce_error(error,0,"unknown C scope hook action");
}

#include "cesta.h"

#include <stdlib.h>
#include <string.h>

/* Terms describe surface atoms; equations interpret them in the AST
   signature. This interpreter has no C spelling or constructor tables. */

void ce_view_free(CeViewProfile *profile) {
    ce_arena_free(&profile->arena); free(profile->rules); memset(profile,0,sizeof(*profile));
}
void ce_view_stats_free(CeViewStats *stats) {
    free(stats->by_constructor); free(stats->short_by_constructor); memset(stats,0,sizeof(*stats));
}

static int symbol_named(const CeGrammar *g, CeText name) {
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].kind != CE_LITERAL && ce_equal(g->symbols[i].name,name)) return (int)i;
    return -1;
}
static int production_named(const CeGrammar *g, CeText name) {
    for (size_t i = 0; i < g->nproductions; ++i) {
        const CeProduction *p = &g->productions[i];
        if (!p->internal && !g->symbols[p->lhs].erased &&
            g->symbols[p->lhs].list_element < 0 && !ce_is(p->label,"_") && ce_equal(p->label,name)) return (int)i;
    }
    return -1;
}
static bool same_sort(const CeGrammar *g, int first, int second) {
    return first >= 0 && second >= 0 && g->symbols[first].sort == g->symbols[second].sort;
}
static int token_sort(const CeGrammar *g, int sort) {
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].kind == CE_TOKEN && same_sort(g,sort,(int)i)) return (int)i;
    return -1;
}
static size_t fields(const CeGrammar *g, const CeProduction *p, int *out) {
    size_t len = 0;
    for (size_t i = 0; i < p->len; ++i) {
        int symbol = p->rhs[i];
        if (g->symbols[symbol].kind != CE_LITERAL && !g->symbols[symbol].erased) {
            if (out) out[len] = symbol;
            ++len;
        }
    }
    return len;
}
static CeText source_name(const CeGrammar *g, const CeTree *tree, CeArena *arena) {
    if (tree->production == -1) return tree->spelling;
    if (ce_tree_label(g,tree,"QualifiedDottedPath")) {
        CeText first = source_name(g,tree->children[0],arena), second = source_name(g,tree->children[1],arena);
        unsigned char *bytes = ce_alloc(arena,first.len+second.len+2);
        memcpy(bytes,first.data,first.len); bytes[first.len] = '.';
        memcpy(bytes+first.len+1,second.data,second.len);
        return (CeText){bytes,first.len+second.len+1};
    }
    if (tree->len == 1) return source_name(g,tree->children[0],arena);
    return (CeText){NULL,0};
}

static CeViewTemplate *template_new(CeViewProfile *profile, unsigned kind, size_t len) {
    CeViewTemplate *body = ce_alloc(&profile->arena,sizeof(*body));
    body->kind = kind; body->len = len; body->symbol = body->production = -1;
    body->children = ce_alloc(&profile->arena,len*sizeof(*body->children));
    return body;
}

static CeViewTemplate *template_source(CeViewProfile *profile, const CeGrammar *syntax,
                                       const CeTree *tree, int sort, int parent, const CeText *variables,
                                       CeViewRule *rule, unsigned *uses, CeError *error) {
    const CeGrammar *g = profile->grammar;
    if (ce_tree_label(syntax,tree,"ASTVar")) {
        CeText name = source_name(syntax,tree->children[0],&profile->arena);
        for (size_t i = 0; i < rule->arity; ++i) if (ce_equal(name,variables[i])) {
            if (!same_sort(g,sort,rule->arguments[i])) {
                ce_error(error,tree->offset,"presentation hole has the wrong AST sort"); return NULL;
            }
            ++uses[i];
            rule->argument_parents[i] = parent;
            CeViewTemplate *body = template_new(profile,CE_VIEW_SLOT,0);
            body->slot = i; body->symbol = sort; return body;
        }
        ce_error(error,tree->offset,"unbound presentation hole %.*s",(int)name.len,name.data); return NULL;
    }
    if (!ce_tree_label(syntax,tree,"ASTSExp")) {
        ce_error(error,tree->offset,"interpretation requires constructor templates, not substitution or freshness syntax"); return NULL;
    }
    CeText head = source_name(syntax,tree->children[0],&profile->arena);
    const CeTree *children = tree->children[1];
    int list = -1;
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].list_element >= 0 && same_sort(g,sort,(int)i)) { list = (int)i; break; }
    if (list >= 0) {
        if (!ce_is(head,"List")) { ce_error(error,tree->offset,"list interpretation requires List"); return NULL; }
        CeViewTemplate *body = template_new(profile,CE_VIEW_LIST,children->len); body->symbol = list;
        for (size_t i = 0; i < children->len; ++i) {
            body->children[i] = template_source(profile,syntax,children->children[i],g->symbols[list].list_element,-1,variables,rule,uses,error);
            if (!body->children[i]) return NULL;
        }
        return body;
    }
    int production = production_named(g,head);
    if (production < 0) { ce_error(error,tree->offset,"unknown AST interpretation constructor %.*s",(int)head.len,head.data); return NULL; }
    const CeProduction *p = &g->productions[production];
    if (!same_sort(g,p->lhs,sort)) { ce_error(error,tree->offset,"interpretation constructor has the wrong result sort"); return NULL; }
    size_t arity = fields(g,p,NULL);
    if (arity != children->len) { ce_error(error,tree->offset,"interpretation constructor has the wrong arity"); return NULL; }
    int *arguments = ce_alloc(&profile->arena,arity*sizeof(*arguments)); fields(g,p,arguments);
    CeViewTemplate *body = template_new(profile,CE_VIEW_NODE,arity);
    body->symbol = p->lhs; body->production = production;
    for (size_t i = 0; i < arity; ++i) {
        body->children[i] = template_source(profile,syntax,children->children[i],arguments[i],production,variables,rule,uses,error);
        if (!body->children[i]) return NULL;
    }
    return body;
}

static bool shape(CeViewProfile *profile, const CeGrammar *surface, const CeProduction *p,
                   CeViewRule *rule, CeError *error) {
    size_t arity = fields(surface,p,NULL);
    rule->arity = arity; rule->arguments = ce_alloc(&profile->arena,arity*sizeof(*rule->arguments));
    rule->argument_parents = ce_alloc(&profile->arena,arity*sizeof(*rule->argument_parents));
    for (size_t i = 0; i < arity; ++i) rule->argument_parents[i] = -1;
    int result = symbol_named(profile->grammar,surface->symbols[p->lhs].name);
    if (result < 0) return ce_error(error,p->offset,"surface result category is outside the AST signature");
    rule->result = result;
    size_t arg = 0;
    for (size_t i = 0; i < p->len; ++i) if (surface->symbols[p->rhs[i]].kind != CE_LITERAL) {
        int target = symbol_named(profile->grammar,surface->symbols[p->rhs[i]].name);
        if (target < 0) return ce_error(error,p->offset,"surface argument category is outside the AST signature");
        rule->arguments[arg++] = target;
    }
    /* A "." before the last field writes it as a dotted tail: (h a . rest),
       so a rest that is itself a list of several atoms continues the form. */
    bool dotted = p->len >= 5 && arity && surface->symbols[p->rhs[p->len-3]].kind == CE_LITERAL &&
        ce_is(surface->symbols[p->rhs[p->len-3]].name,".") && surface->symbols[p->rhs[p->len-2]].kind != CE_LITERAL;
    size_t marker = dotted ? 1 : 0;
    if (dotted && profile->grammar->symbols[rule->arguments[arity-1]].list_element >= 0)
        return ce_error(error,p->offset,"a dotted tail must have a non-list sort");
    if (p->len == 1 && !arity) {
        rule->shape = CE_VIEW_WORD; rule->head = surface->symbols[p->rhs[0]].name;
    } else if (p->len == 1 && arity == 1) {
        int token = token_sort(profile->grammar,rule->arguments[0]);
        rule->shape = token >= 0 ? CE_VIEW_LEXICAL : CE_VIEW_PASS;
    }
    else if (p->len == 2 && !arity && ce_is(surface->symbols[p->rhs[0]].name,"(") && ce_is(surface->symbols[p->rhs[1]].name,")"))
        rule->shape = CE_VIEW_EMPTY;
    else if (p->len >= 3 && ce_is(surface->symbols[p->rhs[0]].name,"(") && ce_is(surface->symbols[p->rhs[p->len-1]].name,")") &&
             surface->symbols[p->rhs[0]].kind == CE_LITERAL && surface->symbols[p->rhs[p->len-1]].kind == CE_LITERAL) {
        /* A tuple begins with a field; a headed form begins with a word. Fixed
           words may follow either, and may also close a spliced list. */
        bool headed_form = surface->symbols[p->rhs[1]].kind == CE_LITERAL;
        size_t first = headed_form ? 2 : 1;
        rule->shape = headed_form ? CE_VIEW_FORM : CE_VIEW_TUPLE; rule->dotted = dotted;
        if (headed_form) rule->head = surface->symbols[p->rhs[1]].name;
        rule->nfields = p->len-first-1-marker;
        rule->fields = ce_alloc(&profile->arena,rule->nfields*sizeof(*rule->fields));
        size_t slot = 0, last_slot = SIZE_MAX;
        for (size_t i = 0; i < rule->nfields; ++i) {
            const CeSymbol *field = &surface->symbols[p->rhs[i+first+(dotted && i+1 == rule->nfields)]];
            if (field->kind != CE_LITERAL) last_slot = i;
            rule->fields[i].slot = field->kind == CE_LITERAL ? SIZE_MAX : slot++;
            if (field->kind == CE_LITERAL) rule->fields[i].word = field->name;
        }
        rule->splice = !dotted && last_slot != SIZE_MAX &&
            profile->grammar->symbols[rule->arguments[arity-1]].list_element >= 0;
        rule->suffix = rule->splice ? rule->nfields-1-last_slot : 0;
        if (dotted && last_slot+1 != rule->nfields) return ce_error(error,p->offset,"a dotted tail must be the last field");
        if (!headed_form && rule->nfields == arity) rule->fields = NULL;
    } else if (p->len == 2 && arity == 1 && surface->symbols[p->rhs[0]].kind == CE_LITERAL &&
               token_sort(profile->grammar,rule->arguments[0]) >= 0) {
        /* A prefixed identifier is one symbol; an empty prefix spells a
           literal token such as 42 or 'a' as a bare symbol. */
        rule->shape = CE_VIEW_PREFIX; rule->head = surface->symbols[p->rhs[0]].name;
        bool identifier = ce_token_symbol_representation(profile->grammar,rule->arguments[0]);
        if (identifier && !rule->head.len) return ce_error(error,p->offset,"token prefix must be nonempty");
        if (!identifier && rule->head.len) return ce_error(error,p->offset,"a literal token is spelled as a bare symbol without prefix");
    } else return ce_error(error,p->offset,"surface rule must describe a word, atom, empty list, tuple or headed structural form");
    return true;
}

static bool starts_with(CeText value, CeText prefix) {
    return value.len >= prefix.len && !memcmp(value.data,prefix.data,prefix.len);
}
static bool token_starts(const CeGrammar *g, int state, unsigned byte, unsigned char *seen) {
    if (state < 0 || seen[state]) return false;
    seen[state] = 1;
    const CeNfaState *s = &g->nfa[state];
    if (!s->epsilon) return (s->bytes.word[byte/64] >> (byte%64))&1;
    return token_starts(g,s->out[0],byte,seen) || token_starts(g,s->out[1],byte,seen);
}
static bool token_language_starts(const CeGrammar *g, int symbol, unsigned byte) {
    for (size_t i = 0; i < g->nlex; ++i) if (g->lex[i].symbol == symbol) {
        unsigned char *seen = calloc(g->nnfa,1);
        if (!seen) { fputs("cesta: out of memory\n",stderr); exit(2); }
        bool starts = token_starts(g,g->lex[i].start,byte,seen);
        free(seen); if (starts) return true;
    }
    return false;
}
static bool prefix_overlaps_token(const CeGrammar *g, const CeViewRule *prefix, const CeViewRule *leaf) {
    if (!ce_token_symbol_representation(g,leaf->arguments[0])) return false;
    int symbol = token_sort(g,leaf->arguments[0]);
    if (token_language_starts(g,symbol,prefix->head.data[0])) return true;
    for (size_t i = 0; i < g->naliases; ++i)
        if (ce_equal(g->aliases[i].token,g->symbols[symbol].name) && starts_with(g->aliases[i].spelling,prefix->head)) return true;
    return false;
}
static bool tokens_overlap(const CeGrammar *g, const CeViewRule *leaf, const CeViewRule *other) {
    int a = token_sort(g,leaf->arguments[0]), b = token_sort(g,other->arguments[0]);
    if (a == b) return true;
    if (leaf->argument_parents[0] < 0 || other->argument_parents[0] < 0) return false;
    for (size_t i = 0; i < g->naliases; ++i) {
        const CeLexAlias *alias = &g->aliases[i];
        if (!ce_equal(alias->production,g->productions[leaf->argument_parents[0]].label) ||
            !ce_equal(alias->token,g->symbols[a].name)) continue;
        for (size_t j = 0; j < g->naliases; ++j)
            if (ce_equal(g->aliases[j].production,g->productions[other->argument_parents[0]].label) &&
                ce_equal(g->aliases[j].token,g->symbols[b].name) &&
                ce_equal(g->aliases[j].spelling,alias->spelling)) return true;
    }
    return false;
}

static bool token_accepts(const CeGrammar *g, const CeViewRule *rule, const CeAtom *atom, size_t depth) {
    CeArena arena = {0}; CeTree *token; CeError error = {{0},0};
    bool ok = ce_tree_decode_atom(g,atom,rule->arguments[0],rule->argument_parents[0],
        &arena,NULL,NULL,&token,&error,depth);
    ce_arena_free(&arena); return ok;
}
static bool lexical_accepts(const CeViewProfile *profile, const CeViewRule *rule,
                             const CeAtom *atom, size_t depth) {
    return token_accepts(profile->grammar,rule,atom,depth);
}
/* The token a prefix rule reads from an atom. An empty prefix writes a token
   as the atom its spelling reads as: 42 is a symbol and "hi\n" is a string,
   so the token spelling is the atom's printed form. */
static CeAtom *prefix_argument(const CeGrammar *g, const CeViewRule *rule,
                               const CeAtom *atom, CeArena *arena) {
    CeTree token = {.production=-1,.symbol=rule->arguments[0]};
    if (!rule->head.len && atom->kind == CE_ATOM_TEXT) token.spelling = ce_quoted(arena,atom->text);
    else if (atom->kind != CE_ATOM_SYMBOL || !starts_with(atom->text,rule->head) || atom->text.len == rule->head.len) return NULL;
    else token.spelling = ce_text(arena,atom->text.data+rule->head.len,atom->text.len-rule->head.len);
    return ce_tree_to_atom(g,&token,arena);
}
/* Whether a token written as its own atom can be a string atom. */
static bool spelled_as_text(const CeGrammar *g, const CeViewRule *rule) {
    return rule->shape == CE_VIEW_PREFIX && !rule->head.len && token_language_starts(g,token_sort(g,rule->arguments[0]),'"');
}
/* Whether a word, lexical or prefix leaf accepts one symbol atom. */
static bool symbol_admitted(const CeGrammar *g, const CeViewRule *leaf, const CeAtom *atom) {
    if (atom->kind != CE_ATOM_SYMBOL) return false;
    if (leaf->shape == CE_VIEW_WORD) return ce_equal(atom->text,leaf->head);
    if (leaf->shape == CE_VIEW_LEXICAL) return token_accepts(g,leaf,atom,0);
    CeArena arena = {0}; CeAtom *argument = prefix_argument(g,leaf,atom,&arena);
    bool ok = argument && token_accepts(g,leaf,argument,0);
    ce_arena_free(&arena); return ok;
}

/* Overlap of two unstructured leaves of one sort: words, lexical tokens and
   prefixed tokens. A bare literal token has the empty prefix. */
static bool leaf_shapes_overlap(const CeGrammar *g, const CeViewRule *first, const CeViewRule *second) {
    if (first->shape == CE_VIEW_PREFIX || second->shape == CE_VIEW_PREFIX) {
        const CeViewRule *prefix = first->shape == CE_VIEW_PREFIX ? first : second;
        const CeViewRule *other = prefix == first ? second : first;
        if (!prefix->head.len) {
            if (other->shape == CE_VIEW_PREFIX && other->head.len)
                return token_language_starts(g,token_sort(g,prefix->arguments[0]),other->head.data[0]);
            /* A token written as its atom is a symbol, or a string when its
               spelling is one; a lexical literal is its spelling as a string. */
            if (other->shape == CE_VIEW_LEXICAL && !ce_token_symbol_representation(g,token_sort(g,other->arguments[0])))
                return spelled_as_text(g,prefix);
            if (other->shape == CE_VIEW_PREFIX && !other->head.len && spelled_as_text(g,prefix) && spelled_as_text(g,other)) return true;
            if (other->shape == CE_VIEW_PREFIX || other->shape == CE_VIEW_LEXICAL) return tokens_overlap(g,prefix,other);
            if (other->shape == CE_VIEW_WORD) { CeAtom word = {.kind=CE_ATOM_SYMBOL,.text=other->head}; return symbol_admitted(g,prefix,&word); }
            return false;
        }
        if (other->shape == CE_VIEW_PREFIX)
            return other->head.len ? true : token_language_starts(g,token_sort(g,other->arguments[0]),prefix->head.data[0]);
        if (other->shape == CE_VIEW_LEXICAL) return prefix_overlaps_token(g,prefix,other);
        if (other->shape == CE_VIEW_WORD) return starts_with(other->head,prefix->head);
        return false;
    }
    if (first->shape == CE_VIEW_LEXICAL || second->shape == CE_VIEW_LEXICAL) {
        const CeViewRule *leaf = first->shape == CE_VIEW_LEXICAL ? first : second;
        const CeViewRule *other = leaf == first ? second : first;
        return other->shape == CE_VIEW_LEXICAL && tokens_overlap(g,leaf,other);
    }
    return first->shape == second->shape && ce_equal(first->head,second->head);
}
static bool dispatch_overlaps(const CeGrammar *g, const CeViewRule *first, const CeViewRule *second) {
    return same_sort(g,first->result,second->result) && leaf_shapes_overlap(g,first,second);
}
static bool list_shape(const CeViewRule *rule) {
    return rule->shape == CE_VIEW_FORM || rule->shape == CE_VIEW_TUPLE;
}

/* First-atom keys. A key set over-approximates the top level of every atom
   that a sort or a rule position accepts: symbols (words, lexical tokens and
   prefixed tokens), text, lists by head symbol, lists headed by a list or text,
   and the empty list. Dispatch admits an atom only when its keys allow it, and
   two rules overlap only when every position of some common length can hold
   the same atom: one-atom lookahead, as in LL(1) parsing, over S-expressions. */
typedef struct { const CeViewRule **leaves; size_t len, capacity; bool any; } LeafSet;
typedef struct { LeafSet symbols, heads; bool text, other_head, empty; } AtomKeys;
struct CeViewKeys { AtomKeys *rules, *full; const CeViewRule **synthetic; size_t nsynthetic, capacity; };

static bool leaf_add(CeArena *arena, LeafSet *set, const CeViewRule *leaf) {
    for (size_t i = 0; i < set->len; ++i) if (set->leaves[i] == leaf) return false;
    if (set->len == set->capacity) {
        size_t capacity = set->capacity ? set->capacity*2 : 8;
        const CeViewRule **leaves = ce_alloc(arena,capacity*sizeof(*leaves));
        if (set->len) memcpy(leaves,set->leaves,set->len*sizeof(*leaves));
        set->leaves = leaves; set->capacity = capacity;
    }
    set->leaves[set->len++] = leaf; return true;
}
static bool leaves_union(CeArena *arena, LeafSet *into, const LeafSet *from) {
    bool changed = from->any && !into->any;
    into->any |= from->any;
    for (size_t i = 0; i < from->len; ++i) changed |= leaf_add(arena,into,from->leaves[i]);
    return changed;
}
static bool keys_union(CeArena *arena, AtomKeys *into, const AtomKeys *from) {
    bool changed = leaves_union(arena,&into->symbols,&from->symbols);
    changed |= leaves_union(arena,&into->heads,&from->heads);
    changed |= (from->text && !into->text) || (from->other_head && !into->other_head) || (from->empty && !into->empty);
    into->text |= from->text; into->other_head |= from->other_head; into->empty |= from->empty;
    return changed;
}
static void any_list(AtomKeys *keys) { keys->heads.any = keys->other_head = keys->empty = true; }

static const CeViewRule *synthetic_leaf(CeViewProfile *profile, unsigned shape, CeText word, int token, int parent) {
    struct CeViewKeys *keys = profile->keys;
    for (size_t i = 0; i < keys->nsynthetic; ++i) {
        const CeViewRule *leaf = keys->synthetic[i];
        if (leaf->shape == shape && (shape == CE_VIEW_WORD ? ce_equal(leaf->head,word) :
            leaf->arguments[0] == token && leaf->argument_parents[0] == parent)) return leaf;
    }
    CeViewRule *leaf = ce_alloc(&profile->arena,sizeof(*leaf));
    leaf->shape = shape; leaf->result = -1; leaf->name = ce_cstr(&profile->arena,"(key)");
    if (shape == CE_VIEW_WORD) leaf->head = ce_text(&profile->arena,word.data,word.len);
    else {
        leaf->arity = 1;
        leaf->arguments = ce_alloc(&profile->arena,sizeof(*leaf->arguments)); leaf->arguments[0] = token;
        leaf->argument_parents = ce_alloc(&profile->arena,sizeof(*leaf->argument_parents)); leaf->argument_parents[0] = parent;
    }
    if (keys->nsynthetic == keys->capacity) {
        size_t capacity = keys->capacity ? keys->capacity*2 : 64;
        const CeViewRule **grown = ce_alloc(&profile->arena,capacity*sizeof(*grown));
        if (keys->nsynthetic) memcpy(grown,keys->synthetic,keys->nsynthetic*sizeof(*grown));
        keys->synthetic = grown; keys->capacity = capacity;
    }
    keys->synthetic[keys->nsynthetic++] = leaf; return leaf;
}
static size_t list_minimum(const CeGrammar *g, int list) {
    for (size_t i = 0; i < g->nproductions; ++i)
        if (!g->productions[i].internal && same_sort(g,g->productions[i].lhs,list) && ce_is(g->productions[i].label,"[]")) return 0;
    return 1;
}
static int list_element(const CeGrammar *g, int sort) {
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].list_element >= 0 && same_sort(g,sort,(int)i)) return g->symbols[i].list_element;
    return -1;
}
static void sort_keys(CeViewProfile *profile, int sort, int parent, AtomKeys *out);
/* A list atom of a list sort is headed by its first element, or is c:List. */
static void list_keys(CeViewProfile *profile, int sort, AtomKeys *out) {
    const CeGrammar *g = profile->grammar; memset(out,0,sizeof(*out));
    AtomKeys element; sort_keys(profile,list_element(g,sort),-1,&element);
    out->heads = element.symbols;
    out->other_head = element.text || element.heads.any || element.heads.len || element.other_head || element.empty;
    out->empty = !list_minimum(g,sort);
    leaf_add(&profile->arena,&out->heads,synthetic_leaf(profile,CE_VIEW_WORD,ce_cstr(&profile->arena,"c:List"),-1,-1));
}
static void sort_keys(CeViewProfile *profile, int sort, int parent, AtomKeys *out) {
    const CeGrammar *g = profile->grammar; memset(out,0,sizeof(*out));
    int token = token_sort(g,sort);
    if (token >= 0) {
        if (ce_token_symbol_representation(g,token)) {
            leaf_add(&profile->arena,&out->symbols,synthetic_leaf(profile,CE_VIEW_LEXICAL,(CeText){NULL,0},token,parent));
            leaf_add(&profile->arena,&out->heads,synthetic_leaf(profile,CE_VIEW_WORD,ce_cstr(&profile->arena,"c:Symbol"),-1,-1));
        } else out->text = true;
    } else *out = profile->keys->full[g->symbols[sort].sort];
}

enum { POSITION_NONE, POSITION_WORD, POSITION_SORT, POSITION_FIRST, POSITION_ANY };
typedef struct { unsigned kind; CeText word; int sort, parent; } Position;
/* What item `at` of an n-item list atom holds under a list-shaped rule; for a
   dotted tail of several atoms, the first is the head of the tail's list. */
static Position field_position(const CeViewRule *rule, size_t field) {
    size_t slot = rule->fields ? rule->fields[field].slot : field;
    if (slot == SIZE_MAX) return (Position){POSITION_WORD,rule->fields[field].word,-1,-1};
    return (Position){POSITION_SORT,{NULL,0},rule->arguments[slot],rule->argument_parents[slot]};
}
static Position position_at(const CeGrammar *g, const CeViewRule *rule, size_t at, size_t n) {
    size_t prefix = rule->shape == CE_VIEW_FORM ? 1 : 0;
    if (prefix && !at) return (Position){POSITION_WORD,rule->head,-1,-1};
    size_t item = at-prefix, items = n ? n-prefix : 0;
    Position any = {POSITION_ANY,{NULL,0},-1,-1};
    if (rule->splice) {
        size_t lead = rule->nfields-1-rule->suffix;
        int sort = rule->arguments[rule->arity-1];
        if (item < lead) return field_position(rule,item);
        if (n && item >= items) return (Position){POSITION_NONE,{NULL,0},-1,-1};
        if (n && item+rule->suffix >= items) return field_position(rule,rule->nfields-(items-item));
        if (!n && rule->suffix && !list_minimum(g,sort)) return any;
        return (Position){POSITION_SORT,{NULL,0},list_element(g,sort),-1};
    }
    if (rule->dotted && item+1 >= rule->nfields) {
        int sort = rule->arguments[rule->arity-1];
        if (n && items == rule->nfields) return (Position){POSITION_SORT,{NULL,0},sort,rule->argument_parents[rule->arity-1]};
        if (n && item+1 == rule->nfields) return (Position){POSITION_FIRST,{NULL,0},sort,-1};
        return any;
    }
    if (item >= rule->nfields) return (Position){POSITION_NONE,{NULL,0},-1,-1};
    return field_position(rule,item);
}
static void position_keys(CeViewProfile *profile, Position position, AtomKeys *out) {
    memset(out,0,sizeof(*out));
    if (position.kind == POSITION_WORD)
        leaf_add(&profile->arena,&out->symbols,synthetic_leaf(profile,CE_VIEW_WORD,position.word,-1,-1));
    else if (position.kind == POSITION_ANY) { out->symbols.any = out->text = true; any_list(out); }
    else if (position.kind == POSITION_SORT) sort_keys(profile,position.sort,position.parent,out);
    else if (position.kind == POSITION_FIRST) {
        AtomKeys list; sort_keys(profile,position.sort,-1,&list);
        out->symbols = list.heads;
        if (list.other_head) { out->text = true; any_list(out); }
    }
}
static bool leaves_overlap(const CeGrammar *g, const CeViewRule *first, const CeViewRule *second) {
    if (first->shape == CE_VIEW_WORD && second->shape == CE_VIEW_WORD) return ce_equal(first->head,second->head);
    if (first->shape == CE_VIEW_WORD || second->shape == CE_VIEW_WORD) {
        const CeViewRule *word = first->shape == CE_VIEW_WORD ? first : second;
        CeAtom atom = {.kind=CE_ATOM_SYMBOL,.text=word->head};
        return symbol_admitted(g,word == first ? second : first,&atom);
    }
    return leaf_shapes_overlap(g,first,second);
}
static bool leafsets_overlap(const CeGrammar *g, const LeafSet *first, const LeafSet *second) {
    if ((first->any && (second->any || second->len)) || (second->any && first->len)) return true;
    for (size_t i = 0; i < first->len; ++i)
        for (size_t j = 0; j < second->len; ++j)
            if (leaves_overlap(g,first->leaves[i],second->leaves[j])) return true;
    return false;
}
static bool keys_overlap(const CeGrammar *g, const AtomKeys *first, const AtomKeys *second) {
    return (first->text && second->text) || (first->other_head && second->other_head) ||
        (first->empty && second->empty) || leafsets_overlap(g,&first->symbols,&second->symbols) ||
        leafsets_overlap(g,&first->heads,&second->heads);
}
static bool leafset_admits(const CeGrammar *g, const LeafSet *set, const CeAtom *symbol) {
    if (set->any) return true;
    for (size_t i = 0; i < set->len; ++i) if (symbol_admitted(g,set->leaves[i],symbol)) return true;
    return false;
}
static bool keys_admit(const CeGrammar *g, const AtomKeys *keys, const CeAtom *atom) {
    if (atom->kind == CE_ATOM_SYMBOL) return leafset_admits(g,&keys->symbols,atom);
    if (atom->kind == CE_ATOM_TEXT) return keys->text;
    if (!atom->len) return keys->empty;
    if (atom->items[0]->kind == CE_ATOM_SYMBOL) return leafset_admits(g,&keys->heads,atom->items[0]);
    return keys->other_head;
}
/* Closed admission uses precomputed keys. Open extensions additionally follow
   the declared pass graph to reach the appropriate syntax position. */
static bool extension_sort_visit(const CeViewProfile *profile, const CeAtom *atom, int sort,
                                  CeViewExtensionAccepts accepts, void *context, unsigned char *seen) {
    int normal = profile->grammar->symbols[sort].sort;
    if (seen[normal]) return false;
    seen[normal] = 1;
    if (accepts(context,atom,sort)) return true;
    for (size_t i = 0; i < profile->len; ++i) {
        const CeViewRule *rule = &profile->rules[i];
        if (rule->shape == CE_VIEW_PASS && same_sort(profile->grammar,rule->result,sort) &&
            extension_sort_visit(profile,atom,rule->arguments[0],accepts,context,seen)) return true;
    }
    return false;
}
static bool extension_sort_admits(const CeViewProfile *profile, const CeAtom *atom, int sort,
                                   CeViewExtensionAccepts accepts, void *context) {
    if (!accepts) return false;
    unsigned char *seen = calloc(profile->grammar->nsymbols,1);
    if (!seen) { fputs("cesta: out of memory\n",stderr); exit(2); }
    bool ok = extension_sort_visit(profile,atom,sort,accepts,context,seen);
    free(seen); return ok;
}
static bool position_admits(const CeViewProfile *profile, Position position, const CeAtom *atom,
                            CeViewExtensionAccepts accepts, void *context) {
    const CeGrammar *g = profile->grammar;
    if (position.kind == POSITION_NONE) return false;
    if (position.kind == POSITION_ANY) return true;
    if (position.kind == POSITION_WORD) return atom->kind == CE_ATOM_SYMBOL && ce_equal(atom->text,position.word);
    const AtomKeys *keys = &profile->keys->full[g->symbols[position.sort].sort];
    if (position.kind == POSITION_FIRST) {
        if (atom->kind == CE_ATOM_SYMBOL) return leafset_admits(g,&keys->heads,atom);
        return keys->other_head;
    }
    int token = token_sort(g,position.sort);
    if (token >= 0) {
        CeViewRule leaf = {.shape=CE_VIEW_LEXICAL,.arity=1,.arguments=&token,.argument_parents=&position.parent};
        return token_accepts(g,&leaf,atom,0);
    }
    return keys_admit(g,keys,atom) || extension_sort_admits(profile,atom,position.sort,accepts,context);
}
static bool rule_admits(const CeViewProfile *profile, const CeViewRule *rule, const CeAtom *atom,
                        CeViewExtensionAccepts accepts, void *context) {
    for (size_t at = 0; at < atom->len; ++at)
        if (!position_admits(profile,position_at(profile->grammar,rule,at,atom->len),atom->items[at],accepts,context)) return false;
    return true;
}
static size_t list_shortest(const CeGrammar *g, const CeViewRule *rule) {
    size_t prefix = rule->shape == CE_VIEW_FORM ? 1 : 0;
    if (rule->splice) return prefix+rule->nfields-1+list_minimum(g,rule->arguments[rule->arity-1]);
    return prefix+rule->nfields;
}
static size_t list_longest(const CeViewRule *rule) {
    return rule->splice || rule->dotted ? SIZE_MAX : (rule->shape == CE_VIEW_FORM ? 1 : 0)+rule->nfields;
}
static bool lengths_meet(const CeGrammar *g, const CeViewRule *first, const CeViewRule *second) {
    if (first->shape == CE_VIEW_FORM && second->shape == CE_VIEW_FORM && !ce_equal(first->head,second->head)) return false;
    size_t low = list_shortest(g,first) > list_shortest(g,second) ? list_shortest(g,first) : list_shortest(g,second);
    size_t high = list_longest(first) < list_longest(second) ? list_longest(first) : list_longest(second);
    return low <= high;
}
/* Two list rules overlap when, at some common length, every item position can
   hold one atom under both. Past both fixed parts the positions repeat, so the
   lengths up to one beyond the longer fixed part decide. */
static bool list_overlap(CeViewProfile *profile, const CeViewRule *first, const CeViewRule *second) {
    const CeGrammar *g = profile->grammar;
    if (!lengths_meet(g,first,second)) return false;
    size_t low = list_shortest(g,first) > list_shortest(g,second) ? list_shortest(g,first) : list_shortest(g,second);
    size_t high = list_longest(first) < list_longest(second) ? list_longest(first) : list_longest(second);
    size_t fixed_first = (first->shape == CE_VIEW_FORM ? 1 : 0)+first->nfields;
    size_t fixed_second = (second->shape == CE_VIEW_FORM ? 1 : 0)+second->nfields;
    size_t bound = (fixed_first > fixed_second ? fixed_first : fixed_second)+1;
    if (bound < low) bound = low;
    for (size_t n = low; n <= high && n <= bound; ++n) {
        bool every = true;
        for (size_t at = 0; at < n && every; ++at) {
            AtomKeys a, b;
            position_keys(profile,position_at(g,first,at,n),&a);
            position_keys(profile,position_at(g,second,at,n),&b);
            every = keys_overlap(g,&a,&b);
        }
        if (every) return true;
    }
    return false;
}
static void rule_keys(CeViewProfile *profile, const CeViewRule *rule, AtomKeys *out) {
    const CeGrammar *g = profile->grammar; CeArena *arena = &profile->arena;
    memset(out,0,sizeof(*out));
    if (rule->shape == CE_VIEW_PASS) { *out = profile->keys->rules[g->symbols[rule->arguments[0]].sort]; return; }
    if (rule->shape == CE_VIEW_WORD || rule->shape == CE_VIEW_PREFIX) {
        leaf_add(arena,&out->symbols,rule);
        out->text = spelled_as_text(g,rule);
    }
    else if (rule->shape == CE_VIEW_LEXICAL) {
        if (ce_token_symbol_representation(g,token_sort(g,rule->arguments[0]))) {
            leaf_add(arena,&out->symbols,rule);
            leaf_add(arena,&out->heads,synthetic_leaf(profile,CE_VIEW_WORD,ce_cstr(arena,"c:Symbol"),-1,-1));
        } else out->text = true;
    } else if (rule->shape == CE_VIEW_EMPTY) out->empty = true;
    else if (rule->shape == CE_VIEW_FORM) leaf_add(arena,&out->heads,synthetic_leaf(profile,CE_VIEW_WORD,rule->head,-1,-1));
    else if (rule->shape == CE_VIEW_TUPLE) {
        if (!list_shortest(g,rule)) out->empty = true;
        AtomKeys first; position_keys(profile,position_at(g,rule,0,0),&first);
        leaves_union(arena,&out->heads,&first.symbols);
        out->other_head = first.text || first.heads.any || first.heads.len || first.other_head || first.empty;
    }
}
/* Key sets from rules alone, then with each sort's exact c: constructors:
   an exact atom is read at the sort it is written for, never through an
   unheaded rule. Sets only grow, so the iteration reaches a fixed point. */
static void dispatch_keys(CeViewProfile *profile) {
    const CeGrammar *g = profile->grammar; CeArena *arena = &profile->arena;
    struct CeViewKeys *keys = profile->keys = ce_alloc(arena,sizeof(*keys));
    keys->rules = ce_alloc(arena,g->nsymbols*sizeof(*keys->rules));
    keys->full = ce_alloc(arena,g->nsymbols*sizeof(*keys->full));
    for (size_t i = 0; i < g->nproductions; ++i) {
        const CeProduction *p = &g->productions[i];
        if (p->internal || ce_is(p->label,"_") || g->symbols[p->lhs].erased || g->symbols[p->lhs].list_element >= 0) continue;
        unsigned char *bytes = ce_alloc(arena,p->label.len+3);
        memcpy(bytes,"c:",2); memcpy(bytes+2,p->label.data,p->label.len);
        leaf_add(arena,&keys->full[g->symbols[p->lhs].sort].heads,
            synthetic_leaf(profile,CE_VIEW_WORD,(CeText){bytes,p->label.len+2},-1,-1));
    }
    for (size_t i = 0; i < g->nsymbols; ++i) {
        if (g->symbols[i].kind == CE_LITERAL || token_sort(g,(int)i) < 0) continue;
        AtomKeys direct; sort_keys(profile,(int)i,-1,&direct); keys_union(arena,&keys->full[g->symbols[i].sort],&direct);
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < g->nsymbols; ++i)
            if (g->symbols[i].kind != CE_LITERAL && g->symbols[i].list_element >= 0) {
                AtomKeys lists; list_keys(profile,(int)i,&lists);
                changed |= keys_union(arena,&keys->full[g->symbols[i].sort],&lists);
            }
        for (size_t i = 0; i < profile->len; ++i) {
            AtomKeys top; rule_keys(profile,&profile->rules[i],&top);
            changed |= keys_union(arena,&keys->rules[g->symbols[profile->rules[i].result].sort],&top);
        }
        for (size_t i = 0; i < g->nsymbols; ++i)
            if (g->symbols[i].kind != CE_LITERAL && g->symbols[i].sort == (int)i)
                changed |= keys_union(arena,&keys->full[i],&keys->rules[i]);
    }
}

static bool fingerprints(CeViewProfile *profile, CeError *error);

static bool pass_overlaps_leaf(const CeViewProfile *profile, int sort,
                               const CeViewRule *leaf, unsigned char *seen) {
    const CeGrammar *g = profile->grammar;
    int normalized = g->symbols[sort].sort;
    if (seen[normalized]) return false;
    seen[normalized] = 1;
    int token = token_sort(g,sort);
    if (token >= 0) {
        CeViewRule token_rule = {.arity=1,.arguments=&token};
        if (leaf->shape == CE_VIEW_PREFIX && !leaf->head.len)
            return ce_token_symbol_representation(g,token) ? token == token_sort(g,leaf->arguments[0]) : spelled_as_text(g,leaf);
        return leaf->shape == CE_VIEW_PREFIX ? prefix_overlaps_token(g,leaf,&token_rule) :
            token == token_sort(g,leaf->arguments[0]);
    }
    /* A raw list could contain the identifier escape. Refuse that overlap
       conservatively rather than choosing an interpretation by priority. */
    if (g->symbols[sort].list_element >= 0)
        return leaf->shape != CE_VIEW_PREFIX && ce_token_symbol_representation(g,leaf->arguments[0]);
    for (size_t i = 0; i < profile->len; ++i) {
        const CeViewRule *rule = &profile->rules[i];
        if (!same_sort(g,rule->result,sort)) continue;
        if (rule->shape == CE_VIEW_LEXICAL || rule->shape == CE_VIEW_PREFIX) {
            CeViewRule at_result = *rule; at_result.result = leaf->result;
            if (dispatch_overlaps(g,&at_result,leaf)) return true;
        } else if (rule->shape == CE_VIEW_WORD) {
            CeAtom atom = {.kind=CE_ATOM_SYMBOL,.text=rule->head};
            if (symbol_admitted(g,leaf,&atom)) return true;
        } else if (rule->shape == CE_VIEW_PASS &&
                   pass_overlaps_leaf(profile,rule->arguments[0],leaf,seen)) return true;
    }
    return false;
}

static bool pass_cycle(const CeViewProfile *profile, int symbol, unsigned char *seen, unsigned char *active) {
    int sort = profile->grammar->symbols[symbol].sort;
    if (active[sort]) return true;
    if (seen[sort]) return false;
    seen[sort] = active[sort] = 1;
    for (size_t i = 0; i < profile->len; ++i)
        if (profile->rules[i].shape == CE_VIEW_PASS && same_sort(profile->grammar,profile->rules[i].result,symbol) &&
            pass_cycle(profile,profile->rules[i].arguments[0],seen,active)) return true;
    active[sort] = 0; return false;
}

bool ce_view_compile(CeViewProfile *profile, const CeGrammar *ast, CeCatalog *catalog,
                     const CePresentation *source, const char *name, unsigned version, CeError *error) {
    profile->grammar = ast; profile->name = ce_cstr(&profile->arena,name); profile->version = version;
    CeGrammar surface = {.zero_width_literals = true}; FILE *stream = tmpfile(); bool ok = false;
    if (!stream) return ce_error(error,0,"cannot allocate surface-signature stream");
    CeLazyLexer lexer = {0}; bool printed = true;
    for (size_t i = 0; i < source->len && printed; ++i) {
        const CeTree *tree = source->definitions[i].tree;
        printed = ce_tree_print_shared(catalog->syntax,&lexer,tree,tree->symbol,stream,error);
        fputs(";\n",stream);
    }
    ce_lazy_lexer_free(&lexer);
    if (!printed) goto done;
    long size = ftell(stream);
    if (size < 0) { ce_error(error,0,"cannot measure surface signature"); goto done; }
    CeText bytes = {ce_alloc(&profile->arena,(size_t)size+1),(size_t)size};
    rewind(stream);
    if (fread(bytes.data,1,bytes.len,stream) != bytes.len || !ce_grammar_read(&surface,bytes,error)) goto done;
    /* Categories are holes in the AST signature, so their admission is
       resolved there instead of compiling a second source-language parser. */
    for (size_t i = 0; i < surface.nproductions; ++i) {
        const CeProduction *p = &surface.productions[i];
        if (p->internal || ce_is(p->label,"_") || surface.symbols[p->lhs].list_element >= 0) continue;
        CeViewRule rule = {.name=ce_text(&profile->arena,p->label.data,p->label.len),.offset=p->offset};
        if (!shape(profile,&surface,p,&rule,error)) goto done;
        if (rule.shape == CE_VIEW_FORM || rule.shape == CE_VIEW_WORD || (rule.shape == CE_VIEW_PREFIX && rule.head.len)) {
            CeAtom *head; CeError invalid = {{0},0};
            if (!ce_atom_read(&profile->arena,rule.head,&head,&invalid) || head->kind != CE_ATOM_SYMBOL ||
                !ce_equal(head->text,rule.head) || (rule.head.len >= 2 && rule.head.data[0] == 'c' && rule.head.data[1] == ':')) {
                ce_error(error,p->offset,"presentation head must be an unreserved atom symbol"); goto done;
            }
        }
        for (size_t j = 0; rule.fields && j < rule.nfields; ++j) if (rule.fields[j].slot == SIZE_MAX) {
            CeAtom *word; CeError invalid = {{0},0}; CeText spelling = rule.fields[j].word;
            if (!ce_atom_read(&profile->arena,spelling,&word,&invalid) || word->kind != CE_ATOM_SYMBOL ||
                !ce_equal(word->text,spelling)) {
                ce_error(error,p->offset,"fixed presentation field must be an atom symbol"); goto done;
            }
            rule.fields[j].word = ce_text(&profile->arena,spelling.data,spelling.len);
        }
        CeText *variables = ce_alloc(&profile->arena,rule.arity*sizeof(*variables));
        const CeTree *interpretation = NULL;
        for (size_t j = 0; j < source->nlaws; ++j) {
            const CeTree *law = source->laws[j].tree;
            if (ce_tree_label(catalog->syntax,law,"RDecl")) law = law->children[1];
            if (!ce_tree_label(catalog->syntax,law,"EquationImpl") && !ce_tree_label(catalog->syntax,law,"RewriteBase")) {
                ce_error(error,law->offset,"presentation interpretation requires an unconditional equation or rewrite"); goto done;
            }
            const CeTree *left = law->children[0];
            if (!ce_tree_label(catalog->syntax,left,"ASTSExp")) { ce_error(error,left->offset,"interpretation source requires a surface constructor"); goto done; }
            if (!ce_equal(source_name(catalog->syntax,left->children[0],&profile->arena),rule.name)) continue;
            if (interpretation) { ce_error(error,left->offset,"duplicate interpretation for one surface constructor"); goto done; }
            const CeTree *args = left->children[1];
            if (args->len != rule.arity) { ce_error(error,left->offset,"interpretation source has the wrong arity"); goto done; }
            for (size_t k = 0; k < rule.arity; ++k) {
                if (!ce_tree_label(catalog->syntax,args->children[k],"ASTVar")) { ce_error(error,args->children[k]->offset,"surface interpretation holes must be variables"); goto done; }
                variables[k] = source_name(catalog->syntax,args->children[k]->children[0],&profile->arena);
                for (size_t before = 0; before < k; ++before)
                    if (ce_equal(variables[k],variables[before])) { ce_error(error,args->children[k]->offset,"duplicate surface hole"); goto done; }
            }
            interpretation = law->children[1];
        }
        if (interpretation) {
            unsigned *uses = ce_alloc(&profile->arena,rule.arity*sizeof(*uses));
            rule.body = template_source(profile,catalog->syntax,interpretation,rule.result,-1,variables,&rule,uses,error);
            if (!rule.body) goto done;
            for (size_t k = 0; k < rule.arity; ++k)
                if (uses[k] != 1) { ce_error(error,interpretation->offset,"presentation interpretation must use every hole exactly once"); goto done; }
            if (rule.body->kind == CE_VIEW_SLOT) { ce_error(error,interpretation->offset,"view interpretation must consume an AST constructor or list"); goto done; }
        } else {
            int production = production_named(ast,rule.name);
            if (production < 0) { ce_error(error,p->offset,"surface constructor requires an explicit AST interpretation"); goto done; }
            const CeProduction *target = &ast->productions[production];
            if (!same_sort(ast,target->lhs,rule.result) || fields(ast,target,NULL) != rule.arity) {
                ce_error(error,p->offset,"constructor-by-label interpretation has the wrong sort or arity"); goto done;
            }
            int *args = ce_alloc(&profile->arena,rule.arity*sizeof(*args)); fields(ast,target,args);
            rule.body = template_new(profile,CE_VIEW_NODE,rule.arity);
            rule.body->symbol = target->lhs; rule.body->production = production;
            for (size_t k = 0; k < rule.arity; ++k) {
                if (!same_sort(ast,args[k],rule.arguments[k])) { ce_error(error,p->offset,"constructor-by-label argument has the wrong sort"); goto done; }
                rule.body->children[k] = template_new(profile,CE_VIEW_SLOT,0);
                rule.body->children[k]->slot = k; rule.body->children[k]->symbol = args[k];
                rule.argument_parents[k] = production;
            }
        }
        rule.head = ce_text(&profile->arena,rule.head.data,rule.head.len);
        profile->rules = ce_resize(profile->rules,profile->len+1,sizeof(*profile->rules)); profile->rules[profile->len++] = rule;
    }
    /* A misspelled interpretation must not vanish from the compiled profile. */
    for (size_t i = 0; i < source->nlaws; ++i) {
        const CeTree *law = source->laws[i].tree;
        if (ce_tree_label(catalog->syntax,law,"RDecl")) law = law->children[1];
        if ((!ce_tree_label(catalog->syntax,law,"EquationImpl") && !ce_tree_label(catalog->syntax,law,"RewriteBase")) ||
            !ce_tree_label(catalog->syntax,law->children[0],"ASTSExp")) {
            ce_error(error,law->offset,"presentation interpretation requires a surface-constructor equation or rewrite"); goto done;
        }
        CeText label = source_name(catalog->syntax,law->children[0]->children[0],&profile->arena);
        bool found = false;
        for (size_t j = 0; j < profile->len; ++j) found |= ce_equal(label,profile->rules[j].name);
        if (!found) { ce_error(error,law->offset,"interpretation names an undeclared surface constructor"); goto done; }
    }
    unsigned char *seen = ce_alloc(&profile->arena,ast->nsymbols);
    unsigned char *active = ce_alloc(&profile->arena,ast->nsymbols);
    for (size_t i = 0; i < ast->nsymbols; ++i) if (ast->symbols[i].kind != CE_LITERAL &&
        pass_cycle(profile,(int)i,seen,active)) { ce_error(error,0,"cyclic unheaded presentation dispatch"); goto done; }
    for (size_t i = 0; i < profile->len; ++i)
        if (list_shape(&profile->rules[i])) profile->rules[i].shortest = list_shortest(ast,&profile->rules[i]);
    /* Unheaded rules are defaults behind every other rule of their sort; two of
       them must accept disjoint atoms. List rules whose lengths meet keep their
       disjoint first-atom keys checked at dispatch. */
    dispatch_keys(profile);
    for (size_t i = 0; i < profile->len; ++i) for (size_t j = 0; j < i; ++j) {
        CeViewRule *rule = &profile->rules[i], *other = &profile->rules[j];
        if (!same_sort(ast,rule->result,other->result)) continue;
        bool overlap;
        if (rule->shape == CE_VIEW_PASS && other->shape == CE_VIEW_PASS)
            overlap = keys_overlap(ast,&profile->keys->rules[ast->symbols[rule->arguments[0]].sort],
                &profile->keys->rules[ast->symbols[other->arguments[0]].sort]);
        else if (list_shape(rule) && list_shape(other)) {
            overlap = list_overlap(profile,rule,other);
            if (!overlap && lengths_meet(ast,rule,other)) rule->admit = other->admit = true;
        } else overlap = dispatch_overlaps(ast,rule,other);
        if (overlap) {
            ce_error(error,rule->offset,"overlapping sort/head/arity presentation dispatch: %.*s and %.*s",
                (int)other->name.len,other->name.data,(int)rule->name.len,rule->name.data);
            goto done;
        }
    }
    /* An unheaded default receives the lists a tuple does not admit, and the
       forms whose head its own sort also uses. */
    for (size_t i = 0; i < profile->len; ++i) for (size_t j = 0; j < profile->len; ++j) {
        CeViewRule *rule = &profile->rules[i]; const CeViewRule *pass = &profile->rules[j];
        if (rule->admit || !list_shape(rule) || pass->shape != CE_VIEW_PASS || !same_sort(ast,rule->result,pass->result)) continue;
        CeAtom head = {.kind=CE_ATOM_SYMBOL,.text=rule->head};
        rule->admit = rule->shape == CE_VIEW_TUPLE ||
            leafset_admits(ast,&profile->keys->rules[ast->symbols[pass->arguments[0]].sort].heads,&head);
    }
    for (size_t i = 0; i < profile->len; ++i) if (profile->rules[i].shape == CE_VIEW_LEXICAL ||
        profile->rules[i].shape == CE_VIEW_PREFIX) {
        const CeViewRule *leaf = &profile->rules[i];
        for (size_t j = 0; j < profile->len; ++j) {
            const CeViewRule *rule = &profile->rules[j];
            if (!same_sort(ast,rule->result,leaf->result)) continue;
            if (rule->shape == CE_VIEW_WORD) {
                CeAtom atom = {.kind=CE_ATOM_SYMBOL,.text=rule->head};
                if (symbol_admitted(ast,leaf,&atom)) {
                    ce_error(error,rule->offset,"word overlaps lexical presentation dispatch"); goto done;
                }
            } else if (rule->shape == CE_VIEW_PASS) {
                memset(seen,0,ast->nsymbols);
                if (pass_overlaps_leaf(profile,rule->arguments[0],leaf,seen)) {
                    ce_error(error,rule->offset,"unheaded rule overlaps lexical presentation dispatch"); goto done;
                }
            }
        }
    }
    ok = fingerprints(profile,error);
done:
    fclose(stream); ce_grammar_free(&surface); return ok;
}

typedef struct { const CeAtom *atom; const CeTree *tree; int sort; } CheckedSubtree;
typedef struct { CheckedSubtree *entries; size_t capacity, len; } ViewCache;
typedef struct {
    const CeViewProfile *profile;
    CeViewStats *stats;
    const ViewCache *cache;
    CeViewExtension extension;
    CeViewExtensionAccepts accepts;
    void *extension_context;
} Expansion;

static size_t subtree_hash(const CeAtom *atom, int sort) {
    uintptr_t value = (uintptr_t)atom >> 4;
    value ^= (uintptr_t)(unsigned)sort*UINT64_C(0x9e3779b97f4a7c15);
    value ^= value >> 23; return (size_t)value;
}
static const CeTree *cached_subtree(const ViewCache *cache, const CeAtom *atom, int sort) {
    if (!cache || !cache->capacity) return NULL;
    size_t at = subtree_hash(atom,sort)&(cache->capacity-1);
    while (cache->entries[at].atom) {
        const CheckedSubtree *entry = &cache->entries[at];
        if (entry->atom == atom && entry->sort == sort) return entry->tree;
        at = (at+1)&(cache->capacity-1);
    }
    return NULL;
}
static void cache_subtree(ViewCache *cache, const CeAtom *atom, int sort, const CeTree *tree) {
    /* Token admission depends on its constructor parent. Only checked
       constructor subtrees are reusable without an external lexical parent. */
    if (tree->production < 0) return;
    if (!cache->capacity || cache->len*2 >= cache->capacity) {
        size_t old_capacity = cache->capacity; CheckedSubtree *old = cache->entries;
        cache->capacity = old_capacity ? old_capacity*2 : 256;
        cache->entries = calloc(cache->capacity,sizeof(*cache->entries));
        if (!cache->entries) { fputs("cesta: out of memory\n",stderr); exit(2); }
        for (size_t i = 0; i < old_capacity; ++i) if (old[i].atom) {
            size_t at = subtree_hash(old[i].atom,old[i].sort)&(cache->capacity-1);
            while (cache->entries[at].atom) at = (at+1)&(cache->capacity-1);
            cache->entries[at] = old[i];
        }
        free(old);
    }
    size_t at = subtree_hash(atom,sort)&(cache->capacity-1);
    while (cache->entries[at].atom) at = (at+1)&(cache->capacity-1);
    cache->entries[at] = (CheckedSubtree){atom,tree,sort}; ++cache->len;
}

static void count_node(Expansion *expansion, int production, bool shortened) {
    CeViewStats *stats = expansion->stats;
    if (!stats) return;
    if (production == -1) { ++stats->tokens; return; }
    if (production == -2) { ++stats->lists; stats->shortened_lists += shortened; return; }
    ++stats->constructors; stats->shortened += shortened;
    ++stats->by_constructor[production]; stats->short_by_constructor[production] += shortened;
}

static bool expand_atom(void *context, const CeAtom *atom, int sort, int parent,
                        CeArena *arena, CeTree **out, CeError *error, size_t depth);

static bool instantiate(Expansion *expansion, const CeViewTemplate *body, CeTree **args,
                         CeArena *arena, size_t offset, CeTree **out, CeError *error, size_t depth) {
    if (depth > 4096) return ce_error(error,offset,"interpretation nesting exceeds stack depth");
    if (body->kind == CE_VIEW_SLOT) { *out = args[body->slot]; return true; }
    CeTree *tree = ce_alloc(arena,sizeof(*tree));
    tree->symbol = body->symbol; tree->production = body->kind == CE_VIEW_LIST ? -2 : body->production;
    tree->len = body->len; tree->offset = offset;
    tree->children = ce_alloc(arena,tree->len*sizeof(*tree->children));
    for (size_t i = 0; i < tree->len; ++i)
        if (!instantiate(expansion,body->children[i],args,arena,offset,&tree->children[i],error,depth+1)) return false;
    if (tree->production == -2) {
        /* Length admission depends on the list schema, not on serializing its
           elements. Elements have already been admitted at their slot sorts. */
        tree->symbol = ce_tree_list_symbol(expansion->profile->grammar,tree->symbol,tree->len);
        if (tree->symbol < 0) return ce_error(error,offset,"list length is not admitted by the source grammar");
    }
    count_node(expansion,tree->production,true); *out = tree; return true;
}

static bool headed(const CeAtom *atom, CeText head) {
    return atom->kind == CE_ATOM_LIST && atom->len && atom->items[0]->kind == CE_ATOM_SYMBOL && ce_equal(atom->items[0]->text,head);
}
static bool is_exact(const CeAtom *atom) {
    if (atom->kind != CE_ATOM_LIST || !atom->len || atom->items[0]->kind != CE_ATOM_SYMBOL) return false;
    CeText head = atom->items[0]->text;
    return head.len > 2 && head.data[0] == 'c' && head.data[1] == ':';
}
static bool symbol_escape(const CeAtom *atom) {
    return atom->kind == CE_ATOM_LIST && atom->len && atom->items[0]->kind == CE_ATOM_SYMBOL &&
        ce_is(atom->items[0]->text,"c:Symbol");
}
static size_t surface_slot(const CeViewRule *rule, size_t position) {
    return rule->fields ? rule->fields[position].slot : position;
}
/* The item holding field i of an n-item form; fields after a splice close it. */
static size_t field_item(const CeViewRule *rule, size_t i, size_t n) {
    return rule->splice && i+rule->suffix >= rule->nfields ? n-(rule->nfields-i) : i;
}
static bool form_matches(const CeViewRule *rule, const CeAtom *atom) {
    size_t prefix = rule->shape == CE_VIEW_FORM ? 1 : 0;
    if (atom->kind != CE_ATOM_LIST || atom->len < prefix) return false;
    size_t n = atom->len-prefix;
    if (rule->splice || rule->dotted ? atom->len < rule->shortest : n != rule->nfields) return false;
    for (size_t i = 0; rule->fields && i < rule->nfields; ++i) if (rule->fields[i].slot == SIZE_MAX) {
        const CeAtom *field = atom->items[field_item(rule,i,n)+prefix];
        if (field->kind != CE_ATOM_SYMBOL || !ce_equal(field->text,rule->fields[i].word)) return false;
    }
    return true;
}

static bool expand_atom(void *context, const CeAtom *atom, int sort, int parent,
                        CeArena *arena, CeTree **out, CeError *error, size_t depth) {
    Expansion *expansion = context; const CeViewProfile *profile = expansion->profile;
    const CeGrammar *g = profile->grammar;
    if (depth > 4096) return ce_error(error,atom->offset,"presentation nesting exceeds decoder stack depth");
    if (expansion->extension && !atom->spliced) {
        unsigned result = expansion->extension(expansion->extension_context,atom,sort,parent,
            arena,out,expand_atom,context,error,depth);
        if (result == CE_EXTENSION_ERROR) return false;
        if (result == CE_EXTENSION_EXPANDED) {
            if (!*out || (*out)->symbol < 0 || (size_t)(*out)->symbol >= g->nsymbols ||
                !same_sort(g,sort,(*out)->symbol))
                return ce_error(error,atom->offset,"extension returned the wrong grammar sort");
            return true;
        }
        if (result != CE_EXTENSION_UNHANDLED) return ce_error(error,atom->offset,"invalid extension result");
    }
    const CeTree *checked = cached_subtree(expansion->cache,atom,sort);
    if (checked) { *out = (CeTree *)checked; return true; }
    int actual = sort;
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (same_sort(g,sort,(int)i) && (g->symbols[i].kind == CE_TOKEN || g->symbols[i].list_element >= 0)) { actual = (int)i; break; }
    if (g->symbols[actual].kind == CE_TOKEN || (is_exact(atom) && !symbol_escape(atom))) {
        if (!ce_tree_decode_atom(g,atom,sort,parent,arena,expand_atom,context,out,error,depth)) return false;
        count_node(expansion,(*out)->production,false); return true;
    }
    if (g->symbols[actual].list_element >= 0) {
        if (atom->kind != CE_ATOM_LIST) return ce_error(error,atom->offset,"presentation list sort requires structural list");
        CeAtom exact = *atom; exact.len = atom->len+1;
        exact.items = ce_alloc(arena,exact.len*sizeof(*exact.items));
        exact.items[0] = ce_atom_new(arena,CE_ATOM_SYMBOL,ce_cstr(arena,"c:List"),0);
        if (atom->len) memcpy(exact.items+1,atom->items,atom->len*sizeof(*atom->items));
        if (!ce_tree_decode_atom(g,&exact,sort,parent,arena,expand_atom,context,out,error,depth)) return false;
        count_node(expansion,-2,true); return true;
    }
    const CeViewRule *selected = NULL, *pass = NULL; bool known_head = false; size_t passes = 0;
    for (size_t i = 0; i < profile->len; ++i) {
        const CeViewRule *rule = &profile->rules[i];
        if (!same_sort(g,rule->result,sort)) continue;
        if (rule->shape == CE_VIEW_PASS) { pass = rule; ++passes; continue; }
        bool matches = false;
        if (rule->shape == CE_VIEW_PREFIX) {
            CeArena trial = {0}; CeAtom *argument = prefix_argument(g,rule,atom,&trial);
            matches = argument && lexical_accepts(profile,rule,argument,depth+1);
            ce_arena_free(&trial);
        } else if (rule->shape == CE_VIEW_LEXICAL && (atom->kind != CE_ATOM_LIST ||
            symbol_escape(atom))) matches = lexical_accepts(profile,rule,atom,depth+1);
        else if (rule->shape == CE_VIEW_WORD) matches = atom->kind == CE_ATOM_SYMBOL && ce_equal(atom->text,rule->head);
        else if (rule->shape == CE_VIEW_EMPTY) matches = atom->kind == CE_ATOM_LIST && !atom->len;
        else if (rule->shape == CE_VIEW_TUPLE && atom->kind == CE_ATOM_LIST)
            matches = form_matches(rule,atom) && (!rule->admit || rule_admits(profile,rule,atom,expansion->accepts,expansion->extension_context));
        else if (headed(atom,rule->head)) {
            known_head = true;
            matches = form_matches(rule,atom) && (!rule->admit || rule_admits(profile,rule,atom,expansion->accepts,expansion->extension_context));
        }
        if (matches) {
            if (selected) return ce_error(error,atom->offset,"ambiguous presentation dispatch: %.*s and %.*s",
                (int)selected->name.len,selected->name.data,(int)rule->name.len,rule->name.data);
            selected = rule;
        }
    }
    if (!selected && known_head) return ce_error(error,atom->offset,"wrong arity or fixed marker for presentation head %.*s at sort %.*s",
        (int)atom->items[0]->text.len,atom->items[0]->text.data,(int)g->symbols[g->symbols[sort].sort].name.len,g->symbols[g->symbols[sort].sort].name.data);
    if (!selected && passes > 1) for (size_t i = 0; i < profile->len; ++i) {
        const CeViewRule *rule = &profile->rules[i];
        if (rule->shape != CE_VIEW_PASS || !same_sort(g,rule->result,sort) ||
            (!keys_admit(g,&profile->keys->rules[g->symbols[rule->arguments[0]].sort],atom) &&
             !extension_sort_admits(profile,atom,rule->arguments[0],expansion->accepts,expansion->extension_context))) continue;
        if (selected) return ce_error(error,atom->offset,"ambiguous unheaded presentation dispatch");
        selected = rule;
    } else if (!selected) selected = pass;
    if (!selected) return ce_error(error,atom->offset,"no presentation form of sort %.*s accepts this atom",
        (int)g->symbols[g->symbols[sort].sort].name.len,g->symbols[g->symbols[sort].sort].name.data);
    CeTree **args = ce_alloc(arena,selected->arity*sizeof(*args));
    size_t prefix = selected->shape == CE_VIEW_FORM ? 1 : 0;
    for (size_t i = 0; i < selected->arity; ++i) {
        const CeAtom *argument;
        CeAtom list = {.kind=CE_ATOM_LIST,.offset=atom->offset};
        if (selected->shape == CE_VIEW_PREFIX) argument = prefix_argument(g,selected,atom,arena);
        else if (selected->shape == CE_VIEW_PASS || selected->shape == CE_VIEW_LEXICAL) argument = atom;
        else if ((selected->splice || (selected->dotted && atom->len-prefix > selected->nfields)) && i+1 == selected->arity) {
            list.len = atom->len-prefix-selected->nfields+1;
            list.items = atom->items+prefix+selected->nfields-1-selected->suffix;
            list.spliced = selected->splice; argument = &list;
        } else {
            size_t position = 0;
            while (surface_slot(selected,position) != i) ++position;
            argument = atom->items[position+prefix];
        }
        if (!expand_atom(context,argument,selected->arguments[i],selected->argument_parents[i],arena,&args[i],error,depth+1)) return false;
    }
    return instantiate(expansion,selected->body,args,arena,atom->offset,out,error,depth+1);
}

bool ce_view_expand(const CeViewProfile *profile, const CeAtom *surface, int sort,
                    CeArena *arena, CeTree **out, CeViewStats *stats, CeError *error) {
    if (stats && stats->capacity != profile->grammar->nproductions) {
        ce_view_stats_free(stats); stats->capacity = profile->grammar->nproductions;
        stats->by_constructor = calloc(stats->capacity,sizeof(*stats->by_constructor));
        stats->short_by_constructor = calloc(stats->capacity,sizeof(*stats->short_by_constructor));
        if (!stats->by_constructor || !stats->short_by_constructor) { fputs("cesta: out of memory\n",stderr); exit(2); }
    }
    Expansion expansion = {.profile=profile,.stats=stats};
    return expand_atom(&expansion,surface,sort,-1,arena,out,error,0);
}

bool ce_view_expand_open(const CeViewProfile *profile, const CeAtom *surface, int sort,
                         CeArena *arena, CeTree **out, CeViewExtension extension,
                         CeViewExtensionAccepts accepts, void *context, CeError *error) {
    Expansion expansion = {.profile=profile,.extension=extension,.accepts=accepts,.extension_context=context};
    return expand_atom(&expansion,surface,sort,-1,arena,out,error,0);
}

static bool match(const CeGrammar *g, const CeViewTemplate *body, const CeTree *tree, const CeTree **args) {
    if (!same_sort(g,body->symbol,tree->symbol)) return false;
    if (body->kind == CE_VIEW_SLOT) { args[body->slot] = tree; return true; }
    if ((body->kind == CE_VIEW_LIST ? -2 : body->production) != tree->production || body->len != tree->len) return false;
    for (size_t i = 0; i < body->len; ++i) if (!match(g,body->children[i],tree->children[i],args)) return false;
    return true;
}

/* A symbol candidate must read back as that one symbol. */
static bool readable_symbol(CeText text) {
    CeArena arena = {0}; CeAtom *atom; CeError error = {{0},0};
    bool ok = ce_atom_read(&arena,text,&atom,&error) && atom->kind == CE_ATOM_SYMBOL && ce_equal(atom->text,text);
    ce_arena_free(&arena); return ok;
}
/* The one atom a token spelling reads as, when printing that atom gives the
   spelling back exactly. */
static CeAtom *spelled_atom(CeText spelling, CeArena *arena) {
    CeAtom *atom; CeError error = {{0},0};
    if (!ce_atom_read(arena,spelling,&atom,&error)) return NULL;
    if (atom->kind == CE_ATOM_SYMBOL) return ce_equal(atom->text,spelling) ? atom : NULL;
    if (atom->kind == CE_ATOM_TEXT) {
        CeArena scratch = {0}; bool exact = ce_equal(ce_quoted(&scratch,atom->text),spelling);
        ce_arena_free(&scratch); return exact ? atom : NULL;
    }
    return NULL;
}
static bool view_tree(const CeViewProfile *profile, const CeTree *tree, int sort,
                      CeArena *arena, CeAtom **out, ViewCache *cache, CeError *error, size_t depth) {
    const CeGrammar *g = profile->grammar;
    if (depth > 4096) return ce_error(error,tree->offset,"view nesting exceeds stack depth");
    for (size_t i = 0; i < profile->len; ++i) {
        const CeViewRule *rule = &profile->rules[i];
        if (!same_sort(g,rule->result,sort)) continue;
        if ((rule->body->kind == CE_VIEW_NODE && rule->body->production != tree->production) ||
            (rule->body->kind == CE_VIEW_LIST && tree->production != -2)) continue;
        const CeTree **captures = ce_alloc(arena,rule->arity*sizeof(*captures));
        if (!match(g,rule->body,tree,captures)) continue;
        CeAtom **arguments = ce_alloc(arena,rule->arity*sizeof(*arguments));
        for (size_t j = 0; j < rule->arity; ++j)
            if (!view_tree(profile,captures[j],rule->arguments[j],arena,&arguments[j],cache,error,depth+1)) return false;
        CeAtom *candidate;
        if (rule->shape == CE_VIEW_PREFIX && !rule->head.len) {
            if (!(candidate = spelled_atom(captures[0]->spelling,arena))) continue;
        } else if (rule->shape == CE_VIEW_PREFIX) {
            CeText spelling = captures[0]->spelling;
            unsigned char *bytes = ce_alloc(arena,rule->head.len+spelling.len+1);
            memcpy(bytes,rule->head.data,rule->head.len); memcpy(bytes+rule->head.len,spelling.data,spelling.len);
            candidate = ce_atom_new(arena,CE_ATOM_SYMBOL,(CeText){bytes,rule->head.len+spelling.len},0);
            if (!readable_symbol(candidate->text)) continue;
        } else if (rule->shape == CE_VIEW_PASS || rule->shape == CE_VIEW_LEXICAL) candidate = arguments[0];
        else if (rule->shape == CE_VIEW_WORD) candidate = ce_atom_new(arena,CE_ATOM_SYMBOL,rule->head,0);
        else if (rule->shape == CE_VIEW_EMPTY) candidate = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},0);
        else {
            CeAtom *last = rule->splice ? arguments[rule->arity-1] : NULL;
            if (last && last->kind != CE_ATOM_LIST) continue;
            /* A dotted tail of several atoms continues the form in place. */
            if (rule->dotted && arguments[rule->arity-1]->kind == CE_ATOM_LIST && arguments[rule->arity-1]->len >= 2)
                last = arguments[rule->arity-1];
            size_t spread = rule->nfields-1-rule->suffix;
            size_t prefix = rule->shape == CE_VIEW_FORM ? 1 : 0;
            size_t n = prefix+rule->nfields-(last ? 1 : 0)+(last ? last->len : 0), at = prefix;
            candidate = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},n);
            if (prefix) candidate->items[0] = ce_atom_new(arena,CE_ATOM_SYMBOL,rule->head,0);
            for (size_t j = 0; j < rule->nfields; ++j) {
                if (last && j == spread) {
                    if (last->len) memcpy(candidate->items+at,last->items,last->len*sizeof(*last->items));
                    at += last->len; continue;
                }
                size_t slot = surface_slot(rule,j);
                candidate->items[at++] = slot == SIZE_MAX ?
                    ce_atom_new(arena,CE_ATOM_SYMBOL,rule->fields[j].word,0) : arguments[slot];
            }
        }
        candidate->offset = tree->offset;
        CeArena checked = {0}; CeTree *expanded; CeError refusal = {{0},0};
        Expansion expansion = {.profile=profile,.cache=cache};
        bool exact = expand_atom(&expansion,candidate,sort,-1,&checked,&expanded,&refusal,0) && ce_tree_equal(g,tree,expanded);
        ce_arena_free(&checked);
        if (exact) { cache_subtree(cache,candidate,sort,tree); *out = candidate; return true; }
    }
    if (tree->production == -1) { *out = ce_tree_to_atom(g,tree,arena); return true; }
    size_t prefix = tree->production == -2 ? 0 : 1;
    CeAtom *fallback = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},tree->len+prefix);
    if (prefix) {
        CeText label = g->productions[tree->production].label;
        unsigned char *bytes = ce_alloc(arena,label.len+3); memcpy(bytes,"c:",2); memcpy(bytes+2,label.data,label.len);
        fallback->items[0] = ce_atom_new(arena,CE_ATOM_SYMBOL,(CeText){bytes,label.len+2},0);
    }
    int *args = ce_alloc(arena,tree->len*sizeof(*args));
    if (tree->production == -2) {
        for (size_t i = 0; i < tree->len; ++i) args[i] = g->symbols[tree->symbol].list_element;
    } else fields(g,&g->productions[tree->production],args);
    for (size_t i = 0; i < tree->len; ++i)
        if (!view_tree(profile,tree->children[i],args[i],arena,&fallback->items[i+prefix],cache,error,depth+1)) return false;
    fallback->offset = tree->offset; *out = fallback; return true;
}

bool ce_view_tree(const CeViewProfile *profile, const CeTree *tree, int sort,
                  CeArena *arena, CeAtom **out, CeError *error) {
    ViewCache cache = {0};
    bool result = view_tree(profile,tree,sort,arena,out,&cache,error,0);
    free(cache.entries); return result;
}

static void json_text(CeText text, FILE *file) {
    fputc('"',file);
    for (size_t i = 0; i < text.len; ++i) {
        unsigned c = text.data[i];
        if (c == '"' || c == '\\') fputc('\\',file);
        if (c < 32) fprintf(file,"\\u%04x",c); else fputc((int)c,file);
    }
    fputc('"',file);
}
static void export_sort(const CeGrammar *g, int symbol, FILE *file) {
    json_text(g->symbols[g->symbols[symbol].sort].name,file);
}
static void export_template(const CeGrammar *g, const CeViewTemplate *body, FILE *file) {
    if (body->kind == CE_VIEW_SLOT) { fprintf(file,"{\"slot\":%zu}",body->slot); return; }
    fputs(body->kind == CE_VIEW_LIST ? "{\"list\":" : "{\"constructor\":",file);
    if (body->kind == CE_VIEW_LIST) export_sort(g,body->symbol,file);
    else json_text(g->productions[body->production].label,file);
    fputs(",\"children\":[",file);
    for (size_t i = 0; i < body->len; ++i) { if (i) fputc(',',file); export_template(g,body->children[i],file); }
    fputs("]}",file);
}
bool ce_view_rules_export(const CeViewProfile *profile, FILE *file, CeError *error) {
    const CeGrammar *g = profile->grammar;
    fputs("{\"format\":\"cesta.presentation-rules.v1\",\"name\":",file); json_text(profile->name,file);
    fprintf(file,",\"version\":%u,\"rules\":[",profile->version);
    for (size_t i = 0; i < profile->len; ++i) {
        const CeViewRule *rule = &profile->rules[i];
        if (i) fputc(',',file);
        fputs("{\"name\":",file); json_text(rule->name,file);
        fputs(",\"result\":",file); export_sort(g,rule->result,file);
        fprintf(file,",\"shape\":%u,\"head\":",rule->shape); json_text(rule->head,file);
        fprintf(file,",\"splice\":%s,",rule->splice ? "true" : "false");
        if (rule->dotted) fputs("\"dotted\":true,",file);
        fputs("\"arguments\":[",file);
        for (size_t j = 0; j < rule->arity; ++j) { if (j) fputc(',',file); export_sort(g,rule->arguments[j],file); }
        fputc(']',file);
        if (rule->fields && rule->nfields != rule->arity) {
            fputs(",\"fields\":[",file);
            for (size_t j = 0; j < rule->nfields; ++j) {
                if (j) fputc(',',file);
                if (rule->fields[j].slot == SIZE_MAX) json_text(rule->fields[j].word,file);
                else fprintf(file,"%zu",rule->fields[j].slot);
            }
            fputc(']',file);
        }
        fputs(",\"body\":",file); export_template(g,rule->body,file); fputc('}',file);
    }
    fputs("],\"lexer\":{\"states\":[",file);
    for (size_t i = 0; i < g->nnfa; ++i) {
        const CeNfaState *state = &g->nfa[i];
        if (i) fputc(',',file);
        fprintf(file,"[%s,%d,%d,[\"%016llx\",\"%016llx\",\"%016llx\",\"%016llx\"]]",
            state->epsilon ? "true" : "false",state->out[0],state->out[1],
            (unsigned long long)state->bytes.word[0],(unsigned long long)state->bytes.word[1],
            (unsigned long long)state->bytes.word[2],(unsigned long long)state->bytes.word[3]);
    }
    fputs("],\"tokens\":[",file);
    for (size_t i = 0; i < g->nlex; ++i) {
        if (i) fputc(',',file);
        fputs("{\"sort\":",file); export_sort(g,g->lex[i].symbol,file);
        fputs(",\"name\":",file); json_text(g->symbols[g->lex[i].symbol].name,file);
        fprintf(file,",\"start\":%d,\"end\":%d}",g->lex[i].start,g->lex[i].end);
    }
    fputs("],\"keywords\":[",file); bool first = true;
    for (size_t i = 0; i < g->nsymbols; ++i) {
        const CeSymbol *symbol = &g->symbols[i];
        if (symbol->kind != CE_LITERAL || symbol->virtual_token || symbol->identifier_spelling) continue;
        if (!first) fputc(',',file);
        first = false; json_text(symbol->name,file);
    }
    fputs("],\"aliases\":[",file);
    for (size_t i = 0; i < g->naliases; ++i) {
        if (i) fputc(',',file);
        fputs("{\"constructor\":",file); json_text(g->aliases[i].production,file);
        fputs(",\"token\":",file); json_text(g->aliases[i].token,file);
        fputs(",\"spelling\":",file); json_text(g->aliases[i].spelling,file); fputc('}',file);
    }
    fputs("]}}\n",file);
    return !ferror(file) || ce_error(error,0,"cannot write presentation rules");
}

static bool stream_fingerprint(FILE *stream, uint64_t *value, CeError *error) {
    if (ferror(stream)) return ce_error(error,0,"cannot render presentation identity");
    rewind(stream); uint64_t hash = UINT64_C(14695981039346656037);
    unsigned char bytes[4096]; size_t n;
    while ((n = fread(bytes,1,sizeof(bytes),stream)))
        for (size_t i = 0; i < n; ++i) hash = (hash ^ bytes[i])*UINT64_C(1099511628211);
    *value = hash;
    return !ferror(stream) || ce_error(error,0,"cannot read presentation identity");
}
static bool fingerprints(CeViewProfile *profile, CeError *error) {
    FILE *stream = tmpfile();
    if (!stream) return ce_error(error,0,"cannot allocate presentation identity stream");
    const CeGrammar *g = profile->grammar;
    bool ok = ce_metadata_export(g,stream,error);
    if (ok) {
        /* Pin token-spelling admission as well as constructor/list sorts. */
        fprintf(stream,"\nc-scope:%u\n",g->c_scope);
        for (size_t i = 0; i < g->nnfa; ++i) {
            const CeNfaState *n = &g->nfa[i];
            fprintf(stream,"%u:%d:%d:%016llx:%016llx:%016llx:%016llx\n",n->epsilon,n->out[0],n->out[1],
                (unsigned long long)n->bytes.word[0],(unsigned long long)n->bytes.word[1],
                (unsigned long long)n->bytes.word[2],(unsigned long long)n->bytes.word[3]);
        }
        for (size_t i = 0; i < g->nlex; ++i)
            fprintf(stream,"token:%d:%d:%d\n",g->lex[i].symbol,g->lex[i].start,g->lex[i].end);
        for (size_t i = 0; i < g->naliases; ++i) {
            ce_quote(stream,g->aliases[i].production); ce_quote(stream,g->aliases[i].token); ce_quote(stream,g->aliases[i].spelling);
        }
        ok = stream_fingerprint(stream,&profile->schema_fingerprint,error);
    }
    fclose(stream); if (!ok) return false;
    stream = tmpfile(); if (!stream) return ce_error(error,0,"cannot allocate rule identity stream");
    ok = ce_view_rules_export(profile,stream,error) && stream_fingerprint(stream,&profile->rules_fingerprint,error);
    fclose(stream); return ok;
}

static CeText profile_key(const CeViewProfile *profile, CeArena *arena) {
    size_t capacity = profile->name.len+24;
    char *bytes = ce_alloc(arena,capacity);
    int len = snprintf(bytes,capacity,"%.*s/%u",(int)profile->name.len,profile->name.data,profile->version);
    return (CeText){(unsigned char *)bytes,(size_t)len};
}
static CeText fingerprint_text(uint64_t value, CeArena *arena) {
    unsigned char *bytes = ce_alloc(arena,17);
    snprintf((char *)bytes,17,"%016llx",(unsigned long long)value);
    return (CeText){bytes,16};
}
CeAtom *ce_view_document(const CeViewProfile *profile, int sort, CeAtom *surface, CeArena *arena) {
    CeAtom *document = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},6);
    document->items[0] = ce_atom_new(arena,CE_ATOM_SYMBOL,ce_cstr(arena,"cesta"),0);
    document->items[1] = ce_atom_new(arena,CE_ATOM_SYMBOL,profile_key(profile,arena),0);
    document->items[2] = ce_atom_new(arena,CE_ATOM_TEXT,fingerprint_text(profile->schema_fingerprint,arena),0);
    document->items[3] = ce_atom_new(arena,CE_ATOM_TEXT,fingerprint_text(profile->rules_fingerprint,arena),0);
    document->items[4] = ce_atom_new(arena,CE_ATOM_SYMBOL,profile->grammar->symbols[profile->grammar->symbols[sort].sort].name,0);
    document->items[5] = surface; return document;
}
bool ce_view_document_payload(const CeViewProfile *profile, int sort, const CeAtom *document,
                              const CeAtom **out, CeError *error) {
    if (document->kind != CE_ATOM_LIST || document->len != 6 || document->items[0]->kind != CE_ATOM_SYMBOL ||
        !ce_is(document->items[0]->text,"cesta")) return ce_error(error,document->offset,"expected versioned Cesta document");
    CeArena arena = {0}; bool ok = false;
    if (document->items[1]->kind != CE_ATOM_SYMBOL || !ce_equal(document->items[1]->text,profile_key(profile,&arena)))
        ce_error(error,document->items[1]->offset,"document presentation or version does not match the declared profile");
    else if (document->items[2]->kind != CE_ATOM_TEXT ||
             !ce_equal(document->items[2]->text,fingerprint_text(profile->schema_fingerprint,&arena)))
        ce_error(error,document->items[2]->offset,"document AST schema or lexical profile fingerprint does not match");
    else if (document->items[3]->kind != CE_ATOM_TEXT ||
             !ce_equal(document->items[3]->text,fingerprint_text(profile->rules_fingerprint,&arena)))
        ce_error(error,document->items[3]->offset,"document interpretation fingerprint does not match");
    else if (document->items[4]->kind != CE_ATOM_SYMBOL ||
             !ce_equal(document->items[4]->text,profile->grammar->symbols[profile->grammar->symbols[sort].sort].name))
        ce_error(error,document->items[4]->offset,"document entry sort does not match");
    else { *out = document->items[5]; ok = true; }
    ce_arena_free(&arena); return ok;
}

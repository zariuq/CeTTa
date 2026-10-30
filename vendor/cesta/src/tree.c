#include "cesta.h"

#include <stdlib.h>
#include <string.h>

bool ce_tree_label(const CeGrammar *g, const CeTree *tree, const char *label) {
    return tree && tree->production >= 0 && ce_is(g->productions[tree->production].label, label);
}

static CeTree *make(CeArena *a, int symbol, int production, size_t len) {
    CeTree *t = ce_alloc(a, sizeof(*t));
    t->symbol = symbol; t->production = production; t->len = len;
    t->children = ce_alloc(a, len * sizeof(*t->children));
    return t;
}

static void prefix_children(const CeForest *f, int id, int *out, size_t *len) {
    if (id < 0) return;
    CeForestNode n = f->nodes[id];
    CePacked p = n.alternatives[0];
    prefix_children(f, p.left, out, len);
    if (p.right >= 0) out[(*len)++] = p.right;
}

static CeTree *extract(const CeGrammar *g, const CeForest *f, const CeTokens *tokens, int id, CeArena *a);

static CeTree *extract_list(const CeGrammar *g, const CeForest *f, const CeTokens *tokens, int id, CeArena *a) {
    typedef struct { int node; bool expand; } Pending;
    size_t pending_capacity = 64;
    Pending *pending = ce_resize(NULL,pending_capacity,sizeof(*pending)); size_t n = 1;
    pending[0] = (Pending){id,true};
    CeTree **children = NULL; size_t len = 0, capacity = 0;
    while (n) {
        Pending item = pending[--n];
        if (!item.expand) {
            if (len == capacity) { capacity = capacity ? capacity*2 : 16; children = ce_resize(children,capacity,sizeof(*children)); }
            children[len++] = extract(g,f,tokens,item.node,a);
            continue;
        }
        CeForestNode node = f->nodes[item.node]; CePacked alt = node.alternatives[0];
        const CeProduction *p = &g->productions[alt.production];
        int *ids = ce_resize(NULL,p->len,sizeof(*ids)); size_t nids = 0;
        prefix_children(f,alt.left,ids,&nids);
        for (size_t i = nids; i; --i) {
            int symbol = p->rhs[i-1];
            if (g->symbols[symbol].kind == CE_LITERAL || g->symbols[symbol].erased) continue;
            bool self = ce_is(p->label,"_") || symbol == p->lhs;
            if (n == pending_capacity) { pending_capacity *= 2; pending = ce_resize(pending,pending_capacity,sizeof(*pending)); }
            pending[n++] = (Pending){ids[i-1],self};
        }
        free(ids);
    }
    CeTree *tree = make(a,f->nodes[id].symbol,-2,len);
    if (len) memcpy(tree->children,children,len*sizeof(*children));
    size_t begin = f->nodes[id].begin;
    tree->offset = begin < tokens->len ? tokens->data[begin].offset : 0;
    free(children); free(pending); return tree;
}

static CeTree *extract(const CeGrammar *g, const CeForest *f, const CeTokens *tokens, int id, CeArena *a) {
    CeForestNode n = f->nodes[id];
    if (g->symbols[n.symbol].kind != CE_NONTERMINAL) {
        CeTree *t = make(a, n.symbol, -1, 0);
        t->spelling = ce_text(a, n.spelling.data, n.spelling.len);
        t->offset = n.begin < tokens->len ? tokens->data[n.begin].offset : 0;
        return t;
    }
    CePacked alternative = n.alternatives[0];
    const CeProduction *p = &g->productions[alternative.production];
    if (g->symbols[p->lhs].list_element >= 0) return extract_list(g,f,tokens,id,a);
    int *ids = ce_alloc(a, p->len * sizeof(int));
    size_t nids = 0;
    prefix_children(f, alternative.left, ids, &nids);
    CeTree **children = ce_alloc(a, p->len * sizeof(*children));
    size_t len = 0;
    for (size_t i = 0; i < nids; ++i)
        if (g->symbols[f->nodes[ids[i]].symbol].kind != CE_LITERAL && !g->symbols[f->nodes[ids[i]].symbol].erased) children[len++] = extract(g, f, tokens, ids[i], a);
    if (ce_is(p->label, "_")) return children[0];
    CeTree *t = make(a, p->lhs, alternative.production, len);
    if (len) memcpy(t->children, children, len*sizeof(*children));
    t->offset = n.begin < tokens->len ? tokens->data[n.begin].offset : 0;
    return t;
}

bool ce_tree_from_forest(const CeGrammar *g, const CeForest *f, const CeTokens *tokens,
                         CeArena *a, CeTree **out, CeError *error) {
    if (ce_forest_count(f, 2) != 1)
        return ce_error(error, 0, "tree extraction requires one parse; supply an explicit ambiguity policy");
    *out = extract(g, f, tokens, f->root, a);
    return true;
}

static bool plain_identifier(CeText text) {
    if (!text.len) return false;
    for (size_t i = 0; i < text.len; ++i) {
        unsigned c = text.data[i];
        if (c != '_' && !(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(i && c >= '0' && c <= '9')) return false;
    }
    return !ce_is(text,"True") && !ce_is(text,"False") && !ce_is(text,"PI") && !ce_is(text,"EXP");
}

bool ce_token_symbol_representation(const CeGrammar *g, int symbol) {
    return ce_is(g->symbols[symbol].name,"Ident") ||
        (g->c_scope && g->symbols[symbol].sort == g->symbols[g->name_token].sort);
}

void ce_tree_encode(const CeGrammar *g, const CeTree *t, FILE *file) {
    if (t->production == CE_TREE_SOURCE) {
        fputs("(c:OpenSource ",file); ce_quote(file,t->spelling); fputc(')',file); return;
    }
    if (t->production == -1) {
        if (ce_token_symbol_representation(g, t->symbol)) {
            if (plain_identifier(t->spelling)) fwrite(t->spelling.data, 1, t->spelling.len, file);
            else { fputs("(c:Symbol ", file); ce_quote(file, t->spelling); fputc(')', file); }
        } else ce_quote(file, t->spelling);
        return;
    }
    if (t->production == -2) fputs("(c:List", file);
    else {
        fputs("(c:", file);
        CeText label = g->productions[t->production].label;
        fwrite(label.data, 1, label.len, file);
    }
    for (size_t i = 0; i < t->len; ++i) { fputc(' ', file); ce_tree_encode(g, t->children[i], file); }
    fputc(')', file);
}

static bool same_sort(const CeGrammar *g, int a, int b) {
    return g->symbols[a].sort == g->symbols[b].sort;
}

static bool advance(CeMetaScanner *s) { return ce_meta_next(s); }
static bool token_admitted(const CeGrammar *g, int symbol, int parent, CeText spelling) {
    CeTokens tokens = {0}; CeError error = {{0},0};
    bool ok = ce_lex(g,spelling,&tokens,&error) && tokens.len == 1 &&
        tokens.data[0].offset == 0 && tokens.data[0].text.len == spelling.len &&
        ce_token_matches(g,parent,symbol,&tokens.data[0]);
    ce_tokens_free(&tokens);
    return ok;
}

static int transparent_child(const CeGrammar *g, const CeProduction *p) {
    if (p->internal || !ce_is(p->label,"_")) return -1;
    int child = -1; size_t count = 0;
    for (size_t i = 0; i < p->len; ++i)
        if (g->symbols[p->rhs[i]].kind != CE_LITERAL && !g->symbols[p->rhs[i]].erased) {
            child = p->rhs[i]; ++count;
        }
    return count == 1 ? child : -1;
}

static bool source_reachable(const CeGrammar *g, int source, int target, unsigned char *seen) {
    if (source == target) return true;
    if (seen[source]) return false;
    seen[source] = 1;
    for (size_t i = 0; i < g->nproductions; ++i) {
        const CeProduction *p = &g->productions[i];
        int child = p->lhs == source ? transparent_child(g,p) : -1;
        if (child >= 0 && source_reachable(g,child,target,seen)) return true;
    }
    return false;
}

static bool list_admitted(const CeGrammar *g, int symbol, size_t len) {
    bool empty = false, singleton = false, cons = false;
    for (size_t i = 0; i < g->nproductions; ++i) {
        const CeProduction *p = &g->productions[i];
        if (p->lhs != symbol || p->internal) continue;
        empty |= ce_is(p->label,"[]"); singleton |= ce_is(p->label,"(:[])"); cons |= ce_is(p->label,"(:)");
    }
    return len ? (singleton || (cons && empty)) && (len == 1 || cons) : empty;
}

static int source_list(const CeGrammar *g, int required, size_t len) {
    if (g->symbols[required].list_element >= 0 && list_admitted(g,required,len)) return required;
    unsigned char *seen = ce_resize(NULL,g->nsymbols,1);
    int selected = -1;
    for (size_t i = 0; i < g->nsymbols; ++i) {
        if (g->symbols[i].list_element < 0 || !same_sort(g,required,(int)i) || !list_admitted(g,(int)i,len)) continue;
        memset(seen,0,g->nsymbols);
        if (source_reachable(g,required,(int)i,seen)) { selected = (int)i; break; }
    }
    free(seen); return selected;
}

int ce_tree_list_symbol(const CeGrammar *g, int required, size_t len) {
    return source_list(g,required,len);
}

static bool decode_nested(const CeGrammar *g, const CeAtom *atom, int sort, int parent,
                           CeArena *arena, CeAtomDecoder child, void *context,
                           CeTree **out, CeError *error, size_t depth) {
    return child ? child(context,atom,sort,parent,arena,out,error,depth) :
        ce_tree_decode_atom(g,atom,sort,parent,arena,NULL,NULL,out,error,depth);
}

static bool atom_head(const CeAtom *atom, const char *name) {
    return atom->kind == CE_ATOM_LIST && atom->len &&
        atom->items[0]->kind == CE_ATOM_SYMBOL && ce_is(atom->items[0]->text,name);
}

bool ce_tree_decode_atom(const CeGrammar *g, const CeAtom *atom, int sort, int parent,
                         CeArena *a, CeAtomDecoder child, void *context,
                         CeTree **out, CeError *error, size_t depth) {
    if (depth > 4096) return ce_error(error,atom->offset,"S-expression nesting exceeds decoder stack depth");
    size_t offset = atom->offset;
    int required = sort;
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].sort == g->symbols[sort].sort &&
            (g->symbols[i].kind == CE_TOKEN ||
             (g->symbols[sort].list_element < 0 && g->symbols[i].list_element >= 0))) { sort = (int)i; break; }
    if (g->symbols[sort].kind == CE_TOKEN) {
        CeText spelling;
        if (ce_token_symbol_representation(g,sort)) {
            if (atom->kind == CE_ATOM_SYMBOL) {
                spelling = atom->text;
                if (!plain_identifier(spelling)) return ce_error(error,offset,"identifier requires c:Symbol wrapping");
            } else {
                if (!atom_head(atom,"c:Symbol") || atom->len != 2 || atom->items[1]->kind != CE_ATOM_TEXT)
                    return ce_error(error,offset,"expected identifier symbol or c:Symbol string");
                spelling = atom->items[1]->text;
                if (plain_identifier(spelling)) return ce_error(error,offset,"safe identifier must use its bare symbol form");
            }
        } else {
            if (atom->kind != CE_ATOM_TEXT) return ce_error(error,offset,"expected exact token spelling string");
            spelling = atom->text;
        }
        if (!token_admitted(g,sort,parent,spelling)) return ce_error(error,offset,"spelling is not admitted by the lexical profile for %.*s",(int)g->symbols[sort].name.len,g->symbols[sort].name.data);
        *out = make(a,sort,-1,0); (*out)->spelling = spelling; (*out)->offset = offset;
        return true;
    }
    if (atom->kind != CE_ATOM_LIST || !atom->len || atom->items[0]->kind != CE_ATOM_SYMBOL)
        return ce_error(error,offset,"expected c: constructor head");
    CeText name = atom->items[0]->text;
    if (name.len <= 2 || name.data[0] != 'c' || name.data[1] != ':')
        return ce_error(error,atom->items[0]->offset,"expected c: constructor head");
    name.data += 2; name.len -= 2;
    if (g->symbols[sort].list_element >= 0) {
        if (!ce_is(name,"List")) return ce_error(error,offset,"expected c:List for list sort");
        size_t len = atom->len-1;
        int actual = source_list(g,required,len);
        if (actual < 0) return ce_error(error,offset,"list length is not admitted by the source grammar");
        CeTree *tree = make(a,actual,-2,len); tree->offset = offset;
        for (size_t i = 0; i < len; ++i)
            if (!decode_nested(g,atom->items[i+1],g->symbols[sort].list_element,-1,a,child,context,&tree->children[i],error,depth+1)) return false;
        *out = tree; return true;
    }
    int prod = -1;
    for (size_t i = 0; i < g->nproductions; ++i) if (ce_equal(g->productions[i].label,name)) {
        if (g->productions[i].internal || ce_is(name,"_") || g->symbols[g->productions[i].lhs].erased || g->symbols[g->productions[i].lhs].list_element >= 0)
            return ce_error(error,offset,"constructor is outside the source-tree codec");
        if (!same_sort(g,g->productions[i].lhs,sort)) return ce_error(error,offset,"constructor %.*s has the wrong result sort",(int)name.len,name.data);
        prod = (int)i; break;
    }
    if (prod < 0) return ce_error(error,offset,"unknown constructor c:%.*s",(int)name.len,name.data);
    const CeProduction *p = &g->productions[prod];
    size_t arity = 0;
    for (size_t i = 0; i < p->len; ++i) if (g->symbols[p->rhs[i]].kind != CE_LITERAL && !g->symbols[p->rhs[i]].erased) ++arity;
    if (atom->len-1 != arity) return ce_error(error,offset,"constructor c:%.*s expects %zu arguments, got %zu",(int)name.len,name.data,arity,atom->len-1);
    CeTree *t = make(a,p->lhs,prod,arity); t->offset = offset;
    size_t j = 0;
    for (size_t i = 0; i < p->len; ++i) if (g->symbols[p->rhs[i]].kind != CE_LITERAL && !g->symbols[p->rhs[i]].erased) {
        if (!decode_nested(g,atom->items[j+1],p->rhs[i],prod,a,child,context,&t->children[j],error,depth+1)) return false;
        ++j;
    }
    *out = t; return true;
}

bool ce_tree_decode(const CeGrammar *g, CeText source, int sort, CeArena *a, CeTree **out, CeError *error) {
    CeAtom *atom;
    return ce_atom_read(a,source,&atom,error) &&
        ce_tree_decode_atom(g,atom,sort,-1,a,NULL,NULL,out,error,0);
}

CeAtom *ce_tree_to_atom(const CeGrammar *g, const CeTree *tree, CeArena *arena) {
    if (tree->production == CE_TREE_SOURCE) {
        CeAtom *atom = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},2);
        atom->items[0] = ce_atom_new(arena,CE_ATOM_SYMBOL,ce_cstr(arena,"c:OpenSource"),0);
        atom->items[1] = ce_atom_new(arena,CE_ATOM_TEXT,tree->spelling,0);
        atom->offset = tree->offset; return atom;
    }
    if (tree->production == -1) {
        if (!ce_token_symbol_representation(g,tree->symbol)) return ce_atom_new(arena,CE_ATOM_TEXT,tree->spelling,0);
        if (plain_identifier(tree->spelling)) return ce_atom_new(arena,CE_ATOM_SYMBOL,tree->spelling,0);
        CeAtom *atom = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},2);
        atom->items[0] = ce_atom_new(arena,CE_ATOM_SYMBOL,ce_cstr(arena,"c:Symbol"),0);
        atom->items[1] = ce_atom_new(arena,CE_ATOM_TEXT,tree->spelling,0);
        return atom;
    }
    CeText label = tree->production == -2 ? ce_cstr(arena,"List") : g->productions[tree->production].label;
    unsigned char *bytes = ce_alloc(arena,label.len+3);
    memcpy(bytes,"c:",2); memcpy(bytes+2,label.data,label.len);
    CeAtom *atom = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},tree->len+1);
    atom->items[0] = ce_atom_new(arena,CE_ATOM_SYMBOL,(CeText){bytes,label.len+2},0);
    for (size_t i = 0; i < tree->len; ++i) atom->items[i+1] = ce_tree_to_atom(g,tree->children[i],arena);
    atom->offset = tree->offset; return atom;
}

bool ce_tree_equal(const CeGrammar *g, const CeTree *a, const CeTree *b) {
    if (a == b) return true;
    if (a->production != b->production || !same_sort(g,a->symbol,b->symbol) || a->len != b->len || !ce_equal(a->spelling,b->spelling)) return false;
    for (size_t i = 0; i < a->len; ++i) if (!ce_tree_equal(g,a->children[i],b->children[i])) return false;
    return true;
}

typedef struct {
    FILE *file;
    size_t block_depth, group_depth, *group_floors, floor_capacity;
    bool line_start, pending_break, open_source, attach;
    unsigned previous;
    CeText previous_text;
    CeDFA lexer;
    const CeDFA *borrowed;
    CeLazyLexer *shared;
    bool lexer_attempted, lexer_ready;
    int parent;
    size_t argument;
    /* Source mapping, when marks are given: the physical position written
       next, the line a #line directive made it stand for (0 when none), and
       the offset of the innermost tree with a source position. */
    const CeLineMarks *marks;
    size_t line, column, presumed, offset, *starts, nstarts;
    bool failed;
} CePrinter;

static void put(CePrinter *out, const void *data, size_t length) {
    fwrite(data,1,length,out->file);
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; ++i)
        if (bytes[i] == '\n') { ++out->line; out->column = 1; if (out->presumed) ++out->presumed; }
        else ++out->column;
}

static void put_char(CePrinter *out, int c) { unsigned char byte = (unsigned char)c; put(out,&byte,1); }

static void print_break(CePrinter *out) {
    if (!out->line_start) put_char(out,'\n');
    out->line_start = true; out->pending_break = false; out->previous = 0;
    out->previous_text = (CeText){NULL,0};
}

static bool map_grow(void **items, size_t *capacity, size_t length, size_t size) {
    if (length < *capacity) return true;
    size_t next = *capacity ? *capacity*2 : 64;
    void *grown = realloc(*items,next*size);
    if (!grown) return false;
    *items = grown; *capacity = next; return true;
}

size_t ce_source_line(CeText source, size_t offset, size_t *column) {
    size_t line = 1, start = 0;
    for (size_t i = 0; i < offset && i < source.len; ++i)
        if (source.data[i] == '\n') { ++line; start = i+1; }
    if (column) *column = offset-start+1;
    return line;
}

static size_t printer_source_line(CePrinter *out, size_t offset) {
    size_t low = 0, high = out->nstarts;
    while (low+1 < high) {
        size_t middle = (low+high)/2;
        if (out->starts[middle] <= offset) low = middle; else high = middle;
    }
    return low+1;
}

static void emit_directive(CePrinter *out, size_t line);

/* A directive starts a line at a statement-level tree, outside parentheses,
   whenever the line the compiler would assume differs from the source line. */
static void mark_line(CePrinter *out, const CeTree *t, int sort) {
    const CeLineMarks *m = out->marks;
    if (!m || !t->offset || t->offset >= m->source.len) return;
    if (!(sort >= 0 && m->sorts[sort]) && !(t->symbol >= 0 && m->sorts[t->symbol])) return;
    size_t floor = out->block_depth ? out->group_floors[out->block_depth] : 0;
    if (out->group_depth != floor) return;
    size_t line = printer_source_line(out,t->offset);
    /* A statement-level tree starts a line; decide on the line it starts. */
    print_break(out);
    if (out->presumed != line) emit_directive(out,line);
}

/* The line of the parenthesis closing the form that starts at `offset`, so a
   closing brace stands where its form closes. Strings, characters and
   comments do not count. */
static size_t closing_line(CePrinter *out, size_t offset) {
    CeText source = out->marks->source;
    if (offset >= source.len || source.data[offset] != '(') return printer_source_line(out,offset);
    size_t depth = 0;
    for (size_t i = offset; i < source.len; ++i) {
        unsigned char c = source.data[i];
        if (c == '"' || c == '\'') {
            for (++i; i < source.len && source.data[i] != c; ++i) if (source.data[i] == '\\') ++i;
        } else if (c == ';') {
            while (i < source.len && source.data[i] != '\n') ++i;
        } else if (c == '(') ++depth;
        else if (c == ')' && !--depth) return printer_source_line(out,i);
    }
    return printer_source_line(out,offset);
}

static void emit_directive(CePrinter *out, size_t line) {
    const CeLineMarks *m = out->marks;
    char number[32]; int n = snprintf(number,sizeof number,"#line %zu \"",line);
    put(out,number,(size_t)n);
    for (const char *c = m->path; *c; ++c) {
        if (*c == '"' || *c == '\\') put_char(out,'\\');
        put_char(out,*c);
    }
    put(out,"\"\n",2);
    out->presumed = line;
    if (m->map) {
        CeSourceMap *map = m->map;
        if (!map_grow((void **)&map->directives,&map->directive_capacity,map->ndirectives,sizeof(*map->directives)))
            out->failed = true;
        else map->directives[map->ndirectives++] = (CeMapDirective){out->line,line};
    }
}

static void map_token(CePrinter *out, size_t length) {
    CeSourceMap *map = out->marks ? out->marks->map : NULL;
    if (!map || !out->offset) return;
    if (!map_grow((void **)&map->tokens,&map->token_capacity,map->ntokens,sizeof(*map->tokens))) { out->failed = true; return; }
    map->tokens[map->ntokens++] = (CeMapToken){out->line,out->column,length,out->offset};
}

const CeDFA *ce_lazy_lexer(CeLazyLexer *lexer, const CeGrammar *g) {
    if (lexer->grammar && lexer->grammar != g) return NULL;
    if (!lexer->attempted) {
        CeError ignored = {{0},0};
        lexer->grammar = g; lexer->attempted = true;
        lexer->ready = ce_dfa_prepare(g,&lexer->dfa,&ignored);
    }
    return lexer->ready ? &lexer->dfa : NULL;
}

void ce_lazy_lexer_free(CeLazyLexer *lexer) {
    ce_dfa_free(&lexer->dfa); *lexer = (CeLazyLexer){0};
}

static bool adjacent_tokens(const CeGrammar *g, CePrinter *out, CeText text) {
    CeError error = {{0},0};
    const CeDFA *lexer = out->borrowed;
    if (!lexer && out->shared) lexer = ce_lazy_lexer(out->shared,g);
    if (!lexer) {
        if (!out->lexer_attempted) {
            out->lexer_attempted = true;
            out->lexer_ready = ce_dfa_prepare(g,&out->lexer,&error);
        }
        if (!out->lexer_ready) return false;
        lexer = &out->lexer;
    }
    size_t length = out->previous_text.len+text.len;
    unsigned char *bytes = ce_resize(NULL,length+1,1);
    memcpy(bytes,out->previous_text.data,out->previous_text.len);
    memcpy(bytes+out->previous_text.len,text.data,text.len);
    CeTokens tokens = {0};
    bool same = ce_dfa_lex(g,lexer,(CeText){bytes,length},&tokens,&error) && tokens.len == 2 &&
        tokens.data[0].offset == 0 && tokens.data[0].text.len == out->previous_text.len &&
        tokens.data[1].offset == out->previous_text.len && tokens.data[1].text.len == text.len;
    ce_tokens_free(&tokens); free(bytes); return same;
}

static void print_token(const CeGrammar *g, CeText text, int symbol, bool line, CePrinter *out) {
    unsigned flags = 0;
    for (size_t i = 0; i < g->nlayout; ++i)
        if (g->layout[i].symbol == symbol) flags = g->layout[i].flags;
    if (line) print_break(out);
    if (flags & CE_LAYOUT_BLOCK_CLOSE) {
        if (out->block_depth) --out->block_depth;
        print_break(out);
        if (out->marks && out->offset) {
            size_t line = closing_line(out,out->offset);
            if (out->presumed != line) emit_directive(out,line);
        }
    }
    if (out->pending_break) {
        if (flags & (CE_LAYOUT_JOIN | CE_LAYOUT_LEFT | CE_LAYOUT_GROUP_CLOSE)) out->pending_break = false;
        else print_break(out);
    }
    if (out->line_start) {
        size_t width = g->indentation ? g->indentation : 2;
        for (size_t i = 0; i < out->block_depth*width; ++i) put_char(out,' ');
        out->line_start = false;
    } else if (out->attach && adjacent_tokens(g,out,text)) {
    } else if (((flags & CE_LAYOUT_BLOCK_OPEN) && !(out->previous & CE_LAYOUT_GROUP_OPEN)) ||
        ((flags & CE_LAYOUT_GROUP_OPEN) && (out->previous & CE_LAYOUT_SPACED) && !(out->previous & CE_LAYOUT_RIGHT)) ||
        (!(flags & CE_LAYOUT_LEFT) && !(out->previous & CE_LAYOUT_RIGHT)) ||
        !adjacent_tokens(g,out,text)) put_char(out,' ');
    map_token(out,text.len);
    put(out,text.data,text.len); out->previous = flags; out->previous_text = text;
    if (flags & CE_LAYOUT_GROUP_OPEN) ++out->group_depth;
    if ((flags & CE_LAYOUT_GROUP_CLOSE) && out->group_depth) --out->group_depth;
    if (flags & CE_LAYOUT_BLOCK_OPEN) {
        ++out->block_depth;
        if (out->block_depth >= out->floor_capacity) {
            out->floor_capacity = out->floor_capacity ? out->floor_capacity*2 : 16;
            out->group_floors = ce_resize(out->group_floors,out->floor_capacity,sizeof(*out->group_floors));
        }
        out->group_floors[out->block_depth] = out->group_depth;
        print_break(out);
    }
    size_t floor = out->block_depth ? out->group_floors[out->block_depth] : 0;
    if ((flags & CE_LAYOUT_LINE) && out->group_depth == floor) print_break(out);
    if (flags & CE_LAYOUT_BLOCK_CLOSE) out->pending_break = true;
    if (line) print_break(out);
}

static bool render(const CeGrammar *g, const CeTree *t, int sort, CePrinter *out,
                   CeError *error, unsigned char *active, size_t depth);

static void print_terminal(const CeGrammar *g, int symbol, CePrinter *out) {
    const CeSymbol *s = &g->symbols[symbol];
    if (s->erased || (s->virtual_token && !s->surface.len)) return;
    CeText text = s->surface.len ? s->surface : s->name;
    print_token(g,text,symbol,false,out);
}

static bool render_rhs(const CeGrammar *g, const CeProduction *p, const CeTree *t,
                       CePrinter *out, CeError *error, unsigned char *active, size_t depth) {
    size_t arguments = 0; bool literals = false;
    for (size_t i = 0; i < p->len; ++i) if (!g->symbols[p->rhs[i]].erased) {
        literals |= g->symbols[p->rhs[i]].kind == CE_LITERAL;
        arguments += g->symbols[p->rhs[i]].kind != CE_LITERAL;
    }
    bool transparent = arguments == 1 && !literals;
    int previous_parent = out->parent; size_t previous_argument = out->argument;
    size_t child = 0;
    for (size_t i = 0; i < p->len; ++i) {
        int symbol = p->rhs[i];
        if (g->symbols[symbol].erased) continue;
        if (g->symbols[symbol].kind == CE_LITERAL) {
            for (size_t k = 0; k < g->nattachments && !out->attach; ++k)
                out->attach = g->attachments[k].symbol == symbol && g->attachments[k].production == (int)(p-g->productions);
            print_terminal(g,symbol,out);
            out->attach = false;
            /* A prefix operator opening its production binds to its operand;
               the lexer check still separates tokens that would merge. */
            if (!child && i+1 < p->len && g->symbols[p->rhs[i+1]].kind != CE_LITERAL)
                for (size_t k = 0; k < g->nlayout; ++k)
                    if (g->layout[k].symbol == symbol && (g->layout[k].flags & CE_LAYOUT_PREFIX))
                        out->previous |= CE_LAYOUT_RIGHT;
        } else {
            if (!transparent && g->symbols[p->lhs].list_element < 0) {
                out->parent = (int)(p-g->productions); out->argument = child;
            }
            if (!render(g,t->children[child++],symbol,out,error,active,depth+1)) return false;
            out->parent = previous_parent; out->argument = previous_argument;
        }
    }
    return true;
}

static bool reachable(const CeGrammar *g, int source, int target, unsigned char *seen) {
    if (source == target) return true;
    if (seen[source]) return false;
    seen[source] = 1;
    for (size_t i = 0; i < g->nproductions; ++i) {
        const CeProduction *p = &g->productions[i];
        if (p->lhs == source && ce_is(p->label,"_") && p->len == 1 && reachable(g,p->rhs[0],target,seen)) return true;
    }
    return false;
}

static void literal_range(const CeGrammar *g, const CeProduction *p, size_t begin, size_t end, CePrinter *out) {
    for (size_t i = begin; i < end; ++i) {
        print_terminal(g,p->rhs[i],out);
    }
}

static bool render_list(const CeGrammar *g, const CeTree *t, CePrinter *out,
                        CeError *error, unsigned char *active, size_t depth) {
    int empty = -1, one = -1, cons = -1;
    for (size_t i = 0; i < g->nproductions; ++i) {
        const CeProduction *p = &g->productions[i];
        if (p->lhs != t->symbol || p->internal) continue;
        if (empty < 0 && ce_is(p->label,"[]")) empty = (int)i;
        if (one < 0 && ce_is(p->label,"(:[])")) one = (int)i;
        if (cons < 0 && ce_is(p->label,"(:)")) cons = (int)i;
    }
    if ((!t->len && empty < 0) || (t->len && one < 0 && (cons < 0 || empty < 0)) || (t->len > 1 && cons < 0))
        return ce_error(error,t->offset,"list length has no source printer production");
    if (!t->len) return render_rhs(g,&g->productions[empty],t,out,error,active,depth);
    size_t repetitions = one >= 0 ? t->len-1 : t->len;
    const CeProduction *p = repetitions ? &g->productions[cons] : NULL;
    size_t self = 0, element = 0;
    if (p) for (size_t i = 0; i < p->len; ++i) {
        if (p->rhs[i] == t->symbol) self = i;
        else if (g->symbols[p->rhs[i]].kind != CE_LITERAL) element = i;
    }
    bool left = p && self < element;
    if (left) for (size_t i = 0; i < repetitions; ++i) literal_range(g,p,0,self,out);
    else for (size_t i = 0; i < repetitions; ++i) {
        literal_range(g,p,0,element,out);
        if (!render(g,t->children[i],p->rhs[element],out,error,active,depth+1)) return false;
        literal_range(g,p,element+1,self,out);
    }
    CeTree base = *t; base.len = one >= 0 ? 1 : 0;
    if (one >= 0) base.children += left ? 0 : t->len-1;
    if (!render_rhs(g,&g->productions[one >= 0 ? one : empty],&base,out,error,active,depth)) return false;
    if (left) for (size_t i = t->len-repetitions; i < t->len; ++i) {
        literal_range(g,p,self+1,element,out);
        if (!render(g,t->children[i],p->rhs[element],out,error,active,depth+1)) return false;
        literal_range(g,p,element+1,p->len,out);
    }
    else for (size_t i = 0; i < repetitions; ++i) literal_range(g,p,self+1,p->len,out);
    return true;
}

static bool render_body(const CeGrammar *g, const CeTree *t, int sort, CePrinter *out,
                        CeError *error, unsigned char *active, size_t depth) {
    if (depth > 8192) return ce_error(error,t->offset,"printer nesting exceeds stack depth");
    CeTree list;
    if (t->production == -2) {
        int selected = source_list(g,sort,t->len);
        if (selected < 0) return ce_error(error,t->offset,"list length has no directed source admission");
        list = *t; list.symbol = selected; t = &list;
    }
    memset(active,0,g->nsymbols);
    if (reachable(g,sort,t->symbol,active)) {
        if (t->production == -1) {
            bool line = false;
            for (size_t i = 0; i < g->nlex; ++i)
                if (g->lex[i].symbol == t->symbol) line = g->lex[i].line;
            print_token(g,t->spelling,t->symbol,line,out); return true;
        }
        if (t->production == -2) return render_list(g,t,out,error,active,depth);
        return render_rhs(g,&g->productions[t->production],t,out,error,active,depth);
    }
    /* Transparent productions preserve the surrounding literal syntax. */
    for (size_t i = 0; i < g->nproductions; ++i) {
        const CeProduction *p = &g->productions[i];
        if (!ce_is(p->label,"_") || p->len <= 1) continue;
        memset(active,0,g->nsymbols);
        if (!reachable(g,sort,p->lhs,active)) continue;
        size_t nchild = 0;
        int inside = -1;
        for (size_t j = 0; j < p->len; ++j) if (g->symbols[p->rhs[j]].kind != CE_LITERAL && !g->symbols[p->rhs[j]].erased) { ++nchild; inside = p->rhs[j]; }
        memset(active,0,g->nsymbols);
        if (nchild != 1 || !source_reachable(g,inside,t->symbol,active)) continue;
        for (size_t j = 0; j < p->len; ++j) {
            int symbol = p->rhs[j];
            if (g->symbols[symbol].erased) continue;
            if (g->symbols[symbol].kind == CE_LITERAL) {
                print_terminal(g,symbol,out);
            } else if (!render(g,t,symbol,out,error,active,depth+1)) return false;
        }
        return true;
    }
    return ce_error(error,t->offset,"constructor cannot be printed in required category");
}

static bool render_tree(const CeGrammar *g, const CeTree *t, int sort, CePrinter *out,
                        CeError *error, unsigned char *active, size_t depth);

static bool render(const CeGrammar *g, const CeTree *t, int sort, CePrinter *out,
                   CeError *error, unsigned char *active, size_t depth) {
    size_t enclosing = out->offset;
    if (t->offset) out->offset = t->offset;
    mark_line(out,t,sort);
    bool ok = render_tree(g,t,sort,out,error,active,depth);
    out->offset = enclosing;
    return ok;
}

static bool render_tree(const CeGrammar *g, const CeTree *t, int sort, CePrinter *out,
                        CeError *error, unsigned char *active, size_t depth) {
    if (t->production == CE_TREE_SOURCE) {
        if (!out->open_source) return ce_error(error,t->offset,"open C source requires the native authoring printer");
        if (!same_sort(g,sort,t->symbol)) return ce_error(error,t->offset,"open C source has the wrong grammar sort");
        print_break(out);
        map_token(out,0);
        put(out,t->spelling.data,t->spelling.len);
        if (t->spelling.len && t->spelling.data[t->spelling.len-1] != '\n') put_char(out,'\n');
        /* Source text may carry its own directives or lines the authored text
           does not have; the next statement-level tree states its line again. */
        out->presumed = 0;
        return !ferror(out->file) || ce_error(error,t->offset,"cannot print open C source");
    }
    for (size_t i = 0; i < g->ngroupings; ++i) if (g->groupings[i].production == t->production) {
        const CeGrouping *group = &g->groupings[i];
        if (group->parent >= 0 && (group->parent != out->parent ||
            (group->argument != SIZE_MAX && group->argument != out->argument))) continue;
        const CeProduction *wrapper = &g->productions[g->groupings[i].wrapper];
        memset(active,0,g->nsymbols);
        if (!source_reachable(g,sort,wrapper->lhs,active)) continue;
        for (size_t j = 0; j < wrapper->len; ++j) {
            int symbol = wrapper->rhs[j];
            if (g->symbols[symbol].erased) continue;
            if (g->symbols[symbol].kind == CE_LITERAL) print_terminal(g,symbol,out);
            else if (!render_body(g,t,symbol,out,error,active,depth+1)) return false;
        }
        return true;
    }
    return render_body(g,t,sort,out,error,active,depth);
}

/* A borrowed lexer must have been prepared for g. Without one, a shared or
   private lexer is prepared on the first token-boundary question. */
static bool print_with(const CeGrammar *g, const CeDFA *lexer, CeLazyLexer *shared,
                       const CeTree *tree, int sort, bool open_source, const CeLineMarks *marks,
                       FILE *file, CeError *error) {
    unsigned char *active = calloc(g->nsymbols,1);
    if (!active) { fputs("cesta: out of memory\n",stderr); exit(2); }
    CePrinter out = {.file=file,.line_start=true,.parent=-1,.borrowed=lexer,.shared=shared,
                     .open_source=open_source,.marks=marks,.line=1,.column=1};
    if (marks) {
        out.starts = malloc((marks->source.len+1)*sizeof(*out.starts));
        if (!out.starts) { fputs("cesta: out of memory\n",stderr); exit(2); }
        out.starts[out.nstarts++] = 0;
        for (size_t i = 0; i < marks->source.len; ++i)
            if (marks->source.data[i] == '\n') out.starts[out.nstarts++] = i+1;
    }
    bool ok = render(g,tree,sort,&out,error,active,0);
    if (ok && out.failed) ok = ce_error(error,0,"cannot record the source map");
    free(out.starts); free(out.group_floors); ce_dfa_free(&out.lexer); free(active); return ok;
}

bool ce_tree_print_prepared(const CeGrammar *g, const CeDFA *lexer, const CeTree *tree,
                            int sort, FILE *file, CeError *error) {
    return print_with(g,lexer,NULL,tree,sort,false,NULL,file,error);
}

bool ce_tree_print(const CeGrammar *g, const CeTree *tree, int sort, FILE *file, CeError *error) {
    return print_with(g,NULL,NULL,tree,sort,false,NULL,file,error);
}

bool ce_tree_print_shared(const CeGrammar *g, CeLazyLexer *lexer, const CeTree *tree,
                          int sort, FILE *file, CeError *error) {
    return print_with(g,NULL,lexer,tree,sort,false,NULL,file,error);
}

bool ce_tree_print_open_shared(const CeGrammar *g, CeLazyLexer *lexer, const CeTree *tree,
                               int sort, FILE *file, CeError *error) {
    return print_with(g,NULL,lexer,tree,sort,true,NULL,file,error);
}

bool ce_tree_print_marked(const CeGrammar *g, CeLazyLexer *lexer, const CeTree *tree, int sort,
                          const CeLineMarks *marks, FILE *file, CeError *error) {
    return print_with(g,NULL,lexer,tree,sort,true,marks,file,error);
}

void ce_source_map_free(CeSourceMap *map) {
    free(map->tokens); free(map->directives); *map = (CeSourceMap){0};
}

bool ce_tree_print_open(const CeGrammar *g, const CeTree *tree, int sort, FILE *file, CeError *error) {
    return print_with(g,NULL,NULL,tree,sort,true,NULL,file,error);
}

bool ce_token_value(CeArena *arena, CeText spelling, CeText *out, CeError *error) {
    CeMetaScanner s = {arena,spelling,0,{{NULL,0},0,0},error};
    if (!advance(&s) || s.token.kind != CE_META_STRING) return ce_error(error,0,"expected string token spelling");
    *out = s.token.text;
    return advance(&s) && (s.token.kind == CE_META_END || ce_error(error,s.token.offset,"extra text after string token"));
}

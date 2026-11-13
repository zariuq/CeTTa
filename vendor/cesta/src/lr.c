#include "cesta.h"

#include <stdlib.h>
#include <string.h>

typedef struct { int production; size_t dot; uint64_t *lookahead; } LRItem;
typedef struct { LRItem *items; size_t len; int *edges; } LRState;
typedef struct {
    const CeGrammar *grammar;
    int entry;
    LRState *states;
    size_t len, words;
    uint64_t *first;
} Builder;

static size_t length(const Builder *b, int production) {
    return production < 0 ? 1 : b->grammar->productions[production].len;
}
static int symbol_at(const Builder *b, int production, size_t dot) {
    return production < 0 ? b->entry : b->grammar->productions[production].rhs[dot];
}
static bool insert(LRState *s, int production, size_t dot) {
    for (size_t i = 0; i < s->len; ++i)
        if (s->items[i].production == production && s->items[i].dot == dot) return false;
    s->items = ce_resize(s->items,s->len+1,sizeof(*s->items));
    s->items[s->len++] = (LRItem){production,dot,NULL}; return true;
}
static int compare(const void *a, const void *b) {
    const LRItem *x = a, *y = b;
    if (x->production != y->production) return x->production < y->production ? -1 : 1;
    return x->dot < y->dot ? -1 : x->dot > y->dot ? 1 : 0;
}
static void item_closure(const Builder *b, LRState *s) {
    for (size_t i = 0; i < s->len; ++i) {
        LRItem item = s->items[i];
        if (item.dot == length(b,item.production)) continue;
        int symbol = symbol_at(b,item.production,item.dot);
        if (b->grammar->symbols[symbol].kind != CE_NONTERMINAL) continue;
        for (size_t p = 0; p < b->grammar->nproductions; ++p)
            if (b->grammar->productions[p].lhs == symbol && !b->grammar->productions[p].internal) insert(s,(int)p,0);
    }
    qsort(s->items,s->len,sizeof(*s->items),compare);
}
static int intern(Builder *b, LRState s) {
    item_closure(b,&s);
    for (size_t i = 0; i < b->len; ++i) {
        if (s.len != b->states[i].len) continue;
        bool same = true;
        for (size_t j = 0; j < s.len; ++j) if (compare(&s.items[j],&b->states[i].items[j])) { same = false; break; }
        if (same) { free(s.items); return (int)i; }
    }
    s.edges = ce_resize(NULL,b->grammar->nsymbols,sizeof(*s.edges));
    for (size_t i = 0; i < b->grammar->nsymbols; ++i) s.edges[i] = -1;
    b->states = ce_resize(b->states,b->len+1,sizeof(*b->states));
    int id = (int)b->len++; b->states[id] = s; return id;
}
static void automaton(Builder *b) {
    LRState start = {0}; insert(&start,-1,0); (void)intern(b,start);
    for (size_t i = 0; i < b->len; ++i) {
        for (size_t symbol = 0; symbol < b->grammar->nsymbols; ++symbol) {
            LRState target = {0};
            for (size_t j = 0; j < b->states[i].len; ++j) {
                LRItem item = b->states[i].items[j];
                if (item.dot < length(b,item.production) && symbol_at(b,item.production,item.dot) == (int)symbol)
                    insert(&target,item.production,item.dot+1);
            }
            if (target.len) { int next = intern(b,target); b->states[i].edges[symbol] = next; }
        }
    }
}
static bool merge(uint64_t *target, const uint64_t *source, size_t words) {
    bool changed = false;
    for (size_t i = 0; i < words; ++i) {
        uint64_t next = target[i] | source[i]; changed |= next != target[i]; target[i] = next;
    }
    return changed;
}
static bool contains(const uint64_t *set, size_t bit) { return (set[bit/64] & (UINT64_C(1) << (bit%64))) != 0; }
static bool nonempty(const uint64_t *set, size_t words) {
    for (size_t i = 0; i < words; ++i) if (set[i]) return true;
    return false;
}
static void first_sets(Builder *b) {
    const CeGrammar *g = b->grammar;
    b->first = calloc(g->nsymbols*b->words,sizeof(uint64_t));
    if (!b->first) { fputs("cesta: out of memory\n",stderr); exit(2); }
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].kind != CE_NONTERMINAL) b->first[i*b->words+i/64] |= UINT64_C(1) << (i%64);
    bool changed;
    do {
        changed = false;
        for (size_t p = 0; p < g->nproductions; ++p) {
            const CeProduction *rule = &g->productions[p];
            if (rule->internal) continue;
            for (size_t j = 0; j < rule->len; ++j) {
                int symbol = rule->rhs[j];
                changed |= merge(b->first+rule->lhs*b->words,b->first+symbol*b->words,b->words);
                if (!g->symbols[symbol].nullable) break;
            }
        }
    } while (changed);
}
static LRItem *find_item(LRState *s, int production, size_t dot) {
    LRItem key = {production,dot,NULL};
    return bsearch(&key,s->items,s->len,sizeof(*s->items),compare);
}
static void lookaheads(Builder *b) {
    const CeGrammar *g = b->grammar;
    for (size_t i = 0; i < b->len; ++i) for (size_t j = 0; j < b->states[i].len; ++j) {
        b->states[i].items[j].lookahead = calloc(b->words,sizeof(uint64_t));
        if (!b->states[i].items[j].lookahead) { fputs("cesta: out of memory\n",stderr); exit(2); }
    }
    LRItem *start = find_item(&b->states[0],-1,0);
    start->lookahead[g->nsymbols/64] |= UINT64_C(1) << (g->nsymbols%64);
    uint64_t *suffix = calloc(b->words,sizeof(uint64_t));
    if (!suffix) { fputs("cesta: out of memory\n",stderr); exit(2); }
    bool changed;
    do {
        changed = false;
        for (size_t i = 0; i < b->len; ++i) {
            LRState *s = &b->states[i];
            for (size_t j = 0; j < s->len; ++j) {
                LRItem item = s->items[j];
                if (!nonempty(item.lookahead,b->words) || item.dot == length(b,item.production)) continue;
                int symbol = symbol_at(b,item.production,item.dot);
                int next = s->edges[symbol];
                LRItem *advanced = find_item(&b->states[next],item.production,item.dot+1);
                changed |= merge(advanced->lookahead,item.lookahead,b->words);
                if (g->symbols[symbol].kind != CE_NONTERMINAL) continue;
                memset(suffix,0,b->words*sizeof(uint64_t));
                bool nullable = true;
                for (size_t k = item.dot+1; k < length(b,item.production); ++k) {
                    int sym = symbol_at(b,item.production,k);
                    (void)merge(suffix,b->first+sym*b->words,b->words);
                    if (!g->symbols[sym].nullable) { nullable = false; break; }
                }
                if (nullable) (void)merge(suffix,item.lookahead,b->words);
                for (size_t k = 0; k < s->len; ++k) {
                    LRItem *predicted = &s->items[k];
                    if (!predicted->dot && predicted->production >= 0 && g->productions[predicted->production].lhs == symbol)
                        changed |= merge(predicted->lookahead,suffix,b->words);
                }
            }
        }
    } while (changed);
    free(suffix);
}
static void add_action(CeActionCell *cell, unsigned kind, int value) {
    for (size_t i = 0; i < cell->len; ++i) if (cell->actions[i].kind == kind && cell->actions[i].value == value) return;
    cell->actions = ce_resize(cell->actions,cell->len+1,sizeof(*cell->actions));
    cell->actions[cell->len++] = (CeAction){kind,value};
}
static bool action_matches(CeAction actual, CeAction pattern) {
    return actual.kind == pattern.kind && (pattern.kind == CE_LR_SHIFT || actual.value == pattern.value);
}

static bool apply_policies(const CeGrammar *g, CeLR *out, CeError *error) {
    for (size_t i = 0; i < out->states*out->symbols; ++i) {
        CeActionCell *cell = &out->cells[i]; cell->policy = -1; cell->selected = 0;
        if (cell->len < 2) continue;
        ++out->ambiguities; cell->selected = -1;
        for (size_t preferred = 0; preferred < cell->len; ++preferred) {
            int owner = -1; bool admitted = true;
            for (size_t alternative = 0; alternative < cell->len; ++alternative) {
                if (alternative == preferred) continue;
                int found = -1;
                for (size_t j = 0; j < g->npolicies; ++j) {
                    CeConflictPolicy policy = g->policies[j];
                    size_t terminal = policy.terminal < 0 ? g->nsymbols : (size_t)policy.terminal;
                    if (policy.terminal != -3 && i%out->symbols != terminal) continue;
                    if (cell->len != 2 && policy.next != -2) continue;
                    if (action_matches(cell->actions[preferred],policy.preferred) &&
                        action_matches(cell->actions[alternative],policy.alternative)) {
                        if (found >= 0) return ce_error(error,0,"two declared policies match one LR action pair");
                        found = (int)j;
                    }
                }
                if (found < 0) admitted = false;
                else owner = found;
            }
            /* Every losing action needs a direct declared preference. Chains,
               cycles and undeclared alternatives never choose a winner. */
            if (admitted) {
                if (cell->selected >= 0) return ce_error(error,0,"declared policies select two LR actions");
                cell->policy = owner; cell->selected = (int)preferred;
            }
        }
        if (cell->selected < 0) ++out->conflicts;
    }
    return true;
}
void ce_lr_free(CeLR *p) {
    for (size_t i = 0; i < p->states*p->symbols; ++i) free(p->cells[i].actions);
    free(p->cells); free(p->gotos); free(p->defaults); memset(p,0,sizeof(*p));
}
bool ce_lr_prepare(const CeGrammar *g, int entry, CeLR *out, CeError *error) {
    memset(out,0,sizeof(*out));
    if (entry < 0 || (size_t)entry >= g->nsymbols || g->symbols[entry].kind != CE_NONTERMINAL)
        return ce_error(error,0,"LR entry must be a nonterminal");
    if (g->naliases) return ce_error(error,0,"contextual lexical aliases require the packed reference parser");
    Builder b = {g,entry,NULL,0,(g->nsymbols+1+63)/64,NULL};
    automaton(&b); first_sets(&b); lookaheads(&b);
    out->states = b.len; out->symbols = g->nsymbols+1; out->entry = entry;
    out->cells = calloc(out->states*out->symbols,sizeof(*out->cells));
    out->gotos = ce_resize(NULL,out->states*g->nsymbols,sizeof(*out->gotos));
    if (!out->cells) { fputs("cesta: out of memory\n",stderr); exit(2); }
    for (size_t i = 0; i < b.len; ++i) {
        LRState *s = &b.states[i]; out->items += s->len;
        memcpy(out->gotos+i*g->nsymbols,s->edges,g->nsymbols*sizeof(*s->edges));
        for (size_t symbol = 0; symbol < g->nsymbols; ++symbol)
            if (g->symbols[symbol].kind != CE_NONTERMINAL && s->edges[symbol] >= 0)
                add_action(&out->cells[i*out->symbols+symbol],CE_LR_SHIFT,s->edges[symbol]);
        for (size_t j = 0; j < s->len; ++j) {
            LRItem item = s->items[j];
            if (item.dot != length(&b,item.production)) continue;
            for (size_t symbol = 0; symbol <= g->nsymbols; ++symbol) if (contains(item.lookahead,symbol))
                add_action(&out->cells[i*out->symbols+symbol],item.production < 0 ? CE_LR_ACCEPT : CE_LR_REDUCE,item.production);
        }
    }
    bool ok = apply_policies(g,out,error);
    out->defaults = ce_resize(NULL,out->states,sizeof(*out->defaults));
    for (size_t state = 0; state < out->states; ++state) {
        int production = -1; bool pure = true;
        for (size_t symbol = 0; symbol < out->symbols; ++symbol) {
            CeActionCell cell = out->cells[state*out->symbols+symbol];
            if (!cell.len) continue;
            if (cell.len != 1 || cell.actions[0].kind != CE_LR_REDUCE) { pure = false; break; }
            if (production >= 0 && production != cell.actions[0].value) { pure = false; break; }
            production = cell.actions[0].value;
        }
        out->defaults[state] = pure ? production : -1;
    }
    for (size_t i = 0; i < b.len; ++i) {
        for (size_t j = 0; j < b.states[i].len; ++j) free(b.states[i].items[j].lookahead);
        free(b.states[i].items); free(b.states[i].edges);
    }
    free(b.states); free(b.first); return ok;
}

void ce_lr_conflicts(const CeGrammar *g, const CeLR *p, FILE *file) {
    for (size_t state = 0; state < p->states; ++state) for (size_t symbol = 0; symbol < p->symbols; ++symbol) {
        CeActionCell cell = p->cells[state*p->symbols+symbol];
        if (cell.len < 2) continue;
        fprintf(file,"state %zu, ",state);
        if (symbol == g->nsymbols) fputs("EOF",file); else ce_quote(file,g->symbols[symbol].name);
        for (size_t i = 0; i < cell.len; ++i) {
            CeAction a = cell.actions[i];
            if (a.kind == CE_LR_SHIFT) fprintf(file,"; shift %d",a.value);
            else if (a.kind == CE_LR_ACCEPT) fputs("; accept",file);
            else {
                const CeProduction *rule = &g->productions[a.value];
                fputs("; reduce ",file); ce_quote(file,rule->label);
                fputc('.',file); ce_quote(file,g->symbols[rule->lhs].name); fputs(" ::=",file);
                for (size_t j = 0; j < rule->len; ++j) { fputc(' ',file); ce_quote(file,g->symbols[rule->rhs[j]].name); }
            }
        }
        if (cell.policy >= 0) {
            fprintf(file,"; declared policy %d selects action %d",cell.policy,cell.selected);
            CeConflictPolicy policy = g->policies[cell.policy];
            if (policy.next != -2) {
                fputs(" when next is ",file);
                if (policy.next < 0) fputs("EOF",file); else ce_quote(file,g->symbols[policy.next].name);
                fprintf(file,", otherwise action %d",1-cell.selected);
            }
        } else fputs("; UNRESOLVED",file);
        fputc('\n',file);
    }
}

typedef struct { int state, node; } StackEntry;
static int fresh_node(CeForest *f, unsigned kind, int symbol, int production,
                      size_t dot, size_t begin, size_t end, CeText spelling,
                      int left, int right, bool packed) {
    if (f->len == f->capacity) {
        f->capacity = f->capacity ? f->capacity*2 : 64;
        f->nodes = ce_resize(f->nodes,f->capacity,sizeof(*f->nodes));
    }
    int id = (int)f->len++;
    CePacked *alternative = packed ? ce_resize(NULL,1,sizeof(*alternative)) : NULL;
    if (packed) *alternative = (CePacked){production,left,right};
    f->nodes[id] = (CeForestNode){kind,symbol,production,dot,begin,end,alternative,packed ? 1u : 0u,spelling};
    return id;
}

bool ce_lr_parse(const CeGrammar *g, const CeLR *p, const CeTokens *tokens, CeForest *out, CeError *error) {
    memset(out,0,sizeof(*out)); out->root = -1;
    if (p->conflicts) return ce_error(error,0,"LR table has %zu unresolved conflict cells; no implicit priority is applied",p->conflicts);
    size_t capacity = 64;
    StackEntry *stack = ce_resize(NULL,capacity,sizeof(*stack));
    size_t len = 1, pos = 0; stack[0] = (StackEntry){0,-1};
    bool ok = false, marker = false;
    CeScope *scope = g->c_scope ? ce_scope_new(g,out) : NULL;
    for (;;) {
        int terminal = -1;
        int default_rule = scope ? p->defaults[stack[len-1].state] : -1;
        CeAction action;
        if (default_rule >= 0) action = (CeAction){CE_LR_REDUCE,default_rule};
        else {
            terminal = marker ? (ce_scope_typedef(scope,tokens->data[pos-1].text) ? g->typedef_terminal : g->variable_terminal) :
                pos == tokens->len ? (int)g->nsymbols : tokens->data[pos].symbol;
            CeActionCell cell = p->cells[(size_t)stack[len-1].state*p->symbols+(size_t)terminal];
            if (!cell.len) {
                size_t offset = marker ? tokens->data[pos-1].offset : pos < tokens->len ? tokens->data[pos].offset :
                    tokens->len ? tokens->data[tokens->len-1].offset+tokens->data[tokens->len-1].text.len : 0;
                char expected[256] = {0}; size_t used = 0;
                for (size_t i = 0; i < p->symbols; ++i) {
                    if (!p->cells[(size_t)stack[len-1].state*p->symbols+i].len) continue;
                    CeText name = i < g->nsymbols ? g->symbols[i].name : (CeText){(unsigned char *)"EOF",3};
                    if (used+name.len+(used ? 2 : 0)+1 >= sizeof(expected)) break;
                    if (used) { memcpy(expected+used,", ",2); used += 2; }
                    memcpy(expected+used,name.data,name.len); used += name.len; expected[used] = 0;
                }
                ce_error(error,offset,"LR syntax rejected after %zu of %zu tokens: unexpected %.*s; expected %s",pos,tokens->len,
                    terminal < (int)g->nsymbols ? (int)g->symbols[terminal].name.len : 3,
                    terminal < (int)g->nsymbols ? (const char *)g->symbols[terminal].name.data : "EOF",expected); break;
            }
            int selected = cell.selected;
            if (cell.policy >= 0) {
                CeConflictPolicy policy = g->policies[cell.policy];
                int next = marker ? (pos >= tokens->len ? -1 : tokens->data[pos].symbol) : pos+1 >= tokens->len ? -1 : tokens->data[pos+1].symbol;
                if (policy.next != -2 && next != policy.next) selected = 1-selected;
            }
            action = cell.actions[selected];
        }
        if (action.kind == CE_LR_SHIFT) {
            CeToken token = marker ? (CeToken){terminal,{NULL,0},tokens->data[pos-1].offset} : tokens->data[pos];
            int id = fresh_node(out,CE_FOREST_SYMBOL,terminal,-1,0,pos,pos+(marker ? 0 : 1),token.text,-1,-1,false);
            if (len == capacity) { capacity *= 2; stack = ce_resize(stack,capacity,sizeof(*stack)); }
            stack[len++] = (StackEntry){action.value,id};
            if (marker) marker = false;
            else { ++pos; marker = scope && terminal == g->name_token; }
        } else if (action.kind == CE_LR_REDUCE) {
            const CeProduction *rule = &g->productions[action.value];
            bool hook_ok = true;
            for (size_t i = 0; i < g->nhooks; ++i) if (g->hooks[i].production == action.value && !g->hooks[i].after_reduce) {
                int value = -1;
                for (size_t j = len; j > 1; --j) {
                    int candidate = stack[j-1].node;
                    if (!g->symbols[out->nodes[candidate].symbol].erased) { value = candidate; break; }
                }
                if (!ce_scope_hook(scope,g->hooks[i].action,value,error)) { hook_ok = false; break; }
            }
            if (!hook_ok) break;
            size_t begin = rule->len ? out->nodes[stack[len-rule->len].node].begin : pos;
            int prefix = fresh_node(out,CE_FOREST_PREFIX,-1,action.value,0,begin,begin,(CeText){NULL,0},-1,-1,true);
            for (size_t i = 0; i < rule->len; ++i) {
                int child = stack[len-rule->len+i].node;
                prefix = fresh_node(out,CE_FOREST_PREFIX,-1,action.value,i+1,begin,out->nodes[child].end,(CeText){NULL,0},prefix,child,true);
            }
            int id = fresh_node(out,CE_FOREST_SYMBOL,rule->lhs,-1,0,begin,pos,(CeText){NULL,0},prefix,-1,true);
            out->nodes[id].alternatives[0].production = action.value;
            for (size_t i = 0; i < g->nhooks; ++i) if (g->hooks[i].production == action.value && g->hooks[i].after_reduce)
                if (!ce_scope_hook(scope,g->hooks[i].action,id,error)) { hook_ok = false; break; }
            if (!hook_ok) break;
            len -= rule->len;
            int target = p->gotos[(size_t)stack[len-1].state*g->nsymbols+(size_t)rule->lhs];
            if (target < 0) { ce_error(error,0,"missing LR goto after reduction"); break; }
            if (len == capacity) { capacity *= 2; stack = ce_resize(stack,capacity,sizeof(*stack)); }
            stack[len++] = (StackEntry){target,id};
        } else { out->root = stack[len-1].node; ok = ce_scope_closed(scope,error); break; }
        ++out->descriptors;
    }
    ce_scope_free(scope); free(stack); return ok;
}

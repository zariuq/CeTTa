#include "cesta.h"

#include <stdlib.h>
#include <string.h>

typedef struct { int production, node; size_t dot, origin; } Item;
typedef struct { Item *items; size_t len, cursor; } Chart;
typedef struct {
    const CeGrammar *grammar;
    CeForest *forest;
    Chart *charts;
} Earley;

void ce_forest_free(CeForest *f) {
    for (size_t i = 0; i < f->len; ++i) free(f->nodes[i].alternatives);
    free(f->nodes);
    memset(f, 0, sizeof(*f));
    f->root = -1;
}

static int node(Earley *e, unsigned kind, int symbol, int production,
                size_t dot, size_t begin, size_t end) {
    CeForest *f = e->forest;
    for (size_t i = 0; i < f->len; ++i) {
        CeForestNode n = f->nodes[i];
        if (n.kind == kind && n.symbol == symbol && n.production == production &&
            n.dot == dot && n.begin == begin && n.end == end) return (int)i;
    }
    if (f->len == f->capacity) {
        f->capacity = f->capacity ? f->capacity*2 : 64;
        f->nodes = ce_resize(f->nodes, f->capacity, sizeof(*f->nodes));
    }
    int id = (int)f->len++;
    f->nodes[id] = (CeForestNode){kind, symbol, production, dot, begin, end, NULL, 0, {NULL,0}};
    return id;
}
static void pack(Earley *e, int id, int production, int left, int right) {
    CeForestNode *n = &e->forest->nodes[id];
    for (size_t i = 0; i < n->len; ++i) {
        CePacked p = n->alternatives[i];
        if (p.production == production && p.left == left && p.right == right) return;
    }
    n->alternatives = ce_resize(n->alternatives, n->len+1, sizeof(*n->alternatives));
    n->alternatives[n->len++] = (CePacked){production,left,right};
}
static int add(Earley *e, size_t end, int production, size_t dot,
               size_t origin, int left, int right) {
    Chart *chart = &e->charts[end];
    int id = -1;
    for (size_t i = 0; i < chart->len; ++i) {
        Item old = chart->items[i];
        if (old.production == production && old.dot == dot && old.origin == origin) { id = old.node; break; }
    }
    if (id < 0) {
        id = node(e, CE_FOREST_PREFIX, -1, production, dot, origin, end);
        chart->items = ce_resize(chart->items, chart->len+1, sizeof(*chart->items));
        chart->items[chart->len++] = (Item){production,id,dot,origin};
        ++e->forest->descriptors;
    }
    pack(e, id, production, left, right);
    return id;
}

bool ce_parse(const CeGrammar *g, const CeTokens *tokens, int entry,
              CeForest *out, CeError *error) {
    memset(out, 0, sizeof(*out)); out->root = -1;
    Chart *charts = calloc(tokens->len+1, sizeof(*charts));
    if (!charts) { fputs("cesta: out of memory\n", stderr); exit(2); }
    Earley e = {g,out,charts};
    for (size_t p = 0; p < g->nproductions; ++p)
        if (g->productions[p].lhs == entry && !g->productions[p].internal) add(&e, 0, (int)p, 0, 0, -1, -1);
    size_t furthest = 0;
    for (size_t end = 0; end <= tokens->len; ++end) {
        if (charts[end].len) furthest = end;
        while (charts[end].cursor < charts[end].len) {
            Item item = charts[end].items[charts[end].cursor++];
            const CeProduction *p = &g->productions[item.production];
            if (item.dot == p->len) {
                int complete = node(&e, CE_FOREST_SYMBOL, p->lhs, -1, 0, item.origin, end);
                pack(&e, complete, item.production, item.node, -1);
                if (p->lhs == entry && !item.origin && end == tokens->len) out->root = complete;
                for (size_t i = 0; i < charts[item.origin].len; ++i) {
                    Item waiting = charts[item.origin].items[i];
                    const CeProduction *wp = &g->productions[waiting.production];
                    if (waiting.dot < wp->len && wp->rhs[waiting.dot] == p->lhs)
                        add(&e, end, waiting.production, waiting.dot+1, waiting.origin, waiting.node, complete);
                }
            } else {
                int symbol = p->rhs[item.dot];
                if (g->symbols[symbol].kind == CE_NONTERMINAL) {
                    for (size_t q = 0; q < g->nproductions; ++q)
                        if (g->productions[q].lhs == symbol && !g->productions[q].internal)
                            add(&e, end, (int)q, 0, end, -1, -1);
                    /* Nullable completions may have been processed before this waiter. */
                    for (size_t j = 0; j < charts[end].len; ++j) {
                        Item finished = charts[end].items[j];
                        const CeProduction *fp = &g->productions[finished.production];
                        if (finished.origin == end && finished.dot == fp->len && fp->lhs == symbol) {
                            int complete = node(&e, CE_FOREST_SYMBOL, symbol, -1, 0, end, end);
                            pack(&e, complete, finished.production, finished.node, -1);
                            add(&e, end, item.production, item.dot+1, item.origin, item.node, complete);
                        }
                    }
                } else if (g->c_scope && (symbol == g->variable_terminal || symbol == g->typedef_terminal)) {
                    /* The raw syntax forest keeps both classifications; scoped LR selects one. */
                    int token = node(&e, CE_FOREST_SYMBOL, symbol, -1, 0, end, end);
                    add(&e, end, item.production, item.dot+1, item.origin, item.node, token);
                } else if (end < tokens->len && ce_token_matches(g, item.production, symbol, &tokens->data[end])) {
                    int token = node(&e, CE_FOREST_SYMBOL, symbol, -1, 0, end, end+1);
                    out->nodes[token].spelling = tokens->data[end].text;
                    add(&e, end+1, item.production, item.dot+1, item.origin, item.node, token);
                }
            }
        }
    }
    for (size_t i = 0; i <= tokens->len; ++i) free(charts[i].items);
    free(charts);
    if (out->root < 0) {
        size_t offset = furthest < tokens->len ? tokens->data[furthest].offset :
                        tokens->len ? tokens->data[tokens->len-1].offset+tokens->data[tokens->len-1].text.len : 0;
        return ce_error(error, offset, "syntax rejected after %zu of %zu tokens", furthest, tokens->len);
    }
    return true;
}

unsigned ce_forest_count(const CeForest *f, unsigned ceiling) {
    if (!ceiling || f->root < 0) return 0;
    unsigned *memo = calloc(f->len, sizeof(*memo));
    unsigned char *active = calloc(f->len, 1);
    if (!memo || !active) { fputs("cesta: out of memory\n", stderr); exit(2); }
    typedef struct { int node; size_t alternative; unsigned total, left, stage; } Frame;
    size_t capacity = 64;
    Frame *stack = ce_resize(NULL,capacity,sizeof(*stack)); size_t len = 1;
    stack[0] = (Frame){f->root,0,0,0,0}; active[f->root] = 1;
    while (len) {
        Frame *frame = &stack[len-1]; const CeForestNode *node = &f->nodes[frame->node];
        if (!node->len || frame->alternative == node->len || frame->total == ceiling) {
            memo[frame->node] = node->len ? frame->total : 1;
            active[frame->node] = 0; --len; continue;
        }
        CePacked alt = node->alternatives[frame->alternative];
        int child = frame->stage ? alt.right : alt.left;
        if (child >= 0 && !memo[child] && !active[child]) {
            if (len == capacity) { capacity *= 2; stack = ce_resize(stack,capacity,sizeof(*stack)); }
            stack[len++] = (Frame){child,0,0,0,0}; active[child] = 1; continue;
        }
        unsigned value = child < 0 ? 1 : memo[child] ? memo[child] : ceiling;
        if (!frame->stage) { frame->left = value; frame->stage = 1; }
        else {
            unsigned product = frame->left > ceiling/value ? ceiling : frame->left*value;
            frame->total = product >= ceiling-frame->total ? ceiling : frame->total+product;
            ++frame->alternative; frame->stage = 0;
        }
    }
    unsigned result = memo[f->root]; free(stack);
    free(memo); free(active);
    return result;
}

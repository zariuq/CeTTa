#include "cesta.h"

#include <stdlib.h>
#include <string.h>

typedef struct { int state, symbol; } Acceptance;
typedef struct {
    CeNfaState *nfa;
    size_t nnfa, words;
    Acceptance *accepts;
    size_t naccepts;
    uint64_t **subsets;
    CeDFA *dfa;
    int *stack;
} Builder;

static bool has(const uint64_t *set, size_t bit) {
    return (set[bit/64] & (UINT64_C(1) << (bit%64))) != 0;
}

static void closure(Builder *b, uint64_t *set, int state) {
    if (has(set,(size_t)state)) return;
    size_t len = 0;
    set[state/64] |= UINT64_C(1) << (state%64); b->stack[len++] = state;
    while (len) {
        CeNfaState n = b->nfa[b->stack[--len]];
        if (!n.epsilon) continue;
        for (size_t i = 0; i < 2; ++i) if (n.out[i] >= 0 && !has(set,(size_t)n.out[i])) {
            set[n.out[i]/64] |= UINT64_C(1) << (n.out[i]%64);
            b->stack[len++] = n.out[i];
        }
    }
}

static int intern(Builder *b, const uint64_t *set) {
    bool empty = true;
    for (size_t i = 0; i < b->words; ++i) empty &= set[i] == 0;
    if (empty) return -1;
    for (size_t i = 0; i < b->dfa->len; ++i)
        if (!memcmp(set,b->subsets[i],b->words*sizeof(*set))) return (int)i;
    size_t id = b->dfa->len++;
    b->dfa->states = ce_resize(b->dfa->states,b->dfa->len,sizeof(*b->dfa->states));
    b->subsets = ce_resize(b->subsets,b->dfa->len,sizeof(*b->subsets));
    b->subsets[id] = ce_resize(NULL,b->words,sizeof(*set));
    memcpy(b->subsets[id],set,b->words*sizeof(*set));
    CeDfaState *state = &b->dfa->states[id]; state->symbol = -1;
    for (size_t c = 0; c < 256; ++c) state->transitions[c] = -1;
    for (size_t i = 0; i < b->naccepts; ++i) if (has(set,(size_t)b->accepts[i].state)) {
        state->symbol = b->accepts[i].symbol; break;
    }
    return (int)id;
}

bool ce_dfa_prepare(const CeGrammar *g, CeDFA *out, CeError *error) {
    memset(out,0,sizeof(*out));
    Builder b = {.nnfa=g->nnfa,.dfa=out};
    for (size_t i = 0; i < g->nsymbols; ++i) if (g->symbols[i].kind == CE_LITERAL && !g->symbols[i].virtual_token && !g->symbols[i].identifier_spelling)
        b.nnfa += g->symbols[i].name.len+1;
    if (!b.nnfa) return ce_error(error,0,"lexer has no terminals");
    b.nfa = ce_resize(NULL,b.nnfa,sizeof(*b.nfa));
    if (g->nnfa) memcpy(b.nfa,g->nfa,g->nnfa*sizeof(*g->nfa));
    b.words = (b.nnfa+63)/64;
    b.stack = ce_resize(NULL,b.nnfa,sizeof(*b.stack));
    uint64_t *start = calloc(b.words,sizeof(*start));
    uint64_t *next = calloc(b.words,sizeof(*next));
    if (!start || !next) { fputs("cesta: out of memory\n",stderr); exit(2); }
    size_t used = g->nnfa;
    for (size_t i = 0; i < g->nsymbols; ++i) {
        CeSymbol s = g->symbols[i];
        if (s.kind != CE_LITERAL || s.virtual_token || s.identifier_spelling || !s.name.len) continue;
        size_t begin = used;
        for (size_t j = 0; j <= s.name.len; ++j) {
            CeNfaState n = {{{0}}, {-1,-1}, j == s.name.len};
            if (j < s.name.len) {
                unsigned c = s.name.data[j]; n.bytes.word[c/64] |= UINT64_C(1) << (c%64);
                n.out[0] = (int)used+1;
            }
            b.nfa[used++] = n;
        }
        closure(&b,start,(int)begin);
        b.accepts = ce_resize(b.accepts,b.naccepts+1,sizeof(*b.accepts));
        b.accepts[b.naccepts++] = (Acceptance){(int)used-1,(int)i};
    }
    for (size_t i = 0; i < g->nlex; ++i) {
        closure(&b,start,g->lex[i].start);
        b.accepts = ce_resize(b.accepts,b.naccepts+1,sizeof(*b.accepts));
        b.accepts[b.naccepts++] = (Acceptance){g->lex[i].end,g->lex[i].symbol};
    }
    (void)intern(&b,start);
    for (size_t state = 0; state < out->len; ++state) for (unsigned c = 0; c < 256; ++c) {
        memset(next,0,b.words*sizeof(*next));
        for (size_t i = 0; i < used; ++i) {
            CeNfaState n = b.nfa[i];
            if (has(b.subsets[state],i) && !n.epsilon && (n.bytes.word[c/64] & (UINT64_C(1) << (c%64))))
                closure(&b,next,n.out[0]);
        }
        int target = intern(&b,next);
        out->states[state].transitions[c] = target;
    }
    for (size_t i = 0; i < out->len; ++i) free(b.subsets[i]);
    free(b.subsets); free(b.nfa); free(b.accepts); free(b.stack); free(start); free(next);
    return true;
}

void ce_dfa_free(CeDFA *dfa) { free(dfa->states); memset(dfa,0,sizeof(*dfa)); }

static bool begins(CeText source, size_t offset, CeText prefix) {
    return prefix.len <= source.len-offset && (!prefix.len || !memcmp(source.data+offset,prefix.data,prefix.len));
}

bool ce_dfa_lex(const CeGrammar *g, const CeDFA *dfa, CeText source, CeTokens *out, CeError *error) {
    memset(out,0,sizeof(*out)); size_t capacity = 0;
    for (size_t pos = 0; pos < source.len;) {
        unsigned c = source.data[pos];
        if (c == ' ' || (c >= 9 && c <= 13)) { ++pos; continue; }
        bool comment = false;
        for (size_t i = 0; i < g->ncomments; ++i) {
            CeComment def = g->comments[i];
            if (!def.open.len || !begins(source,pos,def.open)) continue;
            size_t begin = pos; pos += def.open.len;
            if (!def.close.len) while (pos < source.len && source.data[pos] != '\n') ++pos;
            else {
                while (pos < source.len && !begins(source,pos,def.close)) ++pos;
                if (pos == source.len) { ce_tokens_free(out); return ce_error(error,begin,"unterminated source comment"); }
                pos += def.close.len;
            }
            comment = true; break;
        }
        if (comment) continue;
        size_t end = pos; int symbol = -1, state = 0;
        for (size_t i = pos; i < source.len; ++i) {
            state = dfa->states[state].transitions[source.data[i]];
            if (state < 0) break;
            if (dfa->states[state].symbol >= 0) { symbol = dfa->states[state].symbol; end = i+1; }
        }
        if (symbol < 0) { ce_tokens_free(out); return ce_error(error,pos,"no lexical rule accepts byte 0x%02x",source.data[pos]); }
        if (out->len == capacity) { capacity = capacity ? capacity*2 : 256; out->data = ce_resize(out->data,capacity,sizeof(*out->data)); }
        out->data[out->len++] = (CeToken){symbol,{source.data+pos,end-pos},pos}; pos = end;
    }
    ce_lex_rewrite(g,out); return true;
}

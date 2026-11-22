#include "../native/kernel/pair.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t rng=UINT64_C(0x3278b21da);
static uint64_t random_word(void) { rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng; }
static CettaKernelPairPlan reference(const CettaKernelPairFacts *f) {
    const uint64_t *v=f->word;CettaKernelPairPlan r={0};
    uint64_t l=v[NP_F_LEFT_KIND],q=v[NP_F_RIGHT_KIND];
    if (v[NP_F_PHASE]==2) { r.action=v[NP_F_EQUAL] ? NP_A_SKIP : NP_A_REJECT;return r; }
    bool same=v[NP_F_LEFT_VARIABLE]==v[NP_F_RIGHT_VARIABLE] && v[NP_F_LEFT_SCOPE]==v[NP_F_RIGHT_SCOPE];
    if (!v[NP_F_PHASE]) {
        if (!v[NP_F_WILDCARD] && v[NP_F_ONE_WAY] && l==NP_K_VARIABLE) { r.action=NP_A_BIND_LEFT;return r; }
        r.action=v[NP_F_WILDCARD] || (l==NP_K_VARIABLE && same && v[NP_F_ACYCLIC] &&
            v[NP_F_SELF_VARIABLE] && v[NP_F_LEFT_ADDRESS]==v[NP_F_RIGHT_ADDRESS]) ? NP_A_SKIP : NP_A_ASK_CLOSED;
        return r;
    }
    if (v[NP_F_ONE_WAY]) {
        if (l==NP_K_VARIABLE) { r.action=NP_A_BIND_LEFT;return r; }
        if (l!=q) { r.action=NP_A_REJECT;return r; }
    }
    if (v[NP_F_KNOWN]) { r.action=v[NP_F_EQUAL] ? NP_A_SKIP : NP_A_REJECT;return r; }
    if (l==NP_K_VARIABLE) {
        r.action=v[NP_F_LEFT_BOUND] ? NP_A_DEREFERENCE_LEFT : q==NP_K_VARIABLE && v[NP_F_RIGHT_BOUND] ?
            NP_A_DEREFERENCE_RIGHT : q==NP_K_VARIABLE && same ? NP_A_SKIP :
            q==NP_K_VARIABLE && v[NP_F_PREFER_RIGHT] ? NP_A_BIND_RIGHT : NP_A_BIND_LEFT;
        return r;
    }
    if (q==NP_K_VARIABLE) { r.action=v[NP_F_RIGHT_BOUND] ? NP_A_DEREFERENCE_RIGHT : NP_A_BIND_RIGHT;return r; }
    if (l==NP_K_SYMBOL && q==NP_K_SYMBOL) {
        r.action=v[NP_F_LEFT_SYMBOL]==v[NP_F_RIGHT_SYMBOL] ? NP_A_SKIP : NP_A_REJECT;return r;
    }
    if (l==NP_K_GROUNDED && q==NP_K_GROUNDED) { r.action=NP_A_ASK_GROUNDED;return r; }
    r.action=NP_A_REJECT;
    if (v[NP_F_LEFT_CLASS]!=v[NP_F_RIGHT_CLASS] || l!=NP_K_EXPRESSION || q!=NP_K_EXPRESSION) return r;
    uint64_t a=v[NP_F_LEFT_LENGTH],b=v[NP_F_RIGHT_LENGTH];
    if (!v[NP_F_LEFT_LIST] && !v[NP_F_RIGHT_LIST]) {
        if (a==b) { r.action=NP_A_EXPRESSION;r.prefix=a; }return r;
    }
    if (!v[NP_F_LEFT_LIST] || !v[NP_F_RIGHT_LIST]) return r;
    bool lo=v[NP_F_LEFT_OPEN],ro=v[NP_F_RIGHT_OPEN];
    if (a<1u+lo || b<1u+ro) return r;
    a-=1u+lo;b-=1u+ro;r.prefix=a<b ? a : b;
    if (a==b) r.rest=lo ? ro ? NP_R_BOTH : NP_R_LEFT_EMPTY_RIGHT : ro ? NP_R_EMPTY_LEFT_RIGHT : NP_R_NONE;
    else if (a<b) { if (!lo) return r;r.rest=NP_R_RIGHT_SUFFIX;r.from=1+a;r.count=b-a; }
    else { if (!ro) return r;r.rest=NP_R_LEFT_SUFFIX;r.from=1+b;r.count=a-b; }
    r.action=NP_A_LIST;return r;
}
static void check(const CettaKernelPairProgram *p,CettaKernelPairFacts f,size_t *count) {
    CettaKernelPairFacts original=f;CettaKernelPairPlan n,want=reference(&f);
    assert(cetta_kernel_pair_call(p,&f,100,7,&n));
    assert(!memcmp(&f,&original,sizeof f) && n.fuel_used==8);
    if (n.action!=want.action) fprintf(stderr,"pair phase %llu kinds %llu/%llu: %llu != %llu\n",
        (unsigned long long)f.word[0],(unsigned long long)f.word[NP_F_LEFT_KIND],
        (unsigned long long)f.word[NP_F_RIGHT_KIND],(unsigned long long)n.action,(unsigned long long)want.action);
    assert(n.action==want.action);
    if (n.action==NP_A_EXPRESSION || n.action==NP_A_LIST)
        assert(n.prefix==want.prefix && n.rest==want.rest && n.from==want.from && n.count==want.count);
    ++*count;
}
int main(void) {
    char error[256];CettaKernelPairProgram *p=cetta_kernel_pair_program(error,sizeof error);
    assert(p && cetta_kernel_pair_bytes(p)>4);size_t cases=0;
    for (unsigned direction=0;direction<2;++direction) for (unsigned phase=0;phase<3;++phase) for (unsigned l=0;l<4;++l) for (unsigned r=0;r<4;++r)
      for (unsigned bits=0;bits<512;++bits) {
        CettaKernelPairFacts f={0};f.word[NP_F_PHASE]=phase;f.word[NP_F_LEFT_KIND]=l;f.word[NP_F_RIGHT_KIND]=r;
        f.word[NP_F_ONE_WAY]=direction;
        f.word[NP_F_WILDCARD]=bits&1;f.word[NP_F_ACYCLIC]=(bits>>1)&1;
        f.word[NP_F_SELF_VARIABLE]=(bits>>2)&1;f.word[NP_F_PREFER_RIGHT]=(bits>>3)&1;
        f.word[NP_F_KNOWN]=(bits>>4)&1;f.word[NP_F_EQUAL]=(bits>>5)&1;
        f.word[NP_F_LEFT_BOUND]=(bits>>6)&1;f.word[NP_F_RIGHT_BOUND]=(bits>>7)&1;
        f.word[NP_F_LEFT_ADDRESS]=100;f.word[NP_F_RIGHT_ADDRESS]=bits&256 ? 100 : 200;
        f.word[NP_F_LEFT_VARIABLE]=UINT64_MAX;f.word[NP_F_RIGHT_VARIABLE]=UINT64_MAX;
        f.word[NP_F_LEFT_SCOPE]=17;f.word[NP_F_RIGHT_SCOPE]=bits&64 ? 18 : 17;
        f.word[NP_F_LEFT_SYMBOL]=bits&1;f.word[NP_F_RIGHT_SYMBOL]=(bits>>3)&1;
        f.word[NP_F_LEFT_LENGTH]=3;f.word[NP_F_RIGHT_LENGTH]=bits&1 ? 3 : 4;
        check(p,f,&cases);
    }
    for (unsigned lo=0;lo<2;++lo) for (unsigned ro=0;ro<2;++ro)
      for (unsigned l=0;l<7;++l) for (unsigned r=0;r<7;++r) for (unsigned tags=0;tags<4;++tags) {
        CettaKernelPairFacts f={0};f.word[0]=1;f.word[NP_F_LEFT_KIND]=f.word[NP_F_RIGHT_KIND]=NP_K_EXPRESSION;
        f.word[NP_F_LEFT_LIST]=tags&1;f.word[NP_F_RIGHT_LIST]=(tags>>1)&1;
        f.word[NP_F_LEFT_OPEN]=lo;f.word[NP_F_RIGHT_OPEN]=ro;
        f.word[NP_F_LEFT_LENGTH]=l;f.word[NP_F_RIGHT_LENGTH]=r;check(p,f,&cases);
    }
    for (unsigned i=0;i<20000;++i) {
        CettaKernelPairFacts f={0};for (size_t j=0;j<NP_FACTS;++j) f.word[j]=random_word()&1;
        f.word[0]=random_word()%3;f.word[NP_F_LEFT_KIND]=random_word()%4;f.word[NP_F_RIGHT_KIND]=random_word()%4;
        f.word[NP_F_LEFT_VARIABLE]=random_word();f.word[NP_F_RIGHT_VARIABLE]=random_word();
        f.word[NP_F_LEFT_SCOPE]=random_word();f.word[NP_F_RIGHT_SCOPE]=random_word();
        f.word[NP_F_LEFT_LENGTH]=random_word()%16;f.word[NP_F_RIGHT_LENGTH]=random_word()%16;
        check(p,f,&cases);
    }
    const uint64_t words[]={0,1,UINT64_MAX,UINT64_C(0x8000000000000000),UINT64_C(0xffffffff00000000)};
    for (size_t i=0;i<sizeof words/sizeof *words;++i) for (size_t j=0;j<sizeof words/sizeof *words;++j) {
        bool equal=false;uint64_t fuel=0;
        assert(cetta_kernel_pair_word_call(p,words[i],words[j],100,7,&equal,&fuel) && equal==(words[i]==words[j]) && fuel==8);
    }
    bool equal=true;uint64_t word_fuel=99;
    assert(!cetta_kernel_pair_word_call(p,0,1,0,0,&equal,&word_fuel) && equal && word_fuel==99);
    CettaKernelPairFacts f={0};CettaKernelPairPlan guard={77,78,79,80,81,82},saved=guard;
    assert(!cetta_kernel_pair_call(p,&f,7,7,&guard) && !memcmp(&guard,&saved,sizeof guard));
    f.word[0]=3;assert(!cetta_kernel_pair_call(p,&f,100,0,&guard) && !memcmp(&guard,&saved,sizeof guard));
    cetta_kernel_pair_program_free(p);
    printf("native matching policy: %zu x86 decisions against the independent C oracle; scopes, policy phases, bound slots, classes, list rests, guarded outputs and fuel passed\n",cases);
    return 0;
}

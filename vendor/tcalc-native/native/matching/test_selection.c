#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint64_t nm_selection_advance(size_t *,size_t,size_t);
extern uint64_t nm_selection_allowed(const size_t *,size_t,size_t);
typedef struct { size_t before,positions[6],after; } Storage;
static const size_t guard=(size_t)UINT64_C(0x5B5B5B5B5B5B5B5B);
static size_t checks;
static void guards(const Storage *s,size_t count) {
    assert(s->before==guard && s->after==guard);
    for (size_t i=count;i<6;++i) assert(s->positions[i]==guard);
}
/* An occurrence-set oracle, independent of the pairwise native algorithm. */
static uint64_t allowed(const size_t *p,size_t count,size_t takes) {
    if (takes>count) return 2;
    unsigned taken=0;
    for (size_t i=0;i<takes;++i) {
        unsigned bit=1u<<(unsigned)p[i];
        if (taken&bit) return 0;
        taken|=bit;
    }
    for (size_t i=takes;i<count;++i) if (taken&(1u<<(unsigned)p[i])) return 0;
    return 1;
}
int main(void) {
    for (size_t count=0;count<=5;++count) for (size_t rows=1;rows<=5;++rows) {
        Storage storage;memset(&storage,0x5B,sizeof storage);
        for (size_t i=0;i<count;++i) storage.positions[i]=0;
        size_t total=1;for (size_t i=0;i<count;++i) total*=rows;
        for (size_t rank=0;rank<total;++rank) {
            size_t rest=rank;
            for (size_t i=count;i-->0;) { assert(storage.positions[i]==rest%rows);rest/=rows; }
            for (size_t takes=0;takes<=count+1;++takes) {
                Storage before=storage;
                assert(nm_selection_allowed(storage.positions,count,takes)==allowed(storage.positions,count,takes));
                assert(!memcmp(&storage,&before,sizeof storage));++checks;
            }
            assert(nm_selection_advance(storage.positions,count,rows)==(rank+1<total ? 1u : 0u));
            guards(&storage,count);++checks;
        }
        for (size_t i=0;i<count;++i) assert(!storage.positions[i]);
    }
    Storage storage;memset(&storage,0x5B,sizeof storage);storage.positions[0]=0;storage.positions[1]=4;
    Storage before=storage;
    assert(nm_selection_advance(storage.positions,2,4)==2 && !memcmp(&storage,&before,sizeof storage));
    assert(nm_selection_advance(storage.positions,2,0)==2 && !memcmp(&storage,&before,sizeof storage));
    assert(nm_selection_advance(NULL,0,0)==0 && nm_selection_allowed(NULL,0,0)==1 &&
        nm_selection_allowed(NULL,0,1)==2);
    storage.positions[0]=SIZE_MAX-2;
    assert(nm_selection_advance(storage.positions,1,SIZE_MAX)==1 && storage.positions[0]==SIZE_MAX-1);
    assert(nm_selection_advance(storage.positions,1,SIZE_MAX)==0 && !storage.positions[0]);
    storage.positions[0]=SIZE_MAX;before=storage;
    assert(nm_selection_advance(storage.positions,1,SIZE_MAX)==2 && !memcmp(&storage,&before,sizeof storage));
    printf("native selection: %zu rank/set-oracle decisions, guarded cursors, empty dimensions and 64-bit boundaries passed\n",checks);
    return 0;
}

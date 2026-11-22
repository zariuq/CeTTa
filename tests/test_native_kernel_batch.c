#include "../native/kernel/batch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t rng=UINT64_C(0xe7037ed1a0b428db);
static uint32_t random_word(void) { rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return (uint32_t)rng; }
static uint32_t source(uint32_t row,uint32_t column) { return row|(column<<8); }

/* A C caller may keep all six SysV nonvolatile registers live across a call.
 * This probe supplies distinct values, checks them after execution and then
 * restores its own caller's originals. It is present only in the test ELF. */
extern bool batch_abi_probe(const TcBatchInput *,TcBatchWork *);
__asm__(
    ".text\n.type batch_abi_probe,@function\nbatch_abi_probe:\n"
    "push %rbp\npush %rbx\npush %r12\npush %r13\npush %r14\npush %r15\nsub $8,%rsp\n"
    "mov $0x11223344,%ebp\nmov $0x22334455,%ebx\nmov $0x33445566,%r12d\n"
    "mov $0x44556677,%r13d\nmov $0x55667711,%r14d\nmov $0x66771122,%r15d\n"
    "call tc_batch_x86\nxor %eax,%eax\n"
    "cmp $0x11223344,%rbp\njne 1f\ncmp $0x22334455,%rbx\njne 1f\n"
    "cmp $0x33445566,%r12\njne 1f\ncmp $0x44556677,%r13\njne 1f\n"
    "cmp $0x55667711,%r14\njne 1f\ncmp $0x66771122,%r15\njne 1f\n"
    "mov $1,%eax\n1:\nadd $8,%rsp\npop %r15\npop %r14\npop %r13\npop %r12\npop %rbx\npop %rbp\nret\n"
    ".size batch_abi_probe,.-batch_abi_probe\n");

static size_t compare(TcBatchInput in) {
    uint32_t a[TC_BATCH_CURSOR_WORDS]={0},b[TC_BATCH_CURSOR_WORDS]={0};
    uint32_t ao[256],bo[256];size_t records=0;
    memset(ao,0xd3,sizeof ao);memset(bo,0xd3,sizeof bo);
    TcBatchWork c={a,TC_BATCH_CURSOR_WORDS,ao,in.patterns+in.plan->makes+1,0,0,0,0};
    TcBatchWork n={b,TC_BATCH_CURSOR_WORDS,bo,c.output_words,0,0,0,0};
    size_t calls=0;
    for (;;) {
        assert(tc_batch_valid(&in,&c) && tc_batch_valid(&in,&n));
        uint64_t cs=tc_batch_c(&in,&c),ns=tc_batch_x86(&in,&n);
        if (cs!=ns || c.produced!=n.produced || c.used!=n.used || c.visited!=n.visited ||
            memcmp(a,b,sizeof a) || memcmp(ao,bo,sizeof ao)) {
            fprintf(stderr,"batch disagreement call=%zu status=%llu/%llu produced=%llu/%llu fuel=%llu/%llu depth=%u/%u\n",
                calls,(unsigned long long)cs,(unsigned long long)ns,
                (unsigned long long)c.produced,(unsigned long long)n.produced,
                (unsigned long long)c.used,(unsigned long long)n.used,a[1],b[1]);
            assert(false);
        }
        ++calls;records+=c.produced;assert(calls<10000);
        if (cs==TC_BATCH_END) break;
        assert(cs==TC_BATCH_YIELD || cs==TC_BATCH_EXHAUSTED);
        if (cs==TC_BATCH_EXHAUSTED) ++in.budget;
        if (!in.quantum) { assert(!c.produced && !c.visited);in.quantum=1; }
    }
    return records;
}

int main(void) {
    assert(cetta_kernel_batch_image_valid());
    TcNode nodes[10]={0};
    for (size_t i=1;i<6;++i) nodes[i]=(TcNode){.kind=TC_SYMBOL,.ground=true};
    uint32_t items[]={1,3,1,4,2,3,1,5};
    for (size_t i=6;i<10;++i) nodes[i]=(TcNode){.kind=TC_EXPRESSION,.ground=true,.length=2,.start=(uint32_t)(2*(i-6))};
    uint32_t rows[]={6,6,7,8,9};
    FlatPlan plan={.constant={1,5},.constants=2,.guards=1,.makes=2,.made_length={2},.made_rows=1};
    plan.guard[0]=source(0,0)|(source(1,0)<<16);
    plan.make[0]=source(1,1);plan.make[1]=source(0,1);
    uint32_t stages[TC_BATCH_STAGE_WORDS*TC_BATCH_PATTERNS]={0};
    stages[TC_BATCH_LENGTH]=2;stages[TC_BATCH_HI1]=5;stages[TC_BATCH_TWIN]=UINT32_MAX;stages[TC_BATCH_GUARD_END]=1;
    TcBatchInput in={nodes,10,items,8,rows,5,&plan,stages,1,1,TC_BATCH_ONE,4096,UINT64_MAX};
    uint32_t abi_cursor[TC_BATCH_CURSOR_WORDS]={0},abi_output[16]={0};
    TcBatchWork abi={abi_cursor,TC_BATCH_CURSOR_WORDS,abi_output,16,0,0,0,0};
    assert(tc_batch_valid(&in,&abi) && batch_abi_probe(&in,&abi) && abi.status==TC_BATCH_END);
    assert(compare(in)==3);in.flags=0;assert(compare(in)==4);
    for (uint64_t quantum=0;quantum<6;++quantum) {
        in.quantum=quantum;in.budget=0;assert(compare(in)==4);
    }
    size_t checks=9;
    for (size_t specimen=0;specimen<2000;++specimen) {
        in.patterns=1+random_word()%3;in.takes=random_word()%(in.patterns+1);
        in.flags=random_word()%4;in.quantum=random_word()%9;in.budget=random_word()%12;
        plan.guards=0;plan.makes=1+random_word()%6;
        uint32_t previous=0;
        for (uint32_t k=0;k<in.patterns;++k) {
            uint32_t *s=stages+k*TC_BATCH_STAGE_WORDS;
            s[TC_BATCH_LENGTH]=2;s[TC_BATCH_LO0]=s[TC_BATCH_HI0]=0;
            s[TC_BATCH_LO1]=0;s[TC_BATCH_HI1]=5;
            s[TC_BATCH_TWIN]=k && random_word()%2 ? k-1 : UINT32_MAX;
            s[TC_BATCH_GUARD_BEGIN]=previous;
            uint32_t guards=random_word()%3;
            for (uint32_t g=0;g<guards;++g) {
                uint32_t right=random_word()%(k+2),col=random_word()%2;
                if (right==k+1) right=(uint32_t)in.patterns;
                plan.guard[plan.guards++]=source(k,random_word()%2)|(source(right,col)<<16);
            }
            previous=plan.guards;s[TC_BATCH_GUARD_END]=previous;
        }
        for (uint32_t m=0;m<plan.makes;++m) plan.make[m]=source(random_word()%(in.patterns+1),random_word()%2);
        (void)compare(in);++checks;
    }
    /* Empty conjunction, constants-only output and an empty output. */
    in.patterns=in.takes=0;in.flags=0;in.quantum=1;in.budget=100;plan.guards=0;plan.makes=1;plan.make[0]=source(0,1);
    assert(compare(in)==1);plan.makes=0;assert(compare(in)==1);checks+=2;
    /* Admission rejects corrupt dependency, alias and size records unchanged. */
    uint32_t cursor[TC_BATCH_CURSOR_WORDS]={0},output[16]={0};TcBatchWork w={cursor,TC_BATCH_CURSOR_WORDS,output,16,0,0,0,0};
    assert(tc_batch_valid(&in,&w));w.output=cursor;assert(!tc_batch_valid(&in,&w));w.output=output;
    w.output_words=UINT64_MAX;assert(!tc_batch_valid(&in,&w));w.output_words=16;
    in.patterns=1;in.takes=0;stages[TC_BATCH_TWIN]=0;assert(!tc_batch_valid(&in,&w));stages[TC_BATCH_TWIN]=UINT32_MAX;
    stages[TC_BATCH_GUARD_BEGIN]=1;stages[TC_BATCH_GUARD_END]=1;plan.guards=1;assert(!tc_batch_valid(&in,&w));
    checks+=4;
    stages[TC_BATCH_GUARD_BEGIN]=0;plan.guard[0]=source(0,0)|(source(1,0)<<16);
    plan.makes=2;plan.make[0]=source(1,1);plan.make[1]=source(0,1);
    cursor[0]=1;cursor[1]=1;cursor[TC_BATCH_SELECTED]=0;cursor[TC_BATCH_NEXT]=1;
    assert(tc_batch_valid(&in,&w));
    cursor[TC_BATCH_NEXT]=6;assert(!tc_batch_valid(&in,&w));cursor[TC_BATCH_NEXT]=1;
    cursor[TC_BATCH_SELECTED]=5;assert(!tc_batch_valid(&in,&w));cursor[TC_BATCH_SELECTED]=0;
    nodes[6].start=UINT32_MAX;assert(!tc_batch_valid(&in,&w));nodes[6].start=0;
    nodes[6].ground=false;assert(!tc_batch_valid(&in,&w));nodes[6].ground=true;
    plan.constant[0]=0;assert(!tc_batch_valid(&in,&w));plan.constant[0]=1;
    w.output=(uint32_t *)nodes;assert(!tc_batch_valid(&in,&w));w.output=output;
    checks+=6;
    /* Invalid child data stops after the same private completed prefix. */
    uint32_t other_cursor[TC_BATCH_CURSOR_WORDS]={0},other_output[16];
    memset(cursor,0,sizeof cursor);memset(output,0xd3,sizeof output);memset(other_output,0xd3,sizeof other_output);
    TcBatchWork other={other_cursor,TC_BATCH_CURSOR_WORDS,other_output,16,0,0,0,0};
    w.produced=w.used=w.visited=w.status=0;in.quantum=4096;in.budget=100;items[3]=0;
    assert(tc_batch_valid(&in,&w) && tc_batch_valid(&in,&other));
    assert(tc_batch_c(&in,&w)==TC_BATCH_INVALID && tc_batch_x86(&in,&other)==TC_BATCH_INVALID);
    assert(w.produced==other.produced && w.used==other.used && w.visited==other.visited);
    assert(!memcmp(cursor,other_cursor,sizeof cursor) && !memcmp(output,other_output,sizeof output));
    items[3]=4;++checks;
    /* The largest receipt exhausts without wrapping at an activation. */
    for (unsigned remaining=0;remaining<2;++remaining) {
        memset(cursor,0,sizeof cursor);memset(other_cursor,0,sizeof other_cursor);
        memset(output,0xd3,sizeof output);memset(other_output,0xd3,sizeof other_output);
        in.budget=UINT64_MAX;w.used=other.used=UINT64_MAX-remaining;
        assert(tc_batch_valid(&in,&w) && tc_batch_valid(&in,&other));
        assert(tc_batch_c(&in,&w)==TC_BATCH_EXHAUSTED && tc_batch_x86(&in,&other)==TC_BATCH_EXHAUSTED);
        assert(w.used==UINT64_MAX && other.used==UINT64_MAX && !w.produced && !other.produced &&
            w.visited==remaining && other.visited==remaining);
        assert(!memcmp(cursor,other_cursor,sizeof cursor) && !memcmp(output,other_output,sizeof output));
        ++checks;
    }
    printf("PASS: %zu resident C/x86 batch comparisons and controls; fuel, resumed prefixes, symmetry, duplicate occurrences and physical spans\n",checks);
}

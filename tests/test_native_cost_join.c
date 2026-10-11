#include "native_cost.h"
#include <assert.h>
#include <stdio.h>
static size_t checks;
#define CHECK(X) do {assert(X); checks++;} while(0)
int main(void) {
 CettaNativeCostLedger parent,left,right;CettaNativeCostScope p,l,r;
 cetta_native_cost_init(&parent);cetta_native_cost_init(&left);cetta_native_cost_init(&right);
 cetta_native_cost_scope_enter(&p,&parent,1);cetta_native_cost_charge(CETTA_COST_CAPTURE,2);
 CettaNativeCostScope *saved=cetta_native_cost_detach_begin();
 cetta_native_cost_scope_enter(&l,&left,2);cetta_native_cost_charge(CETTA_COST_HEAP_LOOKUP,3);cetta_native_cost_scope_leave(&l);
 cetta_native_cost_scope_enter(&r,&right,3);cetta_native_cost_charge(CETTA_COST_HEAP_UPDATE,5);cetta_native_cost_scope_leave(&r);
 cetta_native_cost_detach_end(saved);CHECK(parent.total.work==2);
 CettaNativeCostLedger *children[]={&left,&right},*duplicate[]={&left,&left},*self[]={&parent};
 CHECK(!cetta_native_cost_join(duplicate,2));CHECK(!left.imported && parent.total.work==2);
 CHECK(!cetta_native_cost_join(self,1));CHECK(cetta_native_cost_join(children,2));
 CHECK(parent.total.work==10 && parent.total.span==7 && parent.total.parallel);
 CHECK(parent.events[1].identity==left.events[0].identity && parent.events[2].identity==right.events[0].identity);
 CHECK(parent.events[1].origin==left.identity && parent.events[2].origin==right.identity);
 CHECK(!cetta_native_cost_join(children,2));CHECK(parent.total.work==10);
 cetta_native_cost_charge(CETTA_COST_WORKER_JOIN,1);CHECK(parent.total.work==11 && parent.total.span==8);
 CettaNativeCostSnapshot before=cetta_native_cost_read(&parent,0,NULL);CHECK(before.span==8 && parent.total.span==9);
 cetta_native_cost_scope_leave(&p);
 CettaNativeCostLedger ancestor;CettaNativeCostScope outer;cetta_native_cost_init(&ancestor);
 cetta_native_cost_scope_enter(&outer,&ancestor,4);CettaNativeCostLedger *nested[]={&parent};
 CHECK(cetta_native_cost_join(nested,1));CHECK(ancestor.total.work==12 && ancestor.total.span==9 && ancestor.total.parallel);
 cetta_native_cost_scope_leave(&outer);
 cetta_native_cost_free(&ancestor);cetta_native_cost_free(&parent);cetta_native_cost_free(&left);cetta_native_cost_free(&right);
 CettaNativeCostLedger top,child[4];CettaNativeCostScope outer2;
 cetta_native_cost_init(&top);
 CettaNativeCostLedger *parts[4];
 for (size_t i=0;i<4;i++) {cetta_native_cost_init(&child[i]);cetta_native_cost_append(&child[i],CETTA_COST_QUANTUM,i+1,0);parts[i]=&child[i];}
 CettaNativeCostPlacement placement[]={{0,0},{0,0},{0,1},{1,0}};
 cetta_native_cost_scope_enter(&outer2,&top,0);
 CHECK(cetta_native_cost_join_scheduled(parts,placement,4));
 CHECK(top.total.work==10 && top.total.span==7 && top.total.parallel);
 cetta_native_cost_scope_leave(&outer2);
 for(size_t i=0;i<4;i++)cetta_native_cost_free(&child[i]);
 cetta_native_cost_free(&top);
 printf("Native causal slice receipts: %zu checks passed\n",checks);
}

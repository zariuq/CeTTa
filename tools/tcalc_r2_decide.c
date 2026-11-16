/* Selected R2 kernel calls replayed through the public tcalc firing seam.
 * Input is emitted by tcalc_r2_receipts.metta, before the chainer commits.
 * No kernel interpreter or second unifier is implemented here. */
#define _POSIX_C_SOURCE 200809L
#include "../native/tcalc/tcalc_syntax.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *why) { fprintf(stderr, "r2-decide: %s\n", why); exit(2); }
static void *room(size_t n, size_t width) {
    if (width && n > SIZE_MAX / width) die("size overflow");
    void *p = calloc(n ? n : 1u, width);
    if (!p) die("out of memory");
    return p;
}
static bool word(TcTerms *t, TcTerm x, const char *s) {
    if (tc_kind(t, x) != TC_SYMBOL) return false;
    size_t n; const char *b = tc_bytes(t, x, &n);
    return n == strlen(s) && !memcmp(b, s, n);
}
static TcTerm part(TcTerms *t, TcTerm x, uint32_t i) {
    if (tc_kind(t, x) != TC_EXPRESSION || i >= tc_length(t, x)) die("malformed receipt field");
    return tc_item(t, x, i);
}
static bool headed(TcTerms *t, TcTerm x, const char *s) {
    return tc_kind(t, x) == TC_EXPRESSION && tc_length(t, x) && word(t, tc_item(t, x, 0u), s);
}
static bool empty(TcTerms *t, TcTerm x) { return tc_kind(t,x)==TC_EXPRESSION && !tc_length(t,x); }
static uint64_t integer(TcTerms *t, TcTerm x) {
    if (tc_kind(t,x)==TC_INTEGER) {
        int64_t n=tc_value(t,x); if(n<0) die("negative index"); return (uint64_t)n;
    }
    if (tc_kind(t,x)!=TC_SYMBOL) die("index is not an integer");
    size_t n; const char *b=tc_bytes(t,x,&n);
    char *s=room(n+1u,1u); memcpy(s,b,n); char *end;
    errno=0; uint64_t result=strtoull(s,&end,10);
    if(errno || end==s || *end || *s=='-') die("invalid integer spelling");
    free(s); return result;
}
static TcTerm symbol(TcTerms *t,const char *s) {return tc_symbol(t,s,strlen(s));}
static TcTerm decode(TcTerms *t,TcTerm x) {
    if(headed(t,x,"R2Value")) {
        TcTerm q=part(t,x,1u); if(tc_kind(t,q)!=TC_STRING) die("unprinted receipt leaf");
        size_t n; const char *b=tc_bytes(t,q,&n);
        char *copy=room(n,1u); memcpy(copy,b,n); TcTerm out=tc_symbol(t,copy,n);free(copy);return out;
    }
    if(headed(t,x,"R2Local")) {
        uint64_t i=integer(t,part(t,x,1u)); if(i>UINT32_MAX) die("local too large");
        return tc_local(t,(uint32_t)i);
    }
    if(!headed(t,x,"R2Expr")) die("unknown receipt encoding");
    TcTerm xs=part(t,x,1u); uint32_t n=tc_length(t,xs);
    TcTerm *items=room(n,sizeof *items);
    for(uint32_t i=0;i<n;i++) items[i]=decode(t,part(t,xs,i));
    TcTerm out=tc_expression(t,items,n); free(items); if(!out) die("term allocation failed"); return out;
}
static TcTerm field(TcTerms *t,TcTerm rule,const char *name) {
    for(uint32_t i=3;i<tc_length(t,rule);i++) {
        TcTerm x=part(t,rule,i); if(headed(t,x,name)) return x;
    }
    die("missing rule field"); return 0;
}
/* Every source space is an opaque term coordinate, not a shared global row. */
static TcTerm placed(TcTerms *t,TcTerm pair) {
    if(tc_length(t,pair)!=2u) die("expected a placed term");
    TcTerm xs[3]={symbol(t,"at"),part(t,pair,0u),part(t,pair,1u)};
    return tc_expression(t,xs,3u);
}
static TcTerm *patterns(TcTerms *t,TcTerm xs,uint32_t *n) {
    *n=tc_length(t,xs)-1u; TcTerm *out=room(*n,sizeof *out);
    for(uint32_t i=0;i<*n;i++) out[i]=placed(t,part(t,xs,i+1u));
    return out;
}
typedef struct { TcOccurrence id; TcTerm term; } Row;
typedef struct {
    Row *rows; size_t n; size_t commits;
    const TcOccurrence *expected_taken; size_t expected_ntake;
    TcTerm *committed; size_t ncommitted;
    bool delta_matches;
} Store;
typedef struct { Store *store; size_t next; } Cursor;
static void rows(TcTerms *t,Store *store,TcTerm xs) {
    for(uint32_t i=0;i<tc_length(t,xs);i++) {
        TcTerm p=part(t,xs,i); TcOccurrence id=integer(t,part(t,p,0u));
        TcTerm fact=decode(t,part(t,p,1u));
        if(word(t,fact,"none")) continue;
        TcTerm term=placed(t,fact); size_t j=0;
        for(;j<store->n;j++) if(store->rows[j].id==id) break;
        if(j<store->n) {if(store->rows[j].term!=term) die("inconsistent occurrence receipt");continue;}
        Row *grown=realloc(store->rows,(store->n+1u)*sizeof *grown); if(!grown)die("row allocation failed");
        store->rows=grown; store->rows[store->n++]=(Row){id,term};
    }
}
static TcStoreStatus open_rows(void *opaque,const TcTerms *t,TcTerm p,void **cursor) {
    (void)t;(void)p; Cursor *c=room(1,sizeof *c);c->store=opaque;*cursor=c;return TC_STORE_ROW;
}
static TcStoreStatus next_row(void *opaque,TcOccurrence *id) {
    Cursor *c=opaque;if(c->next==c->store->n)return TC_STORE_END;
    *id=c->store->rows[c->next++].id;return TC_STORE_ROW;
}
static void close_rows(void *c){free(c);}
static TcStoreStatus import_row(void *opaque,TcDecideScope *scope,TcTerms *t,TcOccurrence id,TcTerm *row){
    (void)scope;(void)t;Store *s=opaque;
    for(size_t i=0;i<s->n;i++)if(s->rows[i].id==id){*row=s->rows[i].term;return TC_STORE_ROW;}
    return TC_STORE_INVALIDATED;
}
static TcCommitStatus commit_rows(void *opaque,const TcTerms *t,const TcOccurrence *take,size_t ntake,
                                  const TcTerm *made,size_t nmade){
    (void)t;
    Store *s=opaque;s->commits++;
    s->delta_matches=ntake==s->expected_ntake;
    /* Resource identities form a set; their vector order is not observed. */
    for(size_t i=0;s->delta_matches && i<ntake;i++){
        bool found=false;
        for(size_t j=0;j<s->expected_ntake;j++)found=found || take[i]==s->expected_taken[j];
        for(size_t j=0;j<i;j++)if(take[i]==take[j])found=false;
        s->delta_matches=found;
    }
    free(s->committed);
    s->committed=room(nmade,sizeof *s->committed);s->ncommitted=nmade;
    if(nmade)memcpy(s->committed,made,nmade*sizeof *made);
    return TC_COMMIT_DONE;
}
static const char *unsupported(TcTerms *t,TcTerm rule,TcTerm mode){
    static const char *const absent[]={"neq","op","guard","fresh"};
    for(size_t i=0;i<sizeof absent/sizeof *absent;i++)
        if(tc_length(t,field(t,rule,absent[i]))>1u)return absent[i];
    TcTerm holes=field(t,rule,"holes");
    for(uint32_t i=1;i<tc_length(t,holes);i++)
        if(!word(t,part(t,part(t,holes,i),2u),"free"))return "bound-avoid";
    if(integer(t,part(t,field(t,rule,"birth"),1u)))return "birth";
    if(!headed(t,mode,"R2Mode"))die("missing selected-call mode");
    if(word(t,part(t,mode,4u),"true"))return "host-authority";
    TcTerm m=part(t,mode,1u);
    if(headed(t,m,"commit") && !empty(t,part(t,m,1u)))return "reach";
    if(headed(t,m,"step")) {
        TcTerm requested=part(t,m,1u);
        for(uint32_t i=0;i<tc_length(t,requested);i++)
            if(word(t,part(t,requested,i),"reach"))return "reach";
    }
    return NULL;
}
static bool native_answer(TcTerms *t,TcTerm request,TcTerm answer,TcTerm **out,size_t *n){
    bool step=headed(t,request,"step");
    *out=NULL;*n=0;
    if(headed(t,request,"lemma-step")){
        if(headed(t,answer,"refused"))return false;
        TcTerm pair[2]={part(t,answer,1u),part(t,answer,0u)};
        *n=1u;*out=room(1u,sizeof **out);(*out)[0]=placed(t,tc_expression(t,pair,2u));return true;
    }
    if(!step && word(t,answer,"0"))return false;
    if(step && !word(t,part(t,answer,0u),"0"))return false;
    TcTerm facts=step?part(t,answer,2u):0;
    if(step){
        *n=tc_length(t,facts);*out=room(*n,sizeof **out);
        for(size_t i=0;i<*n;i++)(*out)[i]=placed(t,part(t,facts,(uint32_t)i));
    }else{
        /* fire/commit return the first term without its space coordinate.
         * Extra made terms have the same convention and are compared below. */
        *n=1u;*out=room(1,sizeof **out);(*out)[0]=part(t,answer,1u);
    }
    return true;
}
int main(int argc,char **argv){
    if(argc!=2)die("usage: tcalc_r2_decide RECEIPTS");
    FILE *f=!strcmp(argv[1],"-")?stdin:fopen(argv[1],"rb");if(!f)die("cannot read receipts");
    size_t calls=0,admitted=0,declined=0,differ=0,axis=0,unported=0,lemma_steps=0;
    typedef struct {char *name;size_t count;} AxisCount;
    AxisCount *axes=NULL;size_t naxes=0,receipt=0;
    size_t capacity=0u;char *text=NULL;ssize_t length;
    /* One printed receipt per line. The public reader still parses each
     * atom; fresh term tables keep proof streams independent of trace size. */
    while((length=getline(&text,&capacity,f))>=0){
      if(strncmp(text,"(R2Receipt ",11u))continue;
      TcTerms *t=tc_terms_new();TcDeclarations d={0};TcSyntaxError err={0};
      TcProgram *packet=t?tc_read_program(t,text,(size_t)length,&d,&err):NULL;
      if(!packet){
        fprintf(stderr,"r2-decide: receipt %zu, %zu:%zu %s\n",receipt,err.line,err.column,err.message);
        free(d.region);tc_terms_free(t);free(text);fclose(f);return 2;
      }
      size_t nrows;const TcTerm *events=tc_program_rows(packet,&nrows);
    for(size_t event=0;event<nrows;event++){
        TcTerm x=events[event];if(!headed(t,x,"R2Receipt"))continue;
        receipt++;
        TcTerm before=part(t,x,2u);
        if(headed(t,before,"R2Axis")){
            TcTerm name=part(t,before,1u);size_t n=0u;
            if(tc_kind(t,name)!=TC_SYMBOL)die("host-event name is not a symbol");
            const char *bytes=tc_bytes(t,name,&n);char *spelling=room(n+1u,1u);memcpy(spelling,bytes,n);
            size_t i=0;for(;i<naxes;i++)if(!strcmp(axes[i].name,spelling))break;
            if(i==naxes){AxisCount *g=realloc(axes,(naxes+1u)*sizeof *g);if(!g)die("axis allocation failed");
                axes=g;axes[naxes++]=(AxisCount){spelling,0};}else free(spelling);
            axes[i].count++;axis++;continue;
        }
        if(!headed(t,before,"R2Firing")){
            unported++;printf("(R2Unported ");tc_print(t,part(t,x,1u),stdout);printf(" ");
            tc_print(t,before,stdout);puts(")");continue;
        }
        TcTerm mode=part(t,before,1u),rule=decode(t,part(t,before,3u));
        const char *why=unsupported(t,rule,mode);
        if(why){unported++;printf("(R2Unported ");tc_print(t,part(t,x,1u),stdout);
            printf(" (rule ");tc_print(t,part(t,before,2u),stdout);printf(") %s)\n",why);continue;}
        TcRule r={.name="selected-r2-call"};
        r.locals=tc_length(t,field(t,rule,"holes"))-1u;
        TcTerm *take=patterns(t,field(t,rule,"take"),&r.takes),
               *read=patterns(t,field(t,rule,"keep"),&r.reads),
               *make=patterns(t,field(t,rule,"make"),&r.makes);
        r.take=take;r.read=read;r.make=make;
        TcTerm tells=field(t,rule,"tell"),inputs=decode(t,part(t,before,7u));
        TcTerm extra=tc_length(t,before)>8u?decode(t,part(t,before,8u)):tc_expression(t,NULL,0u);
        r.tells=tc_length(t,tells)-1u+tc_length(t,inputs)+tc_length(t,extra);
        TcTerm *tell=room((size_t)r.tells*2u,sizeof *tell);r.tell=tell;size_t at=0;
        for(uint32_t i=1;i<tc_length(t,tells);i++){
            TcTerm eq=part(t,tells,i);tell[at++]=part(t,eq,0u);tell[at++]=part(t,eq,1u);}
        for(uint32_t i=0;i<tc_length(t,inputs);i++){
            TcTerm p=part(t,inputs,i);uint64_t j=integer(t,part(t,p,0u));if(j>=r.locals)die("input outside locals");
            tell[at++]=tc_local(t,(uint32_t)j);tell[at++]=part(t,p,1u);}
        for(uint32_t i=0;i<tc_length(t,extra);i++){
            TcTerm eq=part(t,extra,i);tell[at++]=part(t,eq,0u);tell[at++]=part(t,eq,1u);}
        TcTerm ab=field(t,rule,"absent"),ct=field(t,rule,"count");
        bool ask_ab=word(t,part(t,mode,2u),"true"),ask_ct=word(t,part(t,mode,3u),"true");
        r.checks=(ask_ab?tc_length(t,ab)-1u:0u)+(ask_ct?tc_length(t,ct)-1u:0u);
        TcCheck *checks=room(r.checks,sizeof *checks);r.check=checks;at=0;
        if(ask_ab)for(uint32_t i=1;i<tc_length(t,ab);i++)checks[at++]=(TcCheck){.kind=TC_CHECK_ABSENT,.left=placed(t,part(t,ab,i))};
        if(ask_ct)for(uint32_t i=1;i<tc_length(t,ct);i++){
            TcTerm p=part(t,ct,i),xs[2]={part(t,p,0u),part(t,p,1u)};
            checks[at++]=(TcCheck){.kind=TC_CHECK_COUNT,.left=placed(t,tc_expression(t,xs,2u)),.count=integer(t,part(t,p,2u))};}
        Store store={0};rows(t,&store,part(t,before,5u));rows(t,&store,part(t,before,6u));
        TcTerm ids=part(t,before,4u);size_t ns=tc_length(t,ids);TcOccurrence *selection=room(ns,sizeof *selection);
        for(size_t i=0;i<ns;i++)selection[i]=integer(t,part(t,ids,(uint32_t)i));
        store.expected_taken=selection;store.expected_ntake=ns>=r.takes?r.takes:SIZE_MAX;
        TcProgram *program=tc_program_new(t);if(!program || !tc_program_add_rule(program,&r))die("selected rule refused");
        TcStoreOps ops={.store=&store,.open=open_rows,.next=next_row,.close=close_rows,.import=import_row,.commit=commit_rows};
        TcInterpretation how={.step=TC_STEP_ONE,.ask=TC_ASK_AFTER_TELLS,.store=TC_STORE_MULTISET,.matches=TC_MATCHES_SYMMETRIC,.backend=TC_BACKEND_C};
        const TcTerm *made=NULL;size_t nm=0;TcDecision decision=tc_decide(program,0u,&how,&ops,selection,ns,t,&made,&nm);
        if(decision!=TC_DECIDED_ADMITTED && decision!=TC_DECIDED_DECLINED && decision!=TC_DECIDED_INVALIDATED)
            die("protocol execution failed");
        bool actual=decision==TC_DECIDED_ADMITTED;TcTerm request=decode(t,part(t,x,3u)),answer=decode(t,part(t,x,4u));
        TcTerm *expected=NULL;size_t ne=0;bool ok=native_answer(t,request,answer,&expected,&ne);
        bool same=actual==ok && store.commits==(actual?1u:0u);
        if(same && actual){
            same=store.delta_matches && store.ncommitted==nm;
            for(size_t i=0;same && i<nm;i++)same=store.committed[i]==made[i];
            if(headed(t,request,"step") || headed(t,request,"lemma-step")){
                same=same && nm==ne;for(size_t i=0;same && i<nm;i++)same=made[i]==expected[i];
            }else{
                same=same && nm>=1u && part(t,made[0],2u)==expected[0];
                if(same && headed(t,request,"commit")){
                    TcTerm extras=part(t,answer,2u);same=nm==tc_length(t,extras)+1u;
                    for(size_t i=1;same && i<nm;i++)same=part(t,made[i],2u)==part(t,part(t,extras,(uint32_t)i-1u),1u);
                }else if(same)same=nm==1u;
            }
        }
        calls++;if(headed(t,request,"lemma-step"))lemma_steps++;
        if(actual)admitted++;else declined++;
        if(!same){differ++;printf("(R2Difference ");tc_print(t,part(t,x,1u),stdout);
            printf(" (event %zu) (rule ",receipt);tc_print(t,part(t,before,2u),stdout);
            printf(") (decision %d) (native ",decision);tc_print(t,answer,stdout);printf(") (made");
            for(size_t i=0;i<nm;i++){putchar(' ');tc_print(t,made[i],stdout);}puts("))");}
        free(expected);free(selection);free(store.rows);free(store.committed);free(checks);free(tell);free(take);free(read);free(make);tc_program_free(program);
    }
      free(d.region);tc_program_free(packet);tc_terms_free(t);
    }
    if(ferror(f))die("receipt read failed");
    fclose(f);free(text);
    for(size_t i=0;i<naxes;i++){
        printf("(R2HostEvent %s %zu)\n",axes[i].name,axes[i].count);
        free(axes[i].name);
    }
    printf("(R2CallScopes (native-firings %zu) (lemma-steps %zu))\n",calls-lemma_steps,lemma_steps);
    printf("(R2Protocol (calls %zu) (admitted %zu) (declined %zu) (differ %zu) (unported %zu) (host-events %zu))\n",
           calls,admitted,declined,differ,unported,axis);
    free(axes);return differ?1:0;
}

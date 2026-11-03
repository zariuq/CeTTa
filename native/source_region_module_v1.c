#define _POSIX_C_SOURCE 200809L
#include "source_region_module_v1.h"
#include "src/raw_source_region.h"
#include "src/eval.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

static pthread_mutex_t submission_lock=PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t submission_cancelled;
static void cancel_submission(int signal_number) {
    (void)signal_number;submission_cancelled=1;
}

typedef struct {Arena *arena;Atom **items;size_t count;} Regions;
static Atom *app(Arena *a,const char *head,Atom **items,size_t count) {
    if(count>=UINT32_MAX)return NULL;
    Atom **all=arena_alloc(a,(count+1u)*sizeof(*all));if(!all)return NULL;
    all[0]=atom_symbol(a,head);
    for(size_t i=0;i<count;i++)all[i+1u]=items[i];
    return atom_expr(a,all,(CettaExprLen)count+1u);
}
static bool text(Atom *a){return a&&a->kind==ATOM_GROUNDED&&a->ground.gkind==GV_STRING;}
static bool visit(void *context,const CettaRawSourceRegion *r,CettaRawSourceStatus status) {
    Regions *rs=context;
    if(rs->count>=UINT32_MAX-1u)return false;
    Atom **next=realloc(rs->items,(rs->count+1u)*sizeof(*next));if(!next)return false;
    rs->items=next;
    Atom *spans[]={atom_int(rs->arena,r->start),atom_int(rs->arena,r->body_start),
        atom_int(rs->arena,r->body_end),atom_int(rs->arena,r->end),atom_int(rs->arena,r->fence_depth)};
    const char *name=status==CETTA_RAW_COMPLETE?"RawSourceRegionV1":
        status==CETTA_RAW_INCOMPLETE?"RawSourceRegionIncompleteV1":"RawSourceRegionInvalidV1";
    rs->items[rs->count++]=app(rs->arena,name,spans,5u);
    return rs->items[rs->count-1u]!=NULL;
}
static Atom *read_submission(Arena *a,bool raw_enabled) {
    pthread_mutex_lock(&submission_lock);
    struct sigaction action={0},previous={0};
    action.sa_handler=cancel_submission;sigemptyset(&action.sa_mask);
    submission_cancelled=0;
    bool installed=sigaction(SIGINT,&action,&previous)==0;
    char *source=NULL,*line=NULL;size_t length=0,line_capacity=0;bool tty=isatty(STDIN_FILENO);
    Atom *result=NULL;
    for(;;) {
        if(tty){fputs(length?"... ":"cetta> ",stdout);fflush(stdout);}
        errno=0;ssize_t n=getline(&line,&line_capacity,stdin);
        if(submission_cancelled){clearerr(stdin);result=atom_symbol(a,"SourceCancelledV1");break;}
        if(n<0) {
            if(errno==EINTR){clearerr(stdin);result=atom_symbol(a,"SourceCancelledV1");}
            else if(length) {
                Atom *v[]={atom_string_n(a,source,length)};
                result=app(a,"SourceInputIncompleteV1",v,1u);
            }else result=atom_symbol(a,"SourceEOFV1");
            break;
        }
        size_t command=(size_t)n;
        while(command&&(line[command-1u]=='\n'||line[command-1u]=='\r'))command--;
        if(!length&&command==7u&&!memcmp(line,":cancel",7u)){result=atom_symbol(a,"SourceCancelledV1");break;}
        if(!length&&command==5u&&!memcmp(line,":quit",5u)){result=atom_symbol(a,"SourceEOFV1");break;}
        if((size_t)n>SIZE_MAX-length-1u)break;
        char *grown=realloc(source,length+(size_t)n+1u);if(!grown)break;
        source=grown;memcpy(source+length,line,(size_t)n);length+=(size_t)n;source[length]=0;
        size_t offset=0;
        CettaSourceDocumentStatus status=cetta_source_document_scan((const uint8_t *)source,length,raw_enabled,NULL,NULL,&offset);
        if(status==CETTA_SOURCE_DOCUMENT_INCOMPLETE)continue;
        Atom *v[]={atom_string_n(a,source,length),atom_int(a,offset)};
        result=app(a,status==CETTA_SOURCE_DOCUMENT_COMPLETE?"SourceSubmissionV1":"SourceInputInvalidV1",v,2u);
        break;
    }
    free(source);free(line);
    if(installed)sigaction(SIGINT,&previous,NULL);
    pthread_mutex_unlock(&submission_lock);
    return result?result:atom_error(a,atom_symbol(a,"read-submission"),atom_string(a,"source allocation failed"));
}
Atom *cetta_source_region_dispatch_v1(CettaLibraryContext *ctx,Arena *a,Atom *head,Atom **args,uint32_t nargs) {
    if(atom_is_symbol(head,"__cetta_lib_source_read_submission_v1")&&nargs==1u)
        return read_submission(a,cetta_language_reads_raw_source(ctx->session.language_id,ctx->session.profile));
    if(atom_is_symbol(head,"__cetta_lib_source_regions_v1")&&nargs==1u&&text(args[0])) {
        Regions rs={.arena=a};size_t offset=0;
        CettaSourceDocumentStatus status=cetta_source_document_scan((const uint8_t *)args[0]->ground.sval,
            atom_string_len(args[0]),true,visit,&rs,&offset);
        static const char *names[]={"SourceLexicallyCompleteV1","SourceLexicallyIncompleteV1","SourceLexicallyInvalidV1","SourceAllocationFailureV1"};
        Atom *v[]={atom_symbol(a,names[status]),atom_int(a,offset),app(a,"RawSourceRegionsV1",rs.items,rs.count)};
        free(rs.items);return app(a,"SourceRegionsV1",v,3u);
    }
    if(atom_is_symbol(head,"__cetta_lib_source_region_text_v1")&&nargs==2u&&text(args[0])&&
        args[1]&&args[1]->kind==ATOM_GROUNDED&&args[1]->ground.gkind==GV_INT&&args[1]->ground.ival>=0) {
        size_t length=0,depth=(size_t)args[1]->ground.ival;
        const uint8_t *body=(const uint8_t *)args[0]->ground.sval;size_t n=atom_string_len(args[0]);
        if(!cetta_raw_source_render(body,n,depth,NULL,0,&length))return atom_error(a,head,atom_string(a,"invalid raw source body"));
        uint8_t *bytes=malloc(length);if(!bytes)return atom_error(a,head,atom_string(a,"source allocation failed"));
        bool ok=cetta_raw_source_render(body,n,depth,bytes,length,&length);
        Atom *result=ok?atom_string_n(a,(char *)bytes,length):NULL;free(bytes);return result;
    }
    return NULL;
}

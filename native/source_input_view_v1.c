#include "source_input_view_v1.h"
#include "src/library.h"
#include "src/native_handle.h"
#include "src/space.h"
#include <stdlib.h>
#include <string.h>

#define KIND "cetta.source-input-view.v1"
typedef struct {
    Arena storage;
    Atom *source;
    SpaceHeadSnapshot snapshot;
    CettaCount candidates,lent;
} InputView;

static void release(void *raw) {
    InputView *view=raw;
    space_head_snapshot_release(&view->snapshot);
    arena_free(&view->storage);free(view);
}
static Atom *error(Arena *a,Atom *head,const char *message) {
    return atom_error(a,head,atom_string(a,message));
}
static Atom *form(Arena *a,const char *head,Atom *value) {
    Atom *parts[]={atom_symbol(a,head),value};return atom_expr(a,parts,2u);
}
static SymbolId row_head(Atom *row) {
    if(row&&row->kind==ATOM_SYMBOL)return row->sym_id;
    if(row&&row->kind==ATOM_EXPR&&row->expr.len&&row->expr.elems[0]->kind==ATOM_SYMBOL)
        return row->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}
Atom *cetta_source_input_view_dispatch_v1(CettaLibraryContext *ctx,Arena *a,
    Atom *head,Atom **args,uint32_t nargs) {
    if(atom_is_symbol(head,"__cetta_lib_source_input_capture_v1")) {
        if(nargs!=1u||!args[0]||args[0]->kind!=ATOM_GROUNDED||args[0]->ground.gkind!=GV_SPACE)
            return error(a,head,"input capture expects a Space");
        InputView *view=calloc(1,sizeof(*view));
        if(!view)return error(a,head,"input view allocation failed");
        arena_init(&view->storage);
        view->source=atom_deep_copy(&view->storage,args[0]);
        if(!view->source||!space_head_snapshot_init(args[0]->ground.ptr,&view->snapshot)) {
            release(view);return atom_symbol(a,"SourceInputCopyV1");
        }
        uint64_t id;
        if(!cetta_native_handle_alloc(ctx,KIND,view,release,&id)) {
            release(view);return error(a,head,"input view publication failed");
        }
        Atom *owner=cetta_native_handle_owned_atom(ctx,a,KIND,id);
        if(!owner){cetta_native_handle_close(ctx,KIND,id);return error(a,head,"input owner allocation failed");}
        return form(a,"SourceInputViewV1",owner);
    }
    if(atom_is_symbol(head,"__cetta_lib_source_input_record_v1")) {
        InputView *view=nargs==1u?cetta_native_handle_get_owned(ctx,args[0],KIND):NULL;
        if(!view)return error(a,head,"expected a live captured input view");
        Atom *parts[]={atom_symbol(a,"SourceInputViewRecordV1"),
            atom_int(a,(int64_t)view->snapshot.rows.ceiling),atom_int(a,(int64_t)view->candidates),atom_int(a,(int64_t)view->lent)};
        return atom_expr(a,parts,4u);
    }
    if(!atom_is_symbol(head,"__cetta_lib_source_input_rows_v1"))return NULL;
    InputView *view=nargs==2u?cetta_native_handle_get_owned(ctx,args[0],KIND):NULL;
    Atom *names=nargs==2u?args[1]:NULL;
    if(!view||!names||names->kind!=ATOM_EXPR)return error(a,head,"expected an input view and a tuple of head names");
    size_t count=names->expr.len;
    SymbolId *heads=count?malloc(count*sizeof(*heads)):NULL;
    if(count&&!heads)return error(a,head,"head selection allocation failed");
    for(size_t i=0;i<count;i++) {
        Atom *name=names->expr.elems[i];
        if(!name||name->kind!=ATOM_GROUNDED||name->ground.gkind!=GV_STRING||strlen(name->ground.sval)!=atom_string_len(name)) {
            free(heads);return error(a,head,"input heads must be strings without NUL");
        }
        heads[i]=atom_symbol(a,name->ground.sval)->sym_id;
    }
    SpaceOccurrenceCursor cursor;
    if(!space_head_snapshot_select(&view->snapshot,heads,count,&cursor)) {
        free(heads);return error(a,head,"captured input selection failed");
    }
    Atom **values=NULL;size_t length=0,capacity=0;Atom *row=NULL;
    view->candidates=0u;view->lent=0u;
    SpaceOccurrenceCursorStep step;
    while((step=space_occurrence_cursor_next(&cursor,NULL,&row))==SPACE_OCCURRENCE_CURSOR_ITEM) {
        view->candidates++;
        SymbolId actual=row_head(row);bool selected=false;
        for(size_t i=0;!selected&&i<count;i++)selected=actual==heads[i];
        if(!selected)continue;
        if(length==UINT32_MAX){step=SPACE_OCCURRENCE_CURSOR_INVALIDATED;break;}
        if(length==capacity) {
            size_t next=capacity?capacity*2u:16u;
            if(next<capacity||next>SIZE_MAX/sizeof(*values)){step=SPACE_OCCURRENCE_CURSOR_INVALIDATED;break;}
            Atom **grown=realloc(values,next*sizeof(*values));
            if(!grown){step=SPACE_OCCURRENCE_CURSOR_INVALIDATED;break;}values=grown;capacity=next;
        }
        values[length++]=atom_deep_copy(a,row);view->lent++;
    }
    space_occurrence_cursor_release(&cursor);free(heads);
    Atom *result=step==SPACE_OCCURRENCE_CURSOR_END?atom_expr(a,values,(CettaExprLen)length):
        error(a,head,"captured input enumeration did not complete");
    free(values);return result;
}

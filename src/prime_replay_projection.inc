typedef enum {
    PRIME_REPLAY_META, PRIME_REPLAY_DATA, PRIME_REPLAY_ID, PRIME_REPLAY_OBSERVATION,
    PRIME_REPLAY_HISTORY, PRIME_REPLAY_SELECTIONS, PRIME_REPLAY_SELECTION,
    PRIME_REPLAY_FOREST, PRIME_REPLAY_GRAPH, PRIME_REPLAY_NODES, PRIME_REPLAY_NODE,
    PRIME_REPLAY_EVENT, PRIME_REPLAY_COST, PRIME_REPLAY_COST_EVENT,
    PRIME_REPLAY_RESIDUAL, PRIME_REPLAY_UINT, PRIME_REPLAY_SYMBOL,
    PRIME_REPLAY_AGENDA, PRIME_REPLAY_QUOTED_DATA,
    PRIME_REPLAY_WAITING, PRIME_REPLAY_WAITING_ROW
} PrimeReplayProjectionKind;
typedef struct {
    const Atom *left,*right;
    PrimeReplayProjectionKind kind;
    CettaCheckpointIdentityRole role;
} PrimeReplayProjectionNode;
typedef struct {
    PrimeReplayComparison *comparison;
    PrimeReplayProjectionNode *nodes;
    size_t length,at,capacity;
    CettaLibraryContext *library;
    const PrimeNativeControl *left_residual,*right_residual;
} PrimeReplayProjection;

/* Presentation nodes carry no address authority. Their scalar fields are
 * compared under the declared schema. Only actual data subgraphs use the
 * common injective Atom/backing relation; a code-shaped data value never
 * selects one of these presentation cases. */
static bool prime_replay_projection_push(PrimeReplayProjection *p,const Atom *a,
    const Atom *b,PrimeReplayProjectionKind kind,CettaCheckpointIdentityRole role) {
    if(!prime_replay_compare_tick(p->comparison,"projection.enqueue",1u,false))return false;
    if(p->length==p->capacity)return prime_replay_compare_refuse(p->comparison,
        "projection.queue","ProjectionCapacity");
    p->nodes[p->length++]=(PrimeReplayProjectionNode){a,b,kind,role};return true;
}
static bool prime_replay_projection_span(PrimeReplayComparison *c,const Atom *a,bool right) {
    if(!a)return prime_replay_compare_refuse(c,"projection.atom","MissingProjection");
    const Arena *owner=prime_replay_read_span(c,a,sizeof(*a),right);
    bool global=!owner&&atom_checkpoint_global_contains(c->globals,a,&c->read);
    if(!owner&&!global)return prime_replay_compare_refuse(c,"projection.owner","UnheldProjection");
    if(a->kind==ATOM_EXPR&&a->expr.len) {
        if(a->expr.len>SIZE_MAX/sizeof(*a->expr.elems)||
            (!global&&!prime_replay_read_span(c,a->expr.elems,
                (size_t)a->expr.len*sizeof(*a->expr.elems),right)))
            return prime_replay_compare_refuse(c,"projection.children","UnheldProjectionArray");
    }
    if(a->kind==ATOM_SYMBOL&&!prime_replay_read_symbol_payload(c,a,right))return false;
    if(a->kind==ATOM_GROUNDED&&!atom_checkpoint_payload_admitted(a,owner,global,&c->read,right))
        return prime_replay_compare_refuse(c,"projection.payload","UnheldProjectionPayload");
    return !c->refusal;
}
static bool prime_replay_projection_uint(PrimeReplayComparison *c,const Atom *a,
    bool right,uint64_t *value) {
    if(!prime_replay_projection_span(c,a,right)||a->kind!=ATOM_GROUNDED)return false;
    if(a->ground.gkind==GV_INT&&a->ground.ival>=0){*value=(uint64_t)a->ground.ival;return true;}
    if(a->ground.gkind!=GV_BIGINT||!a->ground.bigint)return false;
#if CETTA_BUILD_WITH_GMP
    /* The existing view returns the initialized value without constructing
     * its optional text cache. This public metadata profile is the selected
     * 64-bit GMP build; arbitrary numeric payloads use the Atom reader. */
    _Static_assert(ULONG_MAX==UINT64_MAX,"64-bit unsigned-long metadata profile");
    mpz_srcptr n=atom_bigint_mpz_view(a);
    if(!n||!prime_replay_compare_tick(c,"projection.uint64_value",1u,false)||
        n->_mp_size<0||n->_mp_size>1||!mpz_fits_ulong_p(n))return false;
    *value=(uint64_t)mpz_get_ui(n);return true;
#else
    /* Without GMP this accessor is a plain initialized-text read. */
    const char *text=atom_bigint_cstr(a);
    if(!text)return false;
    uint64_t v=0u;size_t at=0u;
    while(at<=20u) {
        if(!prime_replay_compare_tick(c,"projection.uint64_digit",1u,false))return false;
        unsigned char ch=(unsigned char)text[at++];
        if(!ch){if(at==1u)return false;*value=v;return true;}
        if(ch<'0'||ch>'9'||v>(UINT64_MAX-(ch-'0'))/10u)return false;
        v=v*10u+(ch-'0');
    }
    return false;
#endif
}
static bool prime_replay_projection_name(PrimeReplayComparison *c,const Atom *a,
    const char *name,bool right) {
    return prime_replay_projection_span(c,a,right)&&atom_is_symbol((Atom *)a,name);
}
static bool prime_replay_projection_expr(PrimeReplayProjection *p,const Atom *a,
    const Atom *b,const char *head,size_t length) {
    PrimeReplayComparison *c=p->comparison;
    if(!prime_replay_projection_span(c,a,false)||!prime_replay_projection_span(c,b,true)||
        a->kind!=ATOM_EXPR||b->kind!=ATOM_EXPR||a->expr.len!=b->expr.len||
        (length&&a->expr.len!=length))return false;
    return !head||(a->expr.len&&prime_replay_projection_name(c,a->expr.elems[0],head,false)&&
        prime_replay_projection_name(c,b->expr.elems[0],head,true));
}
static bool prime_replay_projection_sequence(PrimeReplayProjection *p,const Atom *a,
    const Atom *b,PrimeReplayProjectionKind kind) {
    if(!prime_replay_projection_expr(p,a,b,NULL,0u))return false;
    for(CettaExprIndex i=0u;i<a->expr.len;i++)
        if(!prime_replay_projection_push(p,a->expr.elems[i],b->expr.elems[i],kind,0))return false;
    return true;
}
static bool prime_replay_projection_fields(PrimeReplayProjection *p,const Atom *a,
    const Atom *b,CettaExprIndex first) {
    PrimeReplayComparison *c=p->comparison;
    for(CettaExprIndex i=first;i<a->expr.len;i++) {
        const Atom *x=a->expr.elems[i],*y=b->expr.elems[i];
        if(!prime_replay_projection_expr(p,x,y,NULL,0u)||!x->expr.len||
            !prime_replay_projection_span(c,x->expr.elems[0],false)||
            !prime_replay_projection_span(c,y->expr.elems[0],true)||
            x->expr.elems[0]->kind!=ATOM_SYMBOL||y->expr.elems[0]->kind!=ATOM_SYMBOL||
            !prime_replay_read_symbol(c,x->expr.elems[0],y->expr.elems[0]))return false;
        for(CettaExprIndex j=first;j<i;j++) {
            if(!prime_replay_compare_tick(c,"projection.unique_label",1u,true))return false;
            bool equal=false;
            if(!prime_replay_symbols_equal(c,a->expr.elems[j]->expr.elems[0],
                x->expr.elems[0],&equal)||equal)return false;
        }
    }
    return true;
}
static const Atom *prime_replay_projection_field(const Atom *record,const char *label) {
    for(CettaExprIndex i=2u;i<record->expr.len;i++) {
        const Atom *f=record->expr.elems[i];
        if(f->expr.len==2u&&atom_is_symbol(f->expr.elems[0],label))return f->expr.elems[1];
    }
    return NULL;
}
/* Only the transaction renderer selects this schema. Quoted syntax is
 * retained data; its variables and private ownership metadata are read by
 * the same complete Atom relation as other captured payloads. */
static bool prime_replay_projection_agenda(PrimeReplayProjection *p,const Atom *a,const Atom *b) {
    if(!p->comparison->transaction||!prime_replay_projection_expr(p,a,b,"SpaceAgenda",8u))return false;
    static const char *labels[]={"phase","selected","commits","retries","obligation","waiting","observation"};
    static const PrimeReplayProjectionKind kinds[]={PRIME_REPLAY_SYMBOL,PRIME_REPLAY_QUOTED_DATA,
        PRIME_REPLAY_UINT,PRIME_REPLAY_UINT,PRIME_REPLAY_SYMBOL,PRIME_REPLAY_WAITING,PRIME_REPLAY_SYMBOL};
    for(CettaExprIndex i=1u;i<8u;i++) {
        const Atom *x=a->expr.elems[i],*y=b->expr.elems[i];
        if(!prime_replay_projection_expr(p,x,y,labels[i-1u],2u))return false;
        if(i==7u&&(!prime_replay_projection_name(p->comparison,x->expr.elems[1],"committed-trace",false)||
            !prime_replay_projection_name(p->comparison,y->expr.elems[1],"committed-trace",true)))return false;
        if(!prime_replay_projection_push(p,x->expr.elems[1],y->expr.elems[1],kinds[i-1u],0))return false;
    }
    return true;
}

static bool prime_replay_projection_step(PrimeReplayProjection *p,PrimeReplayProjectionNode n) {
    PrimeReplayComparison *c=p->comparison;const Atom *a=n.left,*b=n.right;
    if(n.kind==PRIME_REPLAY_DATA)return prime_replay_read_atom(c,"projection.data",a,b);
    if(!prime_replay_projection_span(c,a,false)||!prime_replay_projection_span(c,b,true))return false;
    if(n.kind==PRIME_REPLAY_UINT) {
        uint64_t x,y;return prime_replay_projection_uint(c,a,false,&x)&&
            prime_replay_projection_uint(c,b,true,&y)&&
            prime_replay_read_scalar(c,"projection.unsigned",x,y);
    }
    if(n.kind==PRIME_REPLAY_SYMBOL)
        return a->kind==ATOM_SYMBOL&&b->kind==ATOM_SYMBOL&&
            prime_replay_read_symbol(c,a,b);
    if(n.kind==PRIME_REPLAY_AGENDA)return prime_replay_projection_agenda(p,a,b);
    if(n.kind==PRIME_REPLAY_QUOTED_DATA||n.kind==PRIME_REPLAY_WAITING) {
        if(!prime_replay_projection_expr(p,a,b,"quote",2u))return false;
        const Atom *x=a->expr.elems[1],*y=b->expr.elems[1];
        return n.kind==PRIME_REPLAY_QUOTED_DATA
            ? prime_replay_projection_push(p,x,y,PRIME_REPLAY_DATA,0)
            : prime_replay_projection_sequence(p,x,y,PRIME_REPLAY_WAITING_ROW);
    }
    if(n.kind==PRIME_REPLAY_WAITING_ROW) {
        if(!prime_replay_projection_expr(p,a,b,NULL,3u))return false;
        return prime_replay_projection_push(p,a->expr.elems[0],b->expr.elems[0],PRIME_REPLAY_UINT,0)&&
            prime_replay_projection_push(p,a->expr.elems[1],b->expr.elems[1],PRIME_REPLAY_UINT,0)&&
            prime_replay_projection_push(p,a->expr.elems[2],b->expr.elems[2],PRIME_REPLAY_DATA,0);
    }
    if(n.kind==PRIME_REPLAY_ID) {
        uint64_t x,y;return prime_replay_projection_uint(c,a,false,&x)&&
            prime_replay_projection_uint(c,b,true,&y)&&
            prime_replay_read_identity(c,"projection.identity",n.role,x,y);
    }
    if(n.kind==PRIME_REPLAY_RESIDUAL) {
        uint64_t x,y;
        if(!cetta_native_handle_arg((Atom *)a,"prime-search",&x)||
            !cetta_native_handle_arg((Atom *)b,"prime-search",&y)||
            cetta_native_handle_get_owned(p->library,"prime-search",(Atom *)a)!=p->left_residual||
            cetta_native_handle_get_owned(p->library,"prime-search",(Atom *)b)!=p->right_residual)
            return prime_replay_compare_refuse(c,"projection.residual","ResidualAuthority");
        /* These handles are authenticated at their actual owning targets,
         * not by assigning a numeric handle namespace transport. */
        return prime_replay_read_physical(c,"projection.residual_handle",x,y);
    }
    if(n.kind==PRIME_REPLAY_FOREST&&a->kind==ATOM_SYMBOL)
        return prime_replay_projection_name(c,a,"None",false)&&prime_replay_projection_name(c,b,"None",true);
    if(n.kind==PRIME_REPLAY_EVENT&&a->kind==ATOM_SYMBOL)
        return prime_replay_projection_name(c,a,"Join",false)&&prime_replay_projection_name(c,b,"Join",true);
    if(n.kind==PRIME_REPLAY_SELECTIONS)return prime_replay_projection_sequence(p,a,b,PRIME_REPLAY_SELECTION);
    if(n.kind==PRIME_REPLAY_FOREST)return prime_replay_projection_sequence(p,a,b,PRIME_REPLAY_GRAPH);
    if(n.kind==PRIME_REPLAY_NODES)return prime_replay_projection_sequence(p,a,b,PRIME_REPLAY_NODE);
    if(n.kind==PRIME_REPLAY_META) {
        if(!prime_replay_read_scalar(c,"projection.kind",a->kind,b->kind))return false;
        if(a->kind==ATOM_SYMBOL)return prime_replay_read_symbol(c,a,b);
        if(a->kind==ATOM_GROUNDED) {
            if(a->ground.gkind!=b->ground.gkind)return false;
            if(a->ground.gkind==GV_INT)return prime_replay_read_scalar(c,"projection.integer",a->ground.ival,b->ground.ival);
            if(a->ground.gkind==GV_BOOL)return prime_replay_read_scalar(c,"projection.boolean",a->ground.bval,b->ground.bval);
            if(a->ground.gkind==GV_STRING)return prime_replay_read_bytes(c,"projection.string",a->ground.sval,a->ground.slen,b->ground.sval,b->ground.slen);
            uint64_t x,y;return prime_replay_projection_uint(c,a,false,&x)&&
                prime_replay_projection_uint(c,b,true,&y)&&prime_replay_read_scalar(c,"projection.uint64",x,y);
        }
        return prime_replay_projection_sequence(p,a,b,PRIME_REPLAY_META);
    }
    const char *head=NULL;size_t arity=0u;
    switch(n.kind) {
    case PRIME_REPLAY_OBSERVATION:head="Observation";break;
    case PRIME_REPLAY_HISTORY:head="History";arity=13u;break;
    case PRIME_REPLAY_SELECTION:head="Selection";arity=8u;break;
    case PRIME_REPLAY_GRAPH:head="NeedGraph";arity=5u;break;
    case PRIME_REPLAY_NODE:head="NeedNode";arity=5u;break;
    case PRIME_REPLAY_EVENT:head="NeedEvent";arity=12u;break;
    case PRIME_REPLAY_COST:head="NativeCost";arity=8u;break;
    case PRIME_REPLAY_COST_EVENT:head="CostEvent";arity=7u;break;
    default:return false;
    }
    if(!prime_replay_projection_expr(p,a,b,head,arity))return false;
    if(n.kind==PRIME_REPLAY_OBSERVATION) {
        if(a->expr.len<9u||!prime_replay_projection_fields(p,a,b,2u))return false;
        const char *required[]={"satisfied","closed","residual","work","status","history","cost"};
        for(size_t i=0u;i<sizeof(required)/sizeof(*required);i++)
            if(!prime_replay_projection_field(a,required[i])||!prime_replay_projection_field(b,required[i]))return false;
        if(c->transaction&&(!prime_replay_projection_field(a,"agenda")||
            !prime_replay_projection_field(b,"agenda")))return false;
        if(!prime_replay_projection_push(p,a->expr.elems[1],b->expr.elems[1],PRIME_REPLAY_DATA,0))return false;
        for(CettaExprIndex i=2u;i<a->expr.len;i++) {
            const Atom *x=a->expr.elems[i],*y=b->expr.elems[i];if(x->expr.len!=2u)return false;
            PrimeReplayProjectionKind kind=PRIME_REPLAY_META;
            if(atom_is_symbol(x->expr.elems[0],"residual"))kind=PRIME_REPLAY_RESIDUAL;
            else if(atom_is_symbol(x->expr.elems[0],"history"))kind=PRIME_REPLAY_HISTORY;
            else if(atom_is_symbol(x->expr.elems[0],"cost"))kind=PRIME_REPLAY_COST;
            else if(c->transaction&&atom_is_symbol(x->expr.elems[0],"agenda"))kind=PRIME_REPLAY_AGENDA;
            if(!prime_replay_projection_push(p,x->expr.elems[1],y->expr.elems[1],kind,0))return false;
        }
        return true;
    }
    if(n.kind==PRIME_REPLAY_HISTORY) {
        if(!prime_replay_projection_fields(p,a,b,1u))return false;
        static const char *labels[]={"status","start-work","earlier","source-correspondence",
            "omitted-selections","omitted-nodes","overflow","RetainedCharges","limits","nodes","reservations","selections"};
        for(CettaExprIndex i=1u;i<13u;i++) {
            const Atom *x=a->expr.elems[i],*y=b->expr.elems[i];
            if(!atom_is_symbol(x->expr.elems[0],labels[i-1u]))return false;
            if(i==12u) {
                if(x->expr.len!=2u||!prime_replay_projection_push(p,x->expr.elems[1],y->expr.elems[1],PRIME_REPLAY_SELECTIONS,0))return false;
            } else if(!prime_replay_projection_push(p,x,y,PRIME_REPLAY_META,0))return false;
        }
        return true;
    }
    if(n.kind==PRIME_REPLAY_COST) {
        if(!prime_replay_projection_name(c,a->expr.elems[1],"native-events-v1",false)||
            !prime_replay_projection_name(c,b->expr.elems[1],"native-events-v1",true)||
            !prime_replay_projection_fields(p,a,b,2u))return false;
        static const char *labels[]={"scope","work","span","operations","trace-status","read"};
        for(CettaExprIndex i=2u;i<8u;i++) {
            const Atom *x=a->expr.elems[i],*y=b->expr.elems[i];
            if(x->expr.len!=2u||!atom_is_symbol(x->expr.elems[0],labels[i-2u]))return false;
            PrimeReplayProjectionKind kind=i==2u?PRIME_REPLAY_ID:i==7u?PRIME_REPLAY_COST_EVENT:PRIME_REPLAY_META;
            if(i==5u) {
                const Atom *xs=x->expr.elems[1],*ys=y->expr.elems[1];
                if(!prime_replay_projection_expr(p,xs,ys,NULL,CETTA_COST_KIND_COUNT))return false;
                for(size_t j=0u;j<CETTA_COST_KIND_COUNT;j++) {
                    const Atom *row=xs->expr.elems[j],*other=ys->expr.elems[j];
                    if(!prime_replay_projection_expr(p,row,other,"Counter",3u))return false;
                    const Atom *label=row->expr.elems[1];const char *expected=cetta_native_cost_name((CettaNativeCostKind)j);
                    if(!prime_replay_projection_span(c,label,false)||label->kind!=ATOM_GROUNDED||label->ground.gkind!=GV_STRING||
                        !prime_replay_read_bytes(c,"projection.counter_name",label->ground.sval,label->ground.slen,expected,strlen(expected)))return false;
                }
            }
            if(!prime_replay_projection_push(p,x->expr.elems[1],y->expr.elems[1],kind,CETTA_CHECKPOINT_COST_LEDGER))return false;
        }
        return true;
    }
    for(CettaExprIndex i=1u;i<a->expr.len;i++) {
        PrimeReplayProjectionKind kind=PRIME_REPLAY_META;CettaCheckpointIdentityRole role=0;
        if(n.kind==PRIME_REPLAY_GRAPH) {
            if(i==1u){kind=PRIME_REPLAY_ID;role=CETTA_CHECKPOINT_RECEIPT_SESSION;}
            if(i==2u){kind=PRIME_REPLAY_ID;role=CETTA_CHECKPOINT_RECEIPT_NODE;}
            if(i==4u)kind=PRIME_REPLAY_NODES;
        } else if(n.kind==PRIME_REPLAY_NODE) {
            if(i<4u){kind=PRIME_REPLAY_ID;role=CETTA_CHECKPOINT_RECEIPT_NODE;}else kind=PRIME_REPLAY_EVENT;
        } else if(n.kind==PRIME_REPLAY_EVENT) {
            if(i==2u){kind=PRIME_REPLAY_ID;role=CETTA_CHECKPOINT_NEED_SESSION;}
            if(i==3u){kind=PRIME_REPLAY_ID;role=CETTA_CHECKPOINT_NEED_THUNK;}
            if(i==4u){kind=PRIME_REPLAY_ID;role=CETTA_CHECKPOINT_SOURCE_OCCURRENCE;}
            if(i==6u){kind=PRIME_REPLAY_ID;role=CETTA_CHECKPOINT_RULE_OCCURRENCE;}
            if(i==7u){kind=PRIME_REPLAY_ID;role=CETTA_CHECKPOINT_NEED_SERIAL;}
            if(i>=10u)kind=PRIME_REPLAY_DATA;
        } else if(n.kind==PRIME_REPLAY_SELECTION) {
            if(i==2u){kind=PRIME_REPLAY_ID;role=CETTA_CHECKPOINT_ISSUED_OCCURRENCE;}
            if(i>=6u) {
                const Atom *x=a->expr.elems[i],*y=b->expr.elems[i];
                if(!prime_replay_projection_expr(p,x,y,i==6u?"preparation":"operation",2u)||
                    !prime_replay_projection_push(p,x->expr.elems[1],y->expr.elems[1],PRIME_REPLAY_FOREST,0))return false;
                continue;
            }
        } else if(n.kind==PRIME_REPLAY_COST_EVENT&&i<=3u) {
            kind=PRIME_REPLAY_ID;role=i==1u?CETTA_CHECKPOINT_COST_EVENT:
                i==2u?CETTA_CHECKPOINT_COST_LEDGER:CETTA_CHECKPOINT_ISSUED_OCCURRENCE;
        }
        if(!prime_replay_projection_push(p,a->expr.elems[i],b->expr.elems[i],kind,role))return false;
    }
    return true;
}
static bool prime_replay_compare_projection(PrimeReplayComparison *c,CettaLibraryContext *library,
    const Atom *a,const Atom *b,const PrimeNativeControl *left,const PrimeNativeControl *right) {
    if(c->capacity>SIZE_MAX/sizeof(PrimeReplayProjectionNode)||
        !prime_replay_read_service(c,"projection.storage",1u,c->capacity*sizeof(PrimeReplayProjectionNode)))return false;
    PrimeReplayProjection p={.comparison=c,.capacity=c->capacity,.library=library,
        .left_residual=left,.right_residual=right};
    p.nodes=calloc(p.capacity,sizeof(*p.nodes));
    if(!p.nodes)return prime_replay_compare_refuse(c,"projection.storage","ProjectionAllocation");
    bool ok=prime_replay_projection_push(&p,a,b,PRIME_REPLAY_OBSERVATION,0);
    while(ok&&p.at<p.length)ok=prime_replay_projection_step(&p,p.nodes[p.at++]);
    free(p.nodes);
    return ok||prime_replay_compare_refuse(c,"projection.schema","ProjectionMismatch");
}

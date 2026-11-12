/* Complete strict-core frontiers retain the canonical candidate traversal.
 * Candidates are built before their scratch view is retired. Stable key
 * sorting and duplicate elimination precede publication of the final tuple. */
typedef enum {
    RHO_FRONTIER_SOURCE,
    RHO_FRONTIER_ENUMERATE,
    RHO_FRONTIER_KEY,
    RHO_FRONTIER_STORE,
    RHO_FRONTIER_SORT,
    RHO_FRONTIER_DISTINCT,
    RHO_FRONTIER_ASSEMBLE_BEGIN,
    RHO_FRONTIER_ASSEMBLE,
    RHO_FRONTIER_EXPORT
} RhoFrontierPhase;

typedef struct RhoFrontierKeyOwner {
    struct RhoFrontierKeyOwner *next;
    RhoKeyProduct *product;
} RhoFrontierKeyOwner;

struct RhocalcFrontier {
    RhocalcRun *source;
    Arena *receiver;
    RhoSortBuffer items;
    RhoStableSort sort;
    RhoProcessKeyCursor key;
    RhoKeyProduct *product;
    RhoFrontierKeyOwner *keys;
    RhoKeyProbe duplicate;
    AtomExprAssembly *assembly;
    AtomDeepCopySession *export;
    Atom *pending, *tuple, *result;
    const char *last_key;
    uint64_t steps, key_steps, export_steps, children;
    uint32_t count, position, distinct;
    RhoFrontierPhase phase;
    RhocalcRunStatus status;
    bool product_owned, sort_ready, duplicate_ready, retiring;
    char error[256];
};

static bool rho_frontier_fail(RhocalcFrontier *frontier) {
    const char *reason=rhocalc_run_error(frontier->source);
    if(!reason)reason=rhocalc_last_validation_error();
    snprintf(frontier->error,sizeof(frontier->error),"%s",
        reason && *reason ? reason : "rho frontier transition failed");
    frontier->status=RHOCALC_RUN_FAILED;
    return false;
}

RhocalcFrontier *rhocalc_frontier_open_held(Arena *arena,Atom *proc) {
    if(!arena || !proc)return NULL;
    RhocalcFrontier *frontier=calloc(1u,sizeof(*frontier));
    if(!frontier)return NULL;
    RhoRuntimeProfile profile={.scheduler_policy=RHO_SCHEDULER_CANONICAL,
        .reduction_limit=1u,.thread_count=1u};
    frontier->source=rho_run_open(arena,proc,&profile,NULL,false,true);
    if(!frontier->source) {free(frontier);return NULL;}
    frontier->receiver=arena;
    frontier->items.paged=true;
    frontier->status=RHOCALC_RUN_PENDING;
    return frontier;
}

static bool rho_frontier_advance(RhocalcFrontier *frontier) {
    RhocalcRun *source=frontier->source;
    switch(frontier->phase) {
    case RHO_FRONTIER_SOURCE:
        if(source->phase==RHO_RUN_SELECT) {
            rho_canonical_cursor_init(&source->cursor,&source->machine);
            source->cursor.enumerate=true;
            source->cursor_live=true;
            frontier->phase=RHO_FRONTIER_ENUMERATE;
            return true;
        }
        if(rhocalc_run_poll(source,1u,NULL)==RHOCALC_RUN_FAILED)
            return rho_frontier_fail(frontier);
        return true;
    case RHO_FRONTIER_ENUMERATE:
        if(!rho_run_charge(&source->account.selector_steps,1u) ||
            !rho_canonical_cursor_advance(&source->cursor))return rho_frontier_fail(frontier);
        if(source->cursor.yielded) {
            frontier->pending=source->cursor.next;
            if(!frontier->pending)return rho_frontier_fail(frontier);
            frontier->phase=RHO_FRONTIER_KEY;
        } else if(source->cursor.done) {
            if(!rho_stable_sort_init(&frontier->sort,frontier->items,frontier->count,false))
                return rho_frontier_fail(frontier);
            frontier->sort_ready=true;
            frontier->items=(RhoSortBuffer){0};
            frontier->phase=RHO_FRONTIER_SORT;
        }
        return true;
    case RHO_FRONTIER_KEY:
        if(!frontier->key.atom && !rho_process_key_cursor_begin(&frontier->key,
                &source->machine.process_key_cache,frontier->pending,false))return rho_frontier_fail(frontier);
        if(!rho_process_key_cursor_advance(&frontier->key,&frontier->key_steps))return rho_frontier_fail(frontier);
        if(frontier->key.done) {
            frontier->product=rho_process_key_cursor_take(&frontier->key,&frontier->product_owned);
            rho_process_key_cursor_free(&frontier->key);
            if(!frontier->product)return rho_frontier_fail(frontier);
            frontier->phase=RHO_FRONTIER_STORE;
        }
        return true;
    case RHO_FRONTIER_STORE: {
        if(frontier->count==UINT32_MAX)return rho_frontier_fail(frontier);
        CettaSegmentedStep prepared=rho_sort_buffer_prepare(&frontier->items,
            frontier->count,sizeof(RhoKeyedAtom));
        if(prepared==CETTA_SEGMENTED_REFUSED)return rho_frontier_fail(frontier);
        if(prepared==CETTA_SEGMENTED_PENDING)return true;
        if(frontier->product_owned) {
            RhoFrontierKeyOwner *owner=malloc(sizeof(*owner));
            if(!owner)return rho_frontier_fail(frontier);
            *owner=(RhoFrontierKeyOwner){.next=frontier->keys,.product=frontier->product};
            frontier->keys=owner;
            frontier->product_owned=false;
        }
        *rho_keyed_row(&frontier->items,frontier->count)=(RhoKeyedAtom){
            .atom=frontier->pending,.key=frontier->product->text,.ordinal=frontier->count};
        frontier->count++;
        frontier->pending=NULL;frontier->product=NULL;
        source->cursor.yielded=false;source->cursor.next=NULL;
        frontier->phase=RHO_FRONTIER_ENUMERATE;
        return true;
    }
    case RHO_FRONTIER_SORT:
        if(!rho_stable_sort_advance(&frontier->sort))return rho_frontier_fail(frontier);
        if(frontier->sort.done)frontier->phase=RHO_FRONTIER_DISTINCT;
        return true;
    case RHO_FRONTIER_DISTINCT: {
        if(frontier->position==frontier->count) {
            frontier->position=0u;frontier->phase=RHO_FRONTIER_ASSEMBLE_BEGIN;
            return true;
        }
        RhoKeyedAtom *row=rho_keyed_row(&frontier->sort.items,frontier->position);
        if(frontier->last_key) {
            if(!frontier->duplicate_ready) {
                if(!rho_key_probe_init(&frontier->duplicate,frontier->last_key,row->key))
                    return rho_frontier_fail(frontier);
                frontier->duplicate_ready=true;
            }
            rho_key_probe_advance(&frontier->duplicate);
            if(!frontier->duplicate.done)return true;
            row->binders=frontier->duplicate.order!=0;
            frontier->duplicate_ready=false;
        } else row->binders=1u;
        if(row->binders) {frontier->last_key=row->key;frontier->distinct++;}
        frontier->position++;
        return true;
    }
    case RHO_FRONTIER_ASSEMBLE_BEGIN:
        if(!frontier->distinct) {
            frontier->tuple=atom_unit(source->machine.arena);
            frontier->phase=RHO_FRONTIER_EXPORT;
        } else {
            frontier->assembly=atom_expr_assembly_new(source->machine.arena,frontier->distinct);
            if(!frontier->assembly)return rho_frontier_fail(frontier);
            frontier->phase=RHO_FRONTIER_ASSEMBLE;
        }
        return true;
    case RHO_FRONTIER_ASSEMBLE:
        if(frontier->position<frontier->count) {
            RhoKeyedAtom *row=rho_keyed_row(&frontier->sort.items,frontier->position++);
            if(row->binders && (!atom_expr_assembly_append(frontier->assembly,row->atom) ||
                    !rho_run_charge(&frontier->children,1u)))return rho_frontier_fail(frontier);
        } else {
            frontier->tuple=atom_expr_assembly_finish(frontier->assembly);
            atom_expr_assembly_free(frontier->assembly);frontier->assembly=NULL;
            if(!frontier->tuple)return rho_frontier_fail(frontier);
            frontier->phase=RHO_FRONTIER_EXPORT;
        }
        return true;
    case RHO_FRONTIER_EXPORT: {
        if(!frontier->export) {
            frontier->export=atom_deep_copy_session_new(frontier->receiver);
            if(!frontier->export)return rho_frontier_fail(frontier);
            return true;
        }
        uint64_t used=0u;
        AtomDeepCopyStep copied=atom_deep_copy_session_advance(frontier->export,
            frontier->tuple,1u,&frontier->result,&used);
        if(used>1u || !rho_run_charge(&frontier->export_steps,used) || copied==ATOM_DEEP_COPY_REFUSED)
            return rho_frontier_fail(frontier);
        if(copied==ATOM_DEEP_COPY_COMPLETE) {
            atom_deep_copy_session_free(frontier->export);frontier->export=NULL;
            frontier->status=RHOCALC_RUN_DONE;
        }
        return true;
    }
    }
    return rho_frontier_fail(frontier);
}

RhocalcRunStatus rhocalc_frontier_poll(RhocalcFrontier *frontier,uint64_t allowance,uint64_t *used) {
    uint64_t spent=0u;
    if(used)*used=0u;
    if(!frontier || frontier->retiring)return RHOCALC_RUN_FAILED;
    if(allowance && frontier->status==RHOCALC_RUN_PENDING)rho_validation_clear();
    while(spent<allowance && frontier->status==RHOCALC_RUN_PENDING) {
        if(!rho_run_charge(&frontier->steps,1u)) {rho_frontier_fail(frontier);break;}
        spent++;
        if(!rho_frontier_advance(frontier))break;
    }
    if(used)*used=spent;
    return frontier->status;
}

bool rhocalc_frontier_result(const RhocalcFrontier *frontier,Atom **tuple) {
    if(!frontier || frontier->retiring || !tuple || frontier->status!=RHOCALC_RUN_DONE)return false;
    *tuple=frontier->result;
    return true;
}

RhocalcRunAccount rhocalc_frontier_account(const RhocalcFrontier *frontier) {
    if(!frontier)return (RhocalcRunAccount){0};
    RhocalcRunAccount account=rhocalc_run_account(frontier->source);
    account.driver_steps=frontier->steps;
    account.export_copy_steps+=frontier->export_steps;
    account.selector_memo_steps+=frontier->key_steps;
    account.construction_children+=frontier->children;
    return account;
}

const char *rhocalc_frontier_error(const RhocalcFrontier *frontier) {
    return frontier && frontier->error[0] ? frontier->error : NULL;
}

bool rhocalc_frontier_invalid_input(const RhocalcFrontier *frontier) {
    return frontier && !frontier->retiring && frontier->status==RHOCALC_RUN_FAILED &&
        frontier->source && frontier->source->check.status==RHO_CHECK_INCOMPATIBLE;
}

CettaOwnedReleaseStep rhocalc_frontier_release_poll(RhocalcFrontier **owner,
    uint64_t allowance,bool allow_atomic,uint64_t *used) {
    uint64_t spent=0u;
    if(used)*used=0u;
    if(!owner || !*owner)return CETTA_OWNED_RELEASE_COMPLETE;
    RhocalcFrontier *frontier=*owner;
    if(!allowance)return CETTA_OWNED_RELEASE_PENDING;
    if(!frontier->retiring) {frontier->retiring=true;spent++;}
    while(spent<allowance) {
        if(rho_sort_buffer_has_storage(&frontier->items))rho_sort_buffer_release_one(&frontier->items);
        else if(rho_sort_buffer_has_storage(&frontier->sort.items))rho_sort_buffer_release_one(&frontier->sort.items);
        else if(rho_sort_buffer_has_storage(&frontier->sort.scratch))rho_sort_buffer_release_one(&frontier->sort.scratch);
        else {
            if(!allow_atomic) {if(used)*used=spent;return CETTA_OWNED_RELEASE_ATOMIC;}
            if(frontier->keys) {
                RhoFrontierKeyOwner *key=frontier->keys;frontier->keys=key->next;
                rho_key_product_free(key->product);free(key);
            } else {
                rho_process_key_cursor_free(&frontier->key);
                if(frontier->product_owned)rho_key_product_free(frontier->product);
                frontier->product=NULL;frontier->product_owned=false;
                atom_expr_assembly_free(frontier->assembly);frontier->assembly=NULL;
                atom_deep_copy_session_free(frontier->export);frontier->export=NULL;
                uint64_t source_spent=0u;
                CettaOwnedReleaseStep status=rhocalc_run_release_poll(&frontier->source,
                    allowance-spent,allow_atomic,&source_spent);
                spent+=source_spent;
                if(status!=CETTA_OWNED_RELEASE_COMPLETE) {if(used)*used=spent;return status;}
                free(frontier);*owner=NULL;
                if(used)*used=spent;
                return CETTA_OWNED_RELEASE_COMPLETE;
            }
        }
        spent++;
    }
    if(used)*used=spent;
    return CETTA_OWNED_RELEASE_PENDING;
}

void rhocalc_frontier_close(RhocalcFrontier *frontier) {
    while(rhocalc_frontier_release_poll(&frontier,UINT64_MAX,true,NULL)!=CETTA_OWNED_RELEASE_COMPLETE) {}
}

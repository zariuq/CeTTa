typedef struct {const void *left,*right;} PrimeReplayAddressPair;
typedef struct {const Arena *left,*right;} PrimeReplayArenaPair;
typedef struct {
    CettaCheckpointIdentityRole role;
    /* Synchronous owning-edge evidence, never inherited as prefix authority. */
    bool frame_owned;
    /* Positions only: every use rereads the current scope and entry. */
    uint32_t frame_left_hint,frame_right_hint;
    uint64_t left,right;
} PrimeReplayIdentityPair;
typedef struct {
    const char *field;
    uint64_t left,right;
    size_t first_ordinal,repetitions;
} PrimeReplayPhysicalField;
typedef struct {
    PrimeReplayIdentityPair *items;
    size_t length,capacity;
} PrimeReplayIdentityRelation;
struct PrimeReplayComparison {
    CettaCheckpointCompare read;
    PrimeReplayAddressPair *addresses,*atoms;
    unsigned char *atom_domains;
    PrimeReplayArenaPair *arenas;
    PrimeReplayPhysicalField *physical;
    PrimeReplayIdentityRelation identities;
    size_t address_count,atom_count,atom_at,arena_count,arena_at,physical_count,physical_length;
    size_t capacity,physical_capacity,bytes_limit,scratch_reserved,global_reserved;
    uint64_t fuel,reads,probes,services;
    CettaCheckpointIssuance left_begin,left_end,right_begin,right_end;
    const PrimeReplaySources *sources;
    const PrimeNativeControl *left,*right;
    PrimeReplayAddressPair targets[CETTA_SPACE_AGENDA_REGION_COUNT];
    SpaceReadToken left_target,right_target;
    const PrimeReplayTargetOrigin *target_copy;
    bool transaction,targets_ready;
    AtomDeepCopySession *globals;
    Arena inspection;
    const char *refusal,*field;
    bool ready,matched,accounted;
};

static bool prime_replay_compare_refuse(PrimeReplayComparison *c,const char *field,const char *why) {
    if(!c->refusal){c->refusal=why;c->field=field;}
    return false;
}
static bool prime_replay_compare_tick(PrimeReplayComparison *c,const char *field,uint64_t work,bool probe) {
    if(c->refusal)return false;
    if(work>c->fuel)return prime_replay_compare_refuse(c,field,"InspectionLimit");
    c->fuel-=work;
    if(probe)c->probes+=work;else c->reads+=work;
    return true;
}
static bool prime_replay_read_scalar(void *raw,const char *field,uint64_t a,uint64_t b) {
    PrimeReplayComparison *c=raw;
    return prime_replay_compare_tick(c,field,1u,false)&&
        (a==b||prime_replay_compare_refuse(c,field,"ScalarMismatch"));
}
static bool prime_replay_read_address(void *raw,const char *field,const void *a,const void *b) {
    PrimeReplayComparison *c=raw;
    if(!prime_replay_compare_tick(c,field,1u,false))return false;
    if(!a||!b)return (!a&&!b)||prime_replay_compare_refuse(c,field,"NullAddressMismatch");
    for(size_t i=0u;i<c->address_count;i++) {
        if(!prime_replay_compare_tick(c,field,1u,true))return false;
        PrimeReplayAddressPair p=c->addresses[i];
        if(p.left==a||p.right==b)return (p.left==a&&p.right==b)||
            prime_replay_compare_refuse(c,field,"AddressAliasMismatch");
    }
    if(c->address_count==c->capacity)return prime_replay_compare_refuse(c,field,"AddressMapCapacity");
    c->addresses[c->address_count++]=(PrimeReplayAddressPair){a,b};return true;
}
static bool prime_replay_read_target(void *raw,const char *field,
    const Space *a,uint64_t ai,const Space *b,uint64_t bi) {
    PrimeReplayComparison *c=raw;
    if(!prime_replay_compare_tick(c,field,1u,false))return false;
    if(!a||!b)return (!a&&!b&&!ai&&!bi)||prime_replay_compare_refuse(c,field,"NullTargetMismatch");
    if(!c->transaction||!c->targets_ready)return prime_replay_compare_refuse(c,field,"TargetRoleUnadmitted");
    for(size_t i=0u;i<CETTA_SPACE_AGENDA_REGION_COUNT;i++) {
        if(!prime_replay_compare_tick(c,field,1u,true))return false;
        PrimeReplayAddressPair p=c->targets[i];
        if(p.left==a||p.right==b) {
            if(p.left!=a||p.right!=b)return prime_replay_compare_refuse(c,field,"TargetAliasMismatch");
            if(!ai||!bi||ai==UINT64_MAX||bi==UINT64_MAX ||
                (!i && (ai!=c->left_target.instance_id||bi!=c->right_target.instance_id)))
                return prime_replay_compare_refuse(c,field,"TargetInstanceRole");
            return true;
        }
    }
    return prime_replay_compare_refuse(c,field,"UnheldTargetRole");
}

static bool prime_replay_targets(PrimeReplayComparison *c,
    const PrimeNativeControl *a,const PrimeNativeControl *b) {
    if(!c->transaction)return !a->space_agenda&&!b->space_agenda;
    const Space *left[CETTA_SPACE_AGENDA_REGION_COUNT],*right[CETTA_SPACE_AGENDA_REGION_COUNT];
    if(!cetta_space_agenda_checkpoint_regions(a->space_agenda,left) ||
        !cetta_space_agenda_checkpoint_regions(b->space_agenda,right))return false;
    for(size_t i=0u;i<CETTA_SPACE_AGENDA_REGION_COUNT;i++) {
        if(!prime_replay_compare_tick(c,"target.owner_role",1u,false))return false;
        if(c->targets_ready && (c->targets[i].left!=left[i]||c->targets[i].right!=right[i]))return false;
        for(size_t j=0u;j<i;j++)if(left[i]==left[j]||right[i]==right[j])return false;
        c->targets[i]=(PrimeReplayAddressPair){left[i],right[i]};
    }
    c->left_target=space_read_token(left[0]);c->right_target=space_read_token(right[0]);
    c->targets_ready=true;
    return true;
}

static CettaCheckpointIdentityRole prime_replay_identity_namespace(CettaCheckpointIdentityRole role) {
    switch(role) {
    case CETTA_CHECKPOINT_NEED_IMPORT:return CETTA_CHECKPOINT_NEED_STORAGE;
    case CETTA_CHECKPOINT_BINDING_OCCURRENCE:
    case CETTA_CHECKPOINT_WEIGHT_OCCURRENCE:
    case CETTA_CHECKPOINT_WEIGHT_HANDLER:return CETTA_CHECKPOINT_SOURCE_OCCURRENCE;
    default:return role;
    }
}
static bool prime_replay_read_identity(void *raw,const char *field,CettaCheckpointIdentityRole role,
    uint64_t a,uint64_t b) {
    PrimeReplayComparison *c=raw;role=prime_replay_identity_namespace(role);
    if(!prime_replay_compare_tick(c,field,1u,false))return false;
    if(role>=CETTA_CHECKPOINT_IDENTITY_ROLE_COUNT)return prime_replay_compare_refuse(c,field,"IdentityRole");
    if(role==CETTA_CHECKPOINT_NEED_THUNK) {
        bool an=(a&PRIME_NEED_NAMED_CELL_BIT)!=0u,bn=(b&PRIME_NEED_NAMED_CELL_BIT)!=0u;
        if(an!=bn)return prime_replay_compare_refuse(c,field,"NeedIdentityDomain");
        if(an)role=CETTA_CHECKPOINT_NEED_NAMED_CELL;
    }
    if(!a||!b)return (!a&&!b)||prime_replay_compare_refuse(c,field,"ZeroIdentityMismatch");
    for(size_t i=0u;i<c->identities.length;i++) {
        if(!prime_replay_compare_tick(c,field,1u,true))return false;
        PrimeReplayIdentityPair p=c->identities.items[i];
        if(p.role==role&&(p.left==a||p.right==b))return (p.left==a&&p.right==b)||
            prime_replay_compare_refuse(c,field,"IdentityAliasMismatch");
    }
    if(c->identities.length==c->identities.capacity)return prime_replay_compare_refuse(c,field,"IdentityMapCapacity");
    c->identities.items[c->identities.length++]=(PrimeReplayIdentityPair){.role=role,.left=a,.right=b};return true;
}
static bool prime_replay_read_arena(void *raw,const char *field,const Arena *a,const Arena *b) {
    PrimeReplayComparison *c=raw;
    if(!prime_replay_read_address(raw,field,a,b))return false;
    if(!a)return true;
    for(size_t i=0u;i<c->arena_count;i++) {
        if(!prime_replay_compare_tick(c,field,1u,true))return false;
        PrimeReplayArenaPair p=c->arenas[i];
        if(p.left==a||p.right==b)return (p.left==a&&p.right==b)||
            prime_replay_compare_refuse(c,field,"ArenaAliasMismatch");
    }
    if(c->arena_count==c->capacity)return prime_replay_compare_refuse(c,field,"ArenaMapCapacity");
    c->arenas[c->arena_count++]=(PrimeReplayArenaPair){a,b};
    return a->identity&&b->identity&&
        prime_replay_read_identity(raw,field,CETTA_CHECKPOINT_ARENA,a->identity,b->identity);
}
enum { PRIME_REPLAY_ATOM_REGISTRY=1u, PRIME_REPLAY_ATOM_ORDINARY=2u };
static bool prime_replay_read_atom_domain(PrimeReplayComparison *c,const char *field,
    const Atom *a,const Atom *b,unsigned char domain) {
    if(!prime_replay_read_address(c,field,a,b))return false;
    if(!a)return true;
    for(size_t i=0u;i<c->atom_count;i++) {
        if(!prime_replay_compare_tick(c,field,1u,true))return false;
        if(c->atoms[i].left==a) {
            if(c->atoms[i].right!=b)return false;
            unsigned char previous=c->atom_domains[i];
            c->atom_domains[i]|=domain;
            /* An ordinary root wins over an earlier attachment path.
             * Revisit already-read descendants under the stronger domain. */
            if(!(previous&PRIME_REPLAY_ATOM_ORDINARY)&&
                (domain&PRIME_REPLAY_ATOM_ORDINARY)&&i<c->atom_at)c->atom_at=i;
            return true;
        }
    }
    if(c->atom_count==c->capacity)return prime_replay_compare_refuse(c,field,"AtomMapCapacity");
    c->atom_domains[c->atom_count]=domain;
    c->atoms[c->atom_count++]=(PrimeReplayAddressPair){a,b};return true;
}
static bool prime_replay_read_atom(void *raw,const char *field,const Atom *a,const Atom *b) {
    return prime_replay_read_atom_domain(raw,field,a,b,PRIME_REPLAY_ATOM_ORDINARY);
}
static bool prime_replay_read_retained_atom(void *raw,const char *field,
    CettaCheckpointRetainedAtomRole role,const Atom *a,const Atom *b) {
    if(!prime_replay_compare_tick(raw,"atom.retained_role",1u,false))return false;
    if(role!=CETTA_CHECKPOINT_REGISTRY_ATTACHMENT)
        return prime_replay_compare_refuse(raw,field,"RetainedAtomRole");
    return prime_replay_read_atom_domain(raw,field,a,b,PRIME_REPLAY_ATOM_REGISTRY);
}
static bool prime_replay_read_physical(void *raw,const char *field,uint64_t a,uint64_t b) {
    PrimeReplayComparison *c=raw;
    if(!prime_replay_compare_tick(c,field,1u,false))return false;
    if(c->physical_count==SIZE_MAX)
        return prime_replay_compare_refuse(c,field,"PhysicalOrdinalOverflow");
    size_t ordinal=c->physical_count+1u;
    if(c->physical_length) {
        if(!prime_replay_compare_tick(c,"physical.run_compare",1u,true))return false;
        PrimeReplayPhysicalField *last=&c->physical[c->physical_length-1u];
        /* Exact payload and consecutive ordinals only. A gap, another site
         * or changed side/value remains a separate chronological run. */
        if(last->field==field&&last->left==a&&last->right==b&&
            last->repetitions<SIZE_MAX&&last->first_ordinal<=SIZE_MAX-last->repetitions&&
            last->first_ordinal+last->repetitions==ordinal) {
            last->repetitions++;c->physical_count++;return true;
        }
    }
    if(!prime_replay_compare_tick(c,"physical.run_append",1u,false))return false;
    if(c->physical_length==c->physical_capacity) {
        size_t extra=c->physical_capacity?c->physical_capacity:32u;
        size_t available=(c->bytes_limit-c->scratch_reserved)/sizeof(*c->physical);
        if(extra>available)extra=available;
        if(!extra||extra>SIZE_MAX-c->physical_capacity||
            c->physical_capacity+extra>SIZE_MAX/sizeof(*c->physical))
            return prime_replay_compare_refuse(c,field,"PhysicalObservationCapacity");
        size_t capacity=c->physical_capacity+extra;
        /* One actual encoded-buffer growth attempt. Each underlying read
         * keeps its original fuel/read charge, including a refused append. */
        cetta_native_cost_charge(CETTA_COST_STORAGE_PROBE,1u);
        PrimeReplayPhysicalField *rows=realloc(c->physical,capacity*sizeof(*rows));
        if(!rows)return prime_replay_compare_refuse(c,field,"PhysicalObservationAllocation");
        c->physical=rows;c->physical_capacity=capacity;
        c->bytes_limit-=extra*sizeof(*rows);
    }
    c->physical[c->physical_length++]=(PrimeReplayPhysicalField){field,a,b,ordinal,1u};
    c->physical_count++;return true;
}
static bool prime_replay_frame_scope_contains(PrimeReplayComparison *c,
    const CettaFrameIdentityScope *scope,uint32_t identity,bool right,
    uint32_t hint,uint32_t *position) {
    if(!prime_replay_compare_tick(c,"frame_scope.header",1u,false))return false;
    if(!scope)return prime_replay_compare_refuse(c,"frame_scope.owner","MissingFrameScope");
#define FS_P(n,v) do {uint64_t value=(uint64_t)(v);if(!prime_replay_read_physical(c,n,right?0u:value,right?value:0u))return false;}while(0)
    FS_P("frame_scope.owner",(uintptr_t)scope);
    FS_P("frame_scope.entries",(uintptr_t)scope->identities);
    FS_P("frame_scope.length",scope->len);FS_P("frame_scope.capacity",scope->cap);
    const size_t inline_capacity=sizeof(scope->inline_identities)/sizeof(*scope->inline_identities);
    if(scope->len>scope->cap || (!scope->identities&&(scope->len||scope->cap)) ||
        (scope->identities==scope->inline_identities&&scope->cap!=inline_capacity) ||
        (scope->identities&&scope->identities!=scope->inline_identities&&
            (scope->cap<=inline_capacity||(scope->cap&(scope->cap-1u)))))
        return prime_replay_compare_refuse(c,"frame_scope.inventory","InvalidFrameScope");
    /* A hint can be stale after growth, relocation or reordering. It grants
     * nothing until this actual initialized entry has been read again. */
    if(hint<scope->len) {
        FS_P("frame_scope.index",hint);
        uint32_t held=scope->identities[hint];FS_P("frame_scope.identity",held);
        if(held==identity){*position=hint;return true;}
    }
    for(uint32_t i=0u;i<scope->len;i++) {
        FS_P("frame_scope.index",i);
        uint32_t held=scope->identities[i];FS_P("frame_scope.identity",held);
        if(held==identity){*position=i;return true;}
    }
    return prime_replay_compare_refuse(c,"frame_scope.membership","UnheldFrameScopeGeneration");
#undef FS_P
}
static bool prime_replay_read_frame_scope(void *raw,const char *field,
    const CettaFrameIdentityScope *a,uint64_t ai,
    const CettaFrameIdentityScope *b,uint64_t bi) {
    PrimeReplayComparison *c=raw;
    if(!prime_replay_compare_tick(c,field,1u,false))return false;
    if(!ai||!bi||ai>UINT32_MAX||bi>UINT32_MAX)
        return prime_replay_compare_refuse(c,field,"FrameScopeIdentity");
    for(size_t i=0u;i<c->identities.length;i++) {
        if(!prime_replay_compare_tick(c,"frame_scope.relation",1u,true))return false;
        PrimeReplayIdentityPair *p=&c->identities.items[i];
        if(p->role==CETTA_CHECKPOINT_FRAME&&p->left==ai&&p->right==bi) {
            uint32_t left=p->frame_owned?p->frame_left_hint:UINT32_MAX;
            uint32_t right=p->frame_owned?p->frame_right_hint:UINT32_MAX;
            if(!prime_replay_frame_scope_contains(c,a,(uint32_t)ai,false,left,&left)||
                !prime_replay_frame_scope_contains(c,b,(uint32_t)bi,true,right,&right))return false;
            p->frame_left_hint=left;p->frame_right_hint=right;
            p->frame_owned=true;return true;
        }
    }
    return prime_replay_compare_refuse(c,field,"MissingFrameIdentityPair");
}

static bool prime_replay_read_bytes(void *raw,const char *field,const void *left,size_t an,
    const void *right,size_t bn) {
    PrimeReplayComparison *c=raw;
    if(!prime_replay_compare_tick(c,field,1u,false))return false;
    if(an!=bn||((an!=0u)&&(!left||!right)))return prime_replay_compare_refuse(c,field,"ByteExtentMismatch");
    const unsigned char *a=left,*b=right;
    for(size_t i=0u;i<an;i++) {
        if(!prime_replay_compare_tick(c,field,1u,false))return false;
        if(a[i]!=b[i])return prime_replay_compare_refuse(c,field,"ByteMismatch");
        if(an==SIZE_MAX&&!a[i])return true;
    }
    return an!=SIZE_MAX||prime_replay_compare_refuse(c,field,"TextExtent");
}
static bool prime_replay_read_service(void *raw,const char *field,size_t work,size_t scratch) {
    PrimeReplayComparison *c=raw;
    if(!prime_replay_compare_tick(c,field,1u,false))return false;
    if(work>c->fuel)return prime_replay_compare_refuse(c,field,"ServiceInspectionLimit");
    if(scratch>c->bytes_limit-c->scratch_reserved)return prime_replay_compare_refuse(c,field,"ServiceScratchLimit");
    c->fuel-=work;c->scratch_reserved+=scratch;c->services++;
    return true;
}

static void prime_replay_read_service_release(void *raw,size_t bytes) {
    PrimeReplayComparison *c=raw;
    /* This is unconditional cleanup, including after a paid field refusal. */
    assert(bytes<=c->scratch_reserved);
    c->scratch_reserved-=bytes;
}

/* Reserve a finite service purse before allocation. Only the metered
 * global-domain service may settle this reservation; coarse provider bounds
 * still use read_service unchanged. The inspection metric includes every
 * memo probe, so unused allowance is not confused with uncounted traversal. */
static bool prime_replay_global_reserve(PrimeReplayComparison *c,const char *field,
    size_t ceiling,size_t scratch,size_t *granted) {
    *granted=0u;
    if(!prime_replay_compare_tick(c,field,1u,false))return false;
    if(scratch>c->bytes_limit-c->scratch_reserved)
        return prime_replay_compare_refuse(c,field,"ServiceScratchLimit");
    size_t available=c->fuel>SIZE_MAX?SIZE_MAX:(size_t)c->fuel;
    if(ceiling<available)available=ceiling;
    if(!available)return prime_replay_compare_refuse(c,field,"ServiceInspectionLimit");
    c->fuel-=available;c->scratch_reserved+=scratch;c->services++;
    *granted=available;return true;
}
static void prime_replay_global_settle(PrimeReplayComparison *c,size_t granted,
    const AtomDeepCopyGlobalInspection *observed) {
    assert(observed->index_visits<=granted &&
        observed->analysis_steps<=granted-observed->index_visits);
    size_t spent=observed->index_visits+observed->analysis_steps;
    assert(granted-spent<=UINT64_MAX-c->fuel);
    c->fuel+=granted-spent;
    cetta_native_cost_charge(CETTA_COST_INDEX_VISIT,observed->index_visits);
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,observed->analysis_steps);
}

static const Arena *prime_replay_read_span(PrimeReplayComparison *c,const void *pointer,
    size_t bytes,bool right) {
    for(size_t i=0u;i<c->arena_count;i++) {
        if(!prime_replay_compare_tick(c,"held.span_owner",1u,true))return NULL;
        const Arena *owner=right?c->arenas[i].right:c->arenas[i].left;
        if(arena_checkpoint_span(owner,pointer,bytes,&c->read,right))return owner;
        if(c->refusal)return NULL;
    }
    return NULL;
}
/* Spelled symbols own their text in the held arena. Validate that payload
 * before any semantic accessor can follow it; registration is not a read. */
static bool prime_replay_read_symbol_payload(PrimeReplayComparison *c,
    const Atom *atom,bool right) {
    if(atom->arena_id==ATOM_ARENA_ID_FORWARDED)
        return prime_replay_compare_refuse(c,"atom.symbol","ForwardedSymbol");
    if(!atom_symbol_is_spelled(atom))return true;
    const CettaSymbolSpelling *text=atom->spelling;
    if(!prime_replay_read_span(c,text,sizeof(*text),right))
        return prime_replay_compare_refuse(c,"atom.spelling","UnheldSpelling");
    size_t length=text->len;
    if(length>SIZE_MAX-sizeof(*text)-1u ||
        !prime_replay_read_span(c,text,sizeof(*text)+length+1u,right))
        return prime_replay_compare_refuse(c,"atom.spelling","UnheldSpellingBytes");
    return true;
}
/* Payload ownership was checked by the Atom/projection visitor. This
 * comparison also supports distinct field labels without treating them as
 * a refusal; byte work is charged even when no registration exists. */
static bool prime_replay_symbols_equal(PrimeReplayComparison *c,
    const Atom *a,const Atom *b,bool *equal) {
    *equal=false;
    if(!prime_replay_compare_tick(c,"projection.symbol",1u,false))return false;
    if(!atom_symbol_is_spelled(a)&&!atom_symbol_is_spelled(b)) {
        *equal=a->symbol_number==b->symbol_number;return true;
    }
    size_t length=atom_symbol_length(a);
    if(length!=atom_symbol_length(b))return true;
    const char *left=atom_symbol_bytes(a),*right=atom_symbol_bytes(b);
    for(size_t i=0u;i<length;i++) {
        if(!prime_replay_compare_tick(c,"projection.symbol_byte",1u,false))return false;
        if(left[i]!=right[i])return true;
    }
    *equal=true;return true;
}
static bool prime_replay_read_symbol(PrimeReplayComparison *c,const Atom *a,const Atom *b) {
    bool equal=false;
    return prime_replay_symbols_equal(c,a,b,&equal)&&
        (equal||prime_replay_compare_refuse(c,"projection.symbol","SymbolMismatch"));
}
static bool prime_replay_read_arena_inventory(PrimeReplayComparison *c) {
    while(c->arena_at<c->arena_count) {
        PrimeReplayArenaPair p=c->arenas[c->arena_at++];
        if(!arena_checkpoint_cache_compare(p.left,p.right,&c->read)||
            !arena_checkpoint_frame_holds(p.left,&c->read,false)||
            !arena_checkpoint_frame_holds(p.right,&c->read,true))
            return prime_replay_compare_refuse(c,"held.arena_inventory","ArenaInventory");
    }
    return true;
}
static bool prime_replay_read_source_space(PrimeReplayComparison *c,const void *a,const void *b) {
    /* These are references to a held source service, not copied mutable
     * worlds. Pointer membership is checked before any Space dereference;
     * the full captured read views are validated around the comparison. */
    if(!a||a!=b)return prime_replay_compare_refuse(c,"atom.source_space","SourceSpaceMismatch");
    for(size_t i=0u;i<c->sources->count;i++) {
        if(!prime_replay_compare_tick(c,"atom.source_space_inventory",1u,true))return false;
        if(c->sources->spaces[i]==a)
            return prime_replay_read_scalar(c,"atom.source_space_index",i,i)&&
                prime_replay_read_address(c,"atom.source_space_owner",a,b);
    }
    if(c->sources->external_target==a)
        return prime_replay_read_scalar(c,"atom.external_target_instance",
            c->sources->target_instance,c->sources->external_target->instance_id) &&
            prime_replay_read_address(c,"atom.external_target_owner",a,b);
    return prime_replay_compare_refuse(c,"atom.source_space","UnheldSourceSpace");
}
static bool prime_replay_read_atom_graph(PrimeReplayComparison *c) {
    if(!prime_replay_read_arena_inventory(c))return false;
    while(c->atom_at<c->atom_count) {
        size_t current=c->atom_at++;
        const Atom *a=c->atoms[current].left,*b=c->atoms[current].right;
        unsigned char domain=c->atom_domains[current];
        const Arena *ao=prime_replay_read_span(c,a,sizeof(*a),false);
        const Arena *bo=prime_replay_read_span(c,b,sizeof(*b),true);
        if(c->refusal)return false;
        bool ag=!ao&&atom_checkpoint_global_contains(c->globals,a,&c->read);
        bool bg=!bo&&atom_checkpoint_global_contains(c->globals,b,&c->read);
        if(c->refusal)return false;
        if((!ao&&!ag)||(!bo&&!bg)||ag!=bg||(ag&&a!=b))
            return prime_replay_compare_refuse(c,"atom.owner","UnheldAtomDomain");
        if(!prime_replay_read_scalar(c,"atom.kind",a->kind,b->kind)||
            !prime_replay_read_scalar(c,"atom.flags",a->flags,b->flags)||
            !prime_replay_read_scalar(c,"atom.structural_facts",a->structural_facts,b->structural_facts)||
            !prime_replay_read_scalar(c,"atom.symbol",a->symbol_number,b->symbol_number)||
            !prime_replay_read_identity(c,"atom.variable",CETTA_CHECKPOINT_VAR,a->var_id,b->var_id)||
            !prime_replay_read_identity(c,"atom.arena",CETTA_CHECKPOINT_ARENA,a->arena_id,b->arena_id))return false;
        if((a->structural_facts&ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID)&&
            domain!=PRIME_REPLAY_ATOM_REGISTRY)
            return prime_replay_compare_refuse(c,"atom.native_handle","CapturedCapability");
        if(a->flags&ATOM_FLAG_HASH_VALID) {
            if(!prime_replay_read_scalar(c,"atom.hash_cache",a->hash_cache,b->hash_cache))return false;
        } else if(!prime_replay_read_physical(c,"atom.inactive_hash_cache",a->hash_cache,b->hash_cache))return false;
        if(a->kind==ATOM_SYMBOL&&
            (!prime_replay_read_symbol_payload(c,a,false)||
             !prime_replay_read_symbol_payload(c,b,true)))return false;
        if(a->kind==ATOM_VAR) {
            uint32_t af=var_epoch_suffix(a->var_id),bf=var_epoch_suffix(b->var_id);
            if((af&&(!ao||!arena_checkpoint_holds_frame(ao,af,&c->read,false)))||
                (bf&&(!bo||!arena_checkpoint_holds_frame(bo,bf,&c->read,true))))
                return prime_replay_compare_refuse(c,"atom.variable_owner","MissingFrameHold");
            if(!prime_replay_read_atom_domain(c,"atom.name_key",a->name_key,b->name_key,domain))return false;
        } else if(a->kind==ATOM_SYMBOL&&
            (atom_symbol_is_spelled(a)||atom_symbol_is_spelled(b))) {
            if(!atom_symbol_is_spelled(a)||!atom_symbol_is_spelled(b)||
                !prime_replay_read_address(c,"atom.spelling",a->spelling,b->spelling)||
                !prime_replay_read_scalar(c,"atom.spelling_hash",a->spelling->hash,b->spelling->hash)||
                !prime_replay_read_bytes(c,"atom.spelling_bytes",a->spelling->bytes,
                    (size_t)a->spelling->len+1u,b->spelling->bytes,
                    (size_t)b->spelling->len+1u))return false;
        } else if(a->name_key||b->name_key)return prime_replay_compare_refuse(c,"atom.name_key","UnexpectedNameKey");
        if(a->kind==ATOM_EXPR) {
            if(!prime_replay_read_scalar(c,"atom.length",a->expr.len,b->expr.len)||
                a->expr.len>SIZE_MAX/sizeof(*a->expr.elems))return false;
            size_t bytes=(size_t)a->expr.len*sizeof(*a->expr.elems);
            if(bytes&&((!ag&&!prime_replay_read_span(c,a->expr.elems,bytes,false))||
                (!bg&&!prime_replay_read_span(c,b->expr.elems,bytes,true))))
                return prime_replay_compare_refuse(c,"atom.children","UnheldChildArray");
            if(!prime_replay_read_address(c,"atom.backing",a->expr.elems,b->expr.elems))return false;
            for(CettaExprIndex i=0u;i<a->expr.len;i++) {
                if(!prime_replay_read_scalar(c,"atom.child_index",i,i)||
                    !prime_replay_read_atom_domain(c,"atom.child",a->expr.elems[i],b->expr.elems[i],domain))return false;
            }
        } else if(a->kind==ATOM_GROUNDED) {
            if(a->structural_facts&ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) {
                if(domain!=PRIME_REPLAY_ATOM_REGISTRY||
                    !atom_checkpoint_native_reference_compare(a,ao,b,bo,&c->read))
                    return prime_replay_compare_refuse(c,"atom.native_reference","ExternalReferenceMismatch");
                continue;
            }
            if(a->ground.gkind==GV_SPACE||b->ground.gkind==GV_SPACE) {
                if(!prime_replay_read_scalar(c,"atom.grounded_kind",a->ground.gkind,b->ground.gkind)||
                    !prime_replay_read_source_space(c,a->ground.ptr,b->ground.ptr))return false;
                continue;
            }
            if(!atom_checkpoint_payload_admitted(a,ao,ag,&c->read,false))
                return prime_replay_compare_refuse(c,"atom.payload.left_owner","UnheldPayload");
            if(!atom_checkpoint_payload_admitted(b,bo,bg,&c->read,true))
                return prime_replay_compare_refuse(c,"atom.payload.right_owner","UnheldPayload");
            if(!atom_checkpoint_leaf_compare(a,b,&c->read)) {
                const char *field="atom.payload.unknown_kind";
                switch(a->ground.gkind) {
#define PAYLOAD_KIND(k) case k:field="atom.payload."#k;break
                PAYLOAD_KIND(GV_INT);PAYLOAD_KIND(GV_FLOAT);PAYLOAD_KIND(GV_BOOL);
                PAYLOAD_KIND(GV_STRING);PAYLOAD_KIND(GV_BIGINT);PAYLOAD_KIND(GV_SPACE);
                PAYLOAD_KIND(GV_STATE);PAYLOAD_KIND(GV_CAPTURE);PAYLOAD_KIND(GV_FOREIGN);
                PAYLOAD_KIND(GV_RATIONAL);PAYLOAD_KIND(GV_PRIME_NEED_CAPABILITY);
                PAYLOAD_KIND(GV_PRIME_CONTEXT);PAYLOAD_KIND(GV_INTERNAL_TAG);
                PAYLOAD_KIND(GV_BINDINGS);PAYLOAD_KIND(GV_TERM_GRAPH);
#undef PAYLOAD_KIND
                default:break;
                }
                return prime_replay_compare_refuse(c,field,"GroundedPayloadMismatch");
            }
        } else if(a->kind!=ATOM_SYMBOL&&a->kind!=ATOM_VAR)
            return prime_replay_compare_refuse(c,"atom.kind","UnknownAtomKind");
    }
    return true;
}

/* No caller-supplied fresh map is accepted. Monotone ranges are read from
 * the actual issuers around the held native requests. Frame generations
 * additionally require live native ownership; rule/local occurrence IDs
 * stay rigid. Whole primitive charges are still compared separately. */
static bool prime_replay_read_frame_authority(PrimeReplayComparison *c,
    PrimeReplayIdentityPair p) {
    if(p.left>UINT32_MAX||p.right>UINT32_MAX)return false;
    uint32_t a=0u,b=0u;
    if(!cetta_frame_identity_slot_count((uint32_t)p.left,&a)||
        !cetta_frame_identity_slot_count((uint32_t)p.right,&b)||a!=b)
        return prime_replay_compare_refuse(c,"identity.frame","FrameAuthority");
    if(p.left!=p.right) {
        bool held=p.frame_owned;
        for(size_t j=0u;j<c->arena_count;j++) {
            if(arena_checkpoint_holds_frame(c->arenas[j].left,(uint32_t)p.left,&c->read,false)&&
                arena_checkpoint_holds_frame(c->arenas[j].right,(uint32_t)p.right,&c->read,true))held=true;
            if(c->refusal)return false;
        }
        if(!held)return prime_replay_compare_refuse(c,"identity.frame","UnheldFrameGeneration");
    }
    return true;
}

static bool prime_replay_read_identity_authority(PrimeReplayComparison *c,size_t prefix) {
    for(size_t i=prefix;i<c->identities.length;i++) {
        PrimeReplayIdentityPair p=c->identities.items[i];
        if(!prime_replay_compare_tick(c,"identity.authority",1u,false))return false;
        if(p.role==CETTA_CHECKPOINT_NEED_NAMED_CELL) {
            /* These names encode the held symbol/world/scope rather than
             * an allocator interval. Their full origin is read separately. */
            if(!(p.left&PRIME_NEED_NAMED_CELL_BIT)||p.left!=p.right)
                return prime_replay_compare_refuse(c,"identity.named_cell","NamedCellWorldMismatch");
            continue;
        }
        if(p.role==CETTA_CHECKPOINT_ARENA) {
            bool found=false;
            for(size_t j=0u;j<c->arena_count;j++) {
                if(!prime_replay_compare_tick(c,"identity.arena_owner",1u,true))return false;
                if(c->arenas[j].left->identity==p.left&&c->arenas[j].right->identity==p.right)found=true;
            }
            if(!found)return prime_replay_compare_refuse(c,"identity.arena","ArenaAuthority");
            continue;
        }
        if(p.role==CETTA_CHECKPOINT_ISSUED_STORE) {
            if(p.left!=c->left->hub.store.identity||p.right!=c->right->hub.store.identity)
                return prime_replay_compare_refuse(c,"identity.store","StoreAuthority");
            continue;
        }
        if(p.role==CETTA_CHECKPOINT_VAR) {
            if(var_base_id(p.left)!=var_base_id(p.right))
                return prime_replay_compare_refuse(c,"identity.variable","VariableSourceIdentity");
            if(!prime_replay_read_identity(c,"identity.variable_frame",CETTA_CHECKPOINT_FRAME,
                var_epoch_suffix(p.left),var_epoch_suffix(p.right)))return false;
            continue;
        }
        if(p.role==CETTA_CHECKPOINT_FRAME) {
            if(!prime_replay_read_frame_authority(c,p))return false;
            continue;
        }
        if(c->target_copy && (p.role==CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE ||
            p.role==CETTA_CHECKPOINT_TARGET_SPACE_PREFIX)) {
            const PrimeReplayTargetOrigin *t=c->target_copy;
            bool instance=p.role==CETTA_CHECKPOINT_TARGET_SPACE_INSTANCE;
            uint64_t source=instance?t->source.instance_id:t->source.prefix_epoch;
            uint64_t image=instance?t->image.instance_id:t->image.prefix_epoch;
            uint64_t before=instance?t->before.instance:t->before.prefix;
            uint64_t after=instance?t->after.instance:t->after.prefix;
            if(p.left==source || p.right==image) {
                if(!t->complete || p.left!=source || p.right!=image || !source ||
                    source>=before || before>image || image>=after || after==UINT64_MAX)
                    return prime_replay_compare_refuse(c,"identity.target_copy","TargetIssuanceWitness");
                continue;
            }
        }
        uint64_t lb=c->left_begin.next[p.role],le=c->left_end.next[p.role];
        uint64_t rb=c->right_begin.next[p.role],re=c->right_end.next[p.role];
        if(lb||le||rb||re) {
            if(!lb||!rb||le<lb||re<rb||p.left==UINT64_MAX||p.right==UINT64_MAX)
                return prime_replay_compare_refuse(c,"identity.issuer","IssuerExhaustionOrWrap");
            if(p.left<lb) {
                if(p.left!=p.right)return prime_replay_compare_refuse(c,"identity.prefix","ChangedOldIdentity");
            } else if(p.left>=le||p.right<rb||p.right>=re)
                return prime_replay_compare_refuse(c,"identity.fresh","UnissuedIdentity");
        } else if(p.left!=p.right)return prime_replay_compare_refuse(c,"identity.rigid","IdentityMismatch");
    }
    return true;
}

static void prime_replay_comparison_free(PrimeReplayComparison *c) {
    if(!c->ready)return;
    atom_deep_copy_session_free(c->globals);
    free(c->addresses);free(c->atoms);free(c->atom_domains);free(c->arenas);free(c->physical);free(c->identities.items);
    arena_free(&c->inspection);*c=(PrimeReplayComparison){0};
}
static bool prime_replay_copy_identity_prefix(PrimeReplayComparison *c,
    const PrimeReplayIdentityRelation *prefix) {
    if(prefix&&prefix->length) {
        memcpy(c->identities.items,prefix->items,prefix->length*sizeof(*prefix->items));
        c->identities.length=prefix->length;
        for(size_t i=0u;i<c->identities.length;i++) {
            if(!prime_replay_compare_tick(c,"identity.prefix_owner_reset",1u,false))return false;
            c->identities.items[i].frame_owned=false;
            c->identities.items[i].frame_left_hint=0u;
            c->identities.items[i].frame_right_hint=0u;
        }
    }
    return true;
}

static bool prime_replay_comparison_init(PrimeReplayComparison *c,size_t capacity,
    uint64_t fuel,size_t bytes,const PrimeReplaySources *sources,
    const PrimeReplayIdentityRelation *prefix) {
    if(c->ready||!capacity||!sources||!prime_replay_sources_current(sources))return false;
    size_t width=sizeof(*c->addresses)+sizeof(*c->atoms)+sizeof(*c->atom_domains)+sizeof(*c->arenas)+
        sizeof(*c->identities.items);
    size_t physical_capacity=capacity<32u?capacity:32u;
    size_t physical_bytes=physical_capacity*sizeof(*c->physical);
    if(capacity>SIZE_MAX/width||capacity*width>bytes||
        physical_bytes>bytes-capacity*width||(prefix&&prefix->length>capacity))return false;
    *c=(PrimeReplayComparison){.capacity=capacity,.physical_capacity=physical_capacity,.fuel=fuel,
        .bytes_limit=bytes-capacity*width-physical_bytes,.sources=sources,.ready=true};
    arena_init_detached(&c->inspection);arena_set_hashcons(&c->inspection,NULL);
    c->addresses=calloc(capacity,sizeof(*c->addresses));c->atoms=calloc(capacity,sizeof(*c->atoms));
    c->atom_domains=calloc(capacity,sizeof(*c->atom_domains));
    c->arenas=calloc(capacity,sizeof(*c->arenas));c->physical=calloc(physical_capacity,sizeof(*c->physical));
    c->identities.items=calloc(capacity,sizeof(*c->identities.items));c->identities.capacity=capacity;
    if(!c->addresses||!c->atoms||!c->atom_domains||!c->arenas||!c->physical||!c->identities.items)
        return prime_replay_compare_refuse(c,"reader.allocation","ComparisonCapacity");
    if(!prime_replay_copy_identity_prefix(c,prefix))return false;
    c->read=(CettaCheckpointCompare){.context=c,.scalar=prime_replay_read_scalar,
        .identity=prime_replay_read_identity,.address=prime_replay_read_address,.target=prime_replay_read_target,
        .arena=prime_replay_read_arena,.frame_scope=prime_replay_read_frame_scope,
        .atom=prime_replay_read_atom,.retained_atom=prime_replay_read_retained_atom,
        .physical=prime_replay_read_physical,.bytes=prime_replay_read_bytes,
        .service=prime_replay_read_service,.service_release=prime_replay_read_service_release};
    size_t global_bytes=0u,global_work=0u,global_granted=0u;
    if(!atom_checkpoint_global_budget(sources->globals,capacity,&global_bytes,&global_work)||
        !prime_replay_global_reserve(c,"reader.global_preparation",global_work,global_bytes,&global_granted))return false;
    c->global_reserved=global_bytes;
    c->globals=atom_deep_copy_session_new(&c->inspection);
    AtomDeepCopyGlobalInspection observed={0};
    /* Table slots and retained graph edges consume the separately reserved
     * service work; they are not entries in the checkpoint relation map. */
    bool ok=c->globals&&atom_deep_copy_session_restrict_globals(c->globals,sources->globals,global_granted,&observed);
    prime_replay_global_settle(c,global_granted,&observed);
    if(!ok)return prime_replay_compare_refuse(c,"reader.global_domain","GlobalOwnerDomain");
    /* Every comparison authenticates the same held source service, not
     * just the initial ambient-origin comparison. Its arena identity,
     * aliases, initialized caches and frame holds use the common reader. */
    return prime_replay_read_arena(c,"reader.source_owner",
        sources->persistent,sources->persistent);
}

static void prime_replay_comparison_account(PrimeReplayComparison *c) {
    if(!c->ready||c->accounted)return;
    cetta_native_cost_charge(CETTA_COST_ANALYSIS_STEP,c->reads);
    cetta_native_cost_charge(CETTA_COST_INDEX_VISIT,c->probes);
    cetta_native_cost_charge(CETTA_COST_CAPTURE,c->services);
    c->accounted=true;
}

static void prime_replay_checkpoint_origin_free(PrimeReplayCheckpoint *checkpoint) {
    if(!checkpoint->ambient_read)return;
    prime_replay_comparison_free(checkpoint->ambient_read);
    free(checkpoint->ambient_read);checkpoint->ambient_read=NULL;
}

static bool prime_replay_compare_transaction_fields(PrimeReplayComparison *,
    const PrimeNativeControl *,const PrimeNativeControl *,const AtomDeepCopySession *);

static bool prime_replay_checkpoint_origin_check(const PrimeNativeControl *source,
    const PrimeReplaySources *sources,size_t nodes,size_t bytes,
    const AtomDeepCopyArenaPair *pairs,size_t pair_count,
    const AtomDeepCopySession *atoms,PrimeReplayCheckpoint *out) {
    CettaPrimeCheckpointCopy *copy=out->preparation;
    if(out->ambient_read||!copy||!copy->prepared||(!copy->ambient_services&&!copy->transaction_boundary)||
        copy->source!=source||!prime_checkpoint_source_live(copy)||
        !copy->image||!copy->image->source_services||copy->image->source_services->active||
        (source->source_services && source->source_services->active) ||
        !prime_checkpoint_service_selection_live(copy)||!prime_replay_sources_current(sources))return false;
    const EquationMatchDecisionCache *selectors=source->source_services
        ? &source->source_services->selectors : equation_match_cache_current();
    SpaceExecutionAnalysisCache *effects=source->source_services
        ? source->source_services->effects : space_execution_analysis_cache_current();
    if(copy->service_selectors!=selectors||!prime_replay_service_domain(source,sources,nodes))return false;
    if(copy->transaction_boundary && (!out->target.complete ||
        cetta_space_agenda_checkpoint_lookup(atoms,source->space_agenda)!=copy->image->space_agenda ||
        !space_read_token_is_current(out->target.source) || !space_read_token_is_current(out->target.image)))return false;
    /* This is a synchronous read of the actual selected or owned stores.
     * The live scope remains active; no facade is installed or copied. */
    cetta_native_cost_charge(CETTA_COST_CAPTURE,1u);
    out->ambient_read=calloc(1u,sizeof(*out->ambient_read));
    if(!out->ambient_read)return false;
    PrimeReplayComparison *read=out->ambient_read;
    size_t budget=bytes/8u;
    size_t width=sizeof(PrimeReplayAddressPair)*2u+sizeof(PrimeReplayArenaPair)+
        sizeof(PrimeReplayPhysicalField)+sizeof(PrimeReplayIdentityPair)+sizeof(unsigned char);
    size_t capacity=budget/(2u*width);
    if(capacity>nodes)capacity=nodes;
    bool ok=bytes<=UINT64_MAX/32u&&prime_replay_comparison_init(read,capacity,
        (uint64_t)bytes*32u,budget,sources,NULL);
    read->left=source;read->right=copy->image;read->transaction=copy->transaction_boundary;
    read->target_copy=read->transaction?&out->target:NULL;
    for(size_t i=0u;ok&&i<pair_count;i++)
        ok=prime_replay_read_arena(read,"capture.copy_arena",pairs[i].source,pairs[i].destination);
    if(ok && read->transaction) {
        const AtomDeepCopySession *borrowed=cetta_space_agenda_checkpoint_owns_target(source->space_agenda)?NULL:atoms;
        ok=prime_replay_compare_transaction_fields(read,source,copy->image,borrowed);
    } else if(ok)ok=prime_replay_compare_control_fields(source,copy->image,&read->read);
    if(ok)ok=prime_replay_compare_service_stores_in(selectors,effects,
        &copy->image->source_services->selectors,copy->image->source_services->effects,&read->read,
        read->transaction&&!copy->keep_target_keys?&read->left_target:NULL,
        read->transaction&&!copy->keep_target_keys?&read->right_target:NULL) &&
        prime_replay_read_atom_graph(read)&&prime_replay_read_identity_authority(read,0u);
    /* The graph is the actual completed copy's forwarding relation. */
    for(size_t i=0u;ok&&i<read->atom_count;i++) {
        PrimeReplayAddressPair pair=read->atoms[i];
        ok=prime_replay_compare_tick(read,"capture.copy_atom",1u,true);
        const Atom *forwarded=ok?atom_deep_copy_session_forwarded(atoms,pair.left):NULL;
        if(ok&&forwarded!=pair.right&&pair.left!=pair.right)
            ok=prime_replay_compare_refuse(read,"capture.copy_atom","DifferentCopyImage");
    }
    if(ok)ok=prime_checkpoint_source_live(copy)&&prime_checkpoint_service_selection_live(copy)&&
        (source->source_services || (equation_match_cache_current()==selectors &&
            space_execution_analysis_cache_current()==effects)) && prime_replay_sources_current(sources);
    read->matched=ok;
    if(!ok&&!read->refusal)prime_replay_compare_refuse(read,"capture.ambient","ProviderFieldRefusal");
    prime_replay_comparison_account(read);
    return ok;
}

static bool prime_replay_comparison_run_origin(PrimeReplayComparison *c,
    const PrimeNativeControl *a,const PrimeNativeControl *b,
    const PrimeReplayCheckpoint *origin,const PrimeNativeControl *captured) {
    /* Only the first capture changes ambient selection into an owned image.
     * The following accepted-entry comparison is always strict, including
     * the captured store. This witness never applies to copy or post states. */
    if(!a||!b||!origin||!origin->ambient_services_captured||!origin->ambient_read||
        !origin->ambient_read->matched||!captured||
        origin->ambient_source_handle!=a->handle_id||a->source_services||
        !captured->source_services||!b->source_services||
        !c->ready||c->refusal||!prime_replay_sources_current(c->sources))return false;
    c->left=a;c->right=b;size_t prefix=c->identities.length;
    bool ok=prime_replay_compare_control_fields(a,b,&c->read)&&prime_replay_read_atom_graph(c)&&
        prime_replay_read_identity_authority(c,prefix)&&prime_replay_sources_current(c->sources);
    c->matched=ok;
    if(!ok&&!c->refusal)prime_replay_compare_refuse(c,"capture.origin","ProviderFieldRefusal");
    return ok;
}

static bool prime_replay_compare_transaction_fields(PrimeReplayComparison *c,
    const PrimeNativeControl *a,const PrimeNativeControl *b,const AtomDeepCopySession *origin) {
    if(!prime_replay_targets(c,a,b) ||
        !prime_replay_read_service(c,"transaction.provider_read",c->capacity,0u))return false;
    SpaceCheckpointBudget budget={.work_limit=c->capacity,.byte_limit=0u};
    return prime_replay_compare_control_fields_in(a,b,&c->read,&budget,origin) ||
        prime_replay_compare_refuse(c,"transaction.provider",budget.refusal?budget.refusal:"ProviderFieldRefusal");
}

static bool prime_replay_comparison_run(PrimeReplayComparison *c,
    const PrimeNativeControl *a,const PrimeNativeControl *b) {
    if(!c->ready||c->refusal||!a||!b||!prime_replay_sources_current(c->sources))return false;
    c->left=a;c->right=b;size_t prefix=c->identities.length;
    bool ok=(c->transaction ?
        (prime_replay_compare_transaction_fields(c,a,b,NULL) &&
         prime_replay_compare_services_in(a->source_services,b->source_services,&c->read,
            &c->left_target,&c->right_target))
        : prime_replay_compare_control(a,b,&c->read)) && prime_replay_read_atom_graph(c)&&
        prime_replay_read_identity_authority(c,prefix)&&prime_replay_sources_current(c->sources);
    c->matched=ok;
    if(!ok&&!c->refusal)prime_replay_compare_refuse(c,"provider","ProviderFieldRefusal");
    return ok;
}

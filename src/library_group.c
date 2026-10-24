#include "library_group.h"
#include "eval.h"
#include "petta_semantics.h"
#include "name_key.h"
#include "shared_transition.h"
#include "generated/collection_keys.generated.h"
#include <math.h>
#include <stdlib.h>

typedef struct { Atom *key; int64_t integer; double floating; bool real; } Group;
static bool promoted_numeric_key(const Atom *value,void *context) {
    (void)context;
    /* Authored == may promote numbers recursively, whereas unique-atom
     * distinguishes scalar representations. Such a key can contribute to
     * more than one authored group: retain that route rather than merge it. */
    return value->kind==ATOM_GROUNDED && (value->ground.gkind==GV_FLOAT ||
        value->ground.gkind==GV_BIGINT || value->ground.gkind==GV_RATIONAL);
}
static size_t key_slot(Atom *key,size_t mask) {
    uint64_t h=atom_hash(key);
    if(key->kind==ATOM_GROUNDED && key->ground.gkind==GV_INT) h^=(uint64_t)key->ground.ival;
    h^=h>>30; h*=UINT64_C(0xbf58476d1ce4e5b9); h^=h>>27; h*=UINT64_C(0x94d049bb133111eb); h^=h>>31;
    return (size_t)h&mask;
}
bool cetta_group_sum_call(Arena *a,Atom *head,Atom **args,uint32_t nargs,CettaCallOutcome *out) {
    bool blocked=atom_is_symbol_id(head,g_builtin_syms.lib_list_group_sums_blocks);
    if(!blocked && !atom_is_symbol_id(head,g_builtin_syms.lib_list_group_sums)) return false;
    if(eval_native_interrupted()) { *out=cetta_call_interrupted(); return true; }
    Atom *const *rows; CettaExprLen length;
    Group *groups=NULL; size_t *slots=NULL,used=0; Atom **flat=NULL;
    const char *reference=getenv("CETTA_GROUP_SUM_REFERENCE");
    if(reference && *reference && strcmp(reference,"0")) goto fallback;
    if(nargs!=1 || !atom_sequence_view(args[0],&rows,&length)) goto fallback;
    if(blocked) {
        Atom *const *blocks=rows; CettaExprLen blocks_length=length; size_t total=0;
        for(size_t i=0;i<blocks_length;i++) {
            Atom *const *items; CettaExprLen n;
            if(!atom_sequence_view(blocks[i],&items,&n) || n>SIZE_MAX-total) goto fallback;
            total+=(size_t)n;
        }
        if(total>SIZE_MAX/sizeof(Atom *)) goto fallback;
        flat=total ? malloc(total*sizeof(*flat)) : NULL;
        if(total && !flat) goto fallback;
        size_t next=0;
        for(size_t i=0;i<blocks_length;i++) {
            Atom *const *items=NULL; CettaExprLen n=0; (void)atom_sequence_view(blocks[i],&items,&n);
            if(n) memcpy(flat+next,items,(size_t)n*sizeof(*flat));
            next+=(size_t)n;
        }
        rows=flat; length=total;
    }
    if(length>SIZE_MAX/sizeof(Group) || length>SIZE_MAX/2) goto fallback;
    for(size_t i=0;i<length;i++) {
        if((i&1023u)==0u && eval_native_interrupted()) {
            free(flat); *out=cetta_call_interrupted(); return true;
        }
        Atom *const *pair; CettaExprLen n;
        if(!atom_sequence_view(rows[i],&pair,&n) || n!=2 || !pair[0] ||
            !(pair[0]->flags&ATOM_FLAG_TERM_STABLE) || atom_has_vars(pair[0]) || atom_structural_may_have_nan(pair[0]) ||
            atom_tree_any(pair[0],promoted_numeric_key,NULL) ||
            !pair[1] || pair[1]->kind!=ATOM_GROUNDED ||
            (pair[1]->ground.gkind!=GV_INT && pair[1]->ground.gkind!=GV_FLOAT) ||
            (pair[1]->ground.gkind==GV_FLOAT && !isfinite(pair[1]->ground.fval))) goto fallback;
    }
    size_t capacity=1;
    while(capacity<(size_t)length*2) {
        if(capacity>SIZE_MAX/2) goto fallback;
        capacity*=2;
    }
    if(capacity>SIZE_MAX/sizeof(size_t)) goto fallback;
    slots=calloc(capacity,sizeof(*slots)); groups=length ? calloc((size_t)length,sizeof(*groups)) : NULL;
    if(!slots || (length && !groups)) goto fallback;
    for(size_t i=0;i<length;i++) {
        if((i&1023u)==0u && eval_native_interrupted()) { free(slots); free(groups); free(flat); *out=cetta_call_interrupted(); return true; }
        Atom *const *pair=NULL; CettaExprLen n=0; (void)atom_sequence_view(rows[i],&pair,&n);
        size_t slot=key_slot(pair[0],capacity-1);
        while(slots[slot] && !atom_eq(groups[slots[slot]-1].key,pair[0])) slot=(slot+1)&(capacity-1);
        if(!slots[slot]) { groups[used].key=pair[0]; slots[slot]=++used; }
        Group *g=&groups[slots[slot]-1]; Atom *value=pair[1];
        if(value->ground.gkind==GV_FLOAT) {
            if(!g->real) { g->floating=(double)g->integer; g->real=true; }
            g->floating+=value->ground.fval;
        } else if(g->real) g->floating+=(double)value->ground.ival;
        else if(__builtin_add_overflow(g->integer,value->ground.ival,&g->integer)) goto fallback;
        if(g->real && !isfinite(g->floating)) goto fallback;
    }
    Atom **result=used ? arena_alloc(a,used*sizeof(*result)) : NULL;
    for(size_t i=0;i<used;i++) result[i]=atom_expr2(a,atom_deep_copy(a,groups[i].key),
        groups[i].real ? atom_float(a,groups[i].floating) : atom_int(a,groups[i].integer));
    free(slots); free(groups); free(flat);
    *out=cetta_call_completed_value(atom_expr2(a,atom_symbol(a,"List:GroupedSums"),atom_expr(a,result,used)));
    return true;
fallback:
    free(slots); free(groups); free(flat);
    *out=cetta_call_completed_value(atom_symbol(a,"List:GroupSumsFallback")); return true;
}

bool cetta_key_factorize_call(Space *space, Arena *arena, Atom *head, Atom **args,
    uint32_t nargs, CettaCallOutcome *out) {
    if (!atom_is_symbol(head, CETTA_COLLECTION_KEYS_HEAD)) return false;
    CETTA_SCOPED_SHARED_TRANSITION(key_encoding_read);
    NameKeyTable *table = NULL;
    NameId *codes = NULL;
    size_t *firsts = NULL;
    Atom *const *items = NULL;
    CettaExprLen length = 0;
    const char *reference = getenv("CETTA_KEY_ENCODING_REFERENCE");
    bool interrupted = eval_native_interrupted();
    if (interrupted || !space || nargs != 1 ||
        (reference && *reference && strcmp(reference, "0")) ||
        strcmp(CETTA_COLLECTION_KEYS_POLICY,
          "FirstMatchingRepresentative|ZeroBasedCodes|SourceOrder|DuplicateOccurrences|MatchingCompatibleKeys|CompleteValidation|AuthoredFallback") ||
        space_equations_may_match_known_head(space, g_builtin_syms.op_eq) ||
        space_directional_rules_may_match_known_head(space, g_builtin_syms.op_eq) ||
        !atom_sequence_view(args[0], &items, &length) || length >= UINT32_MAX ||
        length > SIZE_MAX / sizeof(*codes) || length > SIZE_MAX / sizeof(*firsts))
        goto declined;
    /* Hashing and authored equality must agree for every key, not merely
     * for the first representative. The numeric fallback is intentional. */
    for (size_t index = 0; index < length; index++) {
        if ((index & 1023u) == 0u && (interrupted = eval_native_interrupted())) goto declined;
        if (!name_key_is_admissible(items[index]) || atom_structural_may_have_nan(items[index]) ||
            atom_tree_any(items[index], promoted_numeric_key, NULL)) goto declined;
    }
    table = name_key_table_new(64u);
    codes = length ? malloc((size_t)length * sizeof(*codes)) : NULL;
    firsts = length ? malloc((size_t)length * sizeof(*firsts)) : NULL;
    if (!table || (length && (!codes || !firsts))) goto declined;
    for (size_t index = 0; index < length; index++) {
        if ((index & 1023u) == 0u && (interrupted = eval_native_interrupted())) goto declined;
        uint32_t previous = name_key_count(table);
        NameId id = name_key_intern(table, items[index]);
        if (id == NAME_ID_NONE) goto declined;
        codes[index] = id;
        if (id > previous) firsts[id - 1u] = index;
    }
    size_t count = name_key_count(table);
    Atom **key_values = count ? arena_alloc(arena, count * sizeof(*key_values)) : NULL;
    Atom **first_values = count ? arena_alloc(arena, count * sizeof(*first_values)) : NULL;
    Atom **code_values = length ? arena_alloc(arena, (size_t)length * sizeof(*code_values)) : NULL;
    for (size_t index = 0; index < count; index++) {
        key_values[index] = atom_deep_copy(arena, (Atom *)name_key_lookup(table, (NameId)(index + 1u)));
        first_values[index] = atom_int(arena, (int64_t)firsts[index]);
    }
    for (size_t index = 0; index < length; index++)
        code_values[index] = atom_int(arena, (int64_t)codes[index] - 1);
    Atom *fields[CETTA_COLLECTION_KEYS_RESULT_WIDTH] = {0};
    fields[0] = atom_symbol(arena, CETTA_COLLECTION_KEYS_RESULT);
    fields[CETTA_COLLECTION_KEYS_KEYS] = atom_expr(arena, key_values, count);
    fields[CETTA_COLLECTION_KEYS_CODES] = atom_expr(arena, code_values, length);
    fields[CETTA_COLLECTION_KEYS_FIRSTPOSITIONS] = atom_expr(arena, first_values, count);
    *out = cetta_call_completed_value(atom_expr(arena, fields, CETTA_COLLECTION_KEYS_RESULT_WIDTH));
    free(codes); free(firsts); name_key_table_delete(table);
    return true;
declined:
    free(codes); free(firsts); name_key_table_delete(table);
    *out = interrupted ? cetta_call_interrupted() :
        cetta_call_completed_value(atom_symbol(arena, "List:FactorizeFallback"));
    return true;
}

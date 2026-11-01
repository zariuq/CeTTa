#define _POSIX_C_SOURCE 200809L
#include "ownership.h"
#include "sema.h"
#include "pp.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

CeText ce_builtin_CPPGNUGrammar(void);

typedef struct { size_t root; unsigned long long first, count; } Range;
typedef struct { CeType *type; CeMember *data, *length; bool write; unsigned family; } View;
typedef struct { size_t id, parent; Range range; bool write, live; unsigned family; size_t scope; } Loan;
typedef struct {
    Range range; size_t via; bool exact, null, nullable;
    unsigned family; size_t owner;
    unsigned authority;
} Reference;
typedef struct {
    bool declared, initialized, moved, parameter, immortal;
    size_t slot, scope;
    Reference reference;
    Reference output; bool output_initialized;
} Variable;
typedef struct { CeText name; CeType *type; bool write, is_static, nullable; unsigned family; } FieldSpec;
typedef struct { CeType *type; FieldSpec *fields; size_t len; } RecordSpec;
typedef struct { CeType *type; unsigned family; } Capability;
typedef struct StoredField {
    CeEntity entity;
    Range storage;
    FieldSpec *spec;
    Variable value;
    bool active;
    struct StoredField *next;
} StoredField;
typedef struct FieldIdentity {
    Range storage;
    CeType *type;
    size_t id;
    struct FieldIdentity *next;
} FieldIdentity;
typedef struct {
    size_t parameter;
    FieldSpec *field;
    unsigned long long offset;
    size_t input;
    bool writing, owning;
} FieldPart;
typedef struct { size_t parameter; FieldSpec *field; int source; size_t part; Reference allowed; } FieldPost;
typedef struct { size_t parameter; FieldSpec *field; } FieldTake;
typedef struct {
    CeText name; const CeAtom *spec;
    CeEntity *entity; const CeTree *body;
    CeEntity **parameters; size_t nparameters;
    bool checked, foreign, variadic_read;
    int result_from; const CeAtom *result_path;
    bool result_static, result_nullable; unsigned result_family;
    int *modes, *stores; unsigned *families;
    bool *nullable;
    CeText obligation;
    int allocation_count; unsigned release_family;
    Reference allowed_result;
    size_t *input_roots, *global_roots;
    bool *global_effects;
    FieldPart *fields; size_t nfields;
    FieldPost *posts; size_t nposts;
    FieldTake *takes; size_t ntakes;
    Reference *originals;
    int result_input;
} Function;
typedef struct {
    Variable *variables;
    StoredField *fields;
    Loan *loans; size_t nloans, capacity;
    size_t next, scope;
    bool reachable;
} State;
typedef struct {
    CeArena *arena; CeSema *sema; const CePPResult *pp;
    const CeAtom *declaration; const char *identity; CeError *error;
    Function *functions; size_t nfunctions;
    Function *function; State state;
    CeText *global_owners;
    CeText *families; size_t nfamilies;
    View *views; size_t nviews;
    RecordSpec *records; size_t nrecords;
    Capability *capabilities; size_t ncapabilities;
    FieldIdentity *field_ids; size_t next_field;
    FILE *plan; size_t loops, loop_scope;
    State *loop_entry, *switch_entry; size_t switches, switch_scope;
} Ownership;
typedef struct { Range range; size_t via; bool exact; CeEntity *variable; int output; } Place;

static bool head(const CeAtom *a, const char *word) {
    return a && a->kind == CE_ATOM_LIST && a->len &&
        a->items[0]->kind == CE_ATOM_SYMBOL && ce_is(a->items[0]->text,word);
}
static bool same(CeText a, CeText b) { return a.len == b.len && !memcmp(a.data,b.data,a.len); }
static bool symbol(const CeAtom *a, const char *word) { return a && a->kind == CE_ATOM_SYMBOL && ce_is(a->text,word); }
static unsigned kind(Ownership *o, const CeTree *t) { return t ? ce_sema_kind(o->sema,t) : CE_K_NONE; }
static const CeExpr *typed(Ownership *o, const CeTree *t) { return ce_sema_expr(o->sema,t); }
static CeEntity *entity(Ownership *o, const CeTree *t) {
    const CeUse *use = t ? ce_sema_use(o->sema,t) : NULL;
    return use ? use->entity : NULL;
}
static bool fail(Ownership *o, const CeTree *site, const char *format, ...) {
    char message[640]; va_list args;
    va_start(args,format); vsnprintf(message,sizeof(message),format,args); va_end(args);
    const CePPOrigin *origin = site ? ce_pp_origin(o->pp,site->offset) : NULL;
    return ce_error(o->error,o->declaration->offset,"ownership%s%.*s%s%s%s%u: %s",
        o->function ? " in " : "",o->function ? (int)o->function->name.len : 0,
        o->function ? o->function->name.data : (const unsigned char *)"",
        origin ? " at " : "",origin ? origin->file : "",origin ? ":" : "",
        origin ? origin->line : 0,message);
}
static bool malformed(Ownership *o, const CeAtom *site, const char *message) {
    return ce_error(o->error,site->offset,"ownership: %s",message);
}
static bool pointer(const CeType *type) { return type && type->kind == CE_T_POINTER; }
static bool read_only(const CeType *type) { return pointer(type) && (type->base->qualifiers & CE_Q_CONST); }
static View *view(Ownership *o, const CeType *type) {
    if (!ce_type_record(type)) return NULL;
    for (size_t i = 0; i < o->nviews; ++i) if (o->views[i].type->tag == type->tag) return &o->views[i];
    return NULL;
}
static bool reference_type(Ownership *o, const CeType *type) { return pointer(type) || view(o,type); }
static bool reference_read_only(Ownership *o, const CeType *type) {
    View *v = view(o,type); return v ? !v->write : read_only(type);
}
static Capability *capability(Ownership *o, const CeType *type) {
    if (!ce_type_record(type)) return NULL;
    for (size_t i = 0; i < o->ncapabilities; ++i)
        if (o->capabilities[i].type->tag == type->tag) return &o->capabilities[i];
    return NULL;
}
static Capability *capability_family(Ownership *o, unsigned brand) {
    for (size_t i = 0; i < o->ncapabilities; ++i)
        if (o->capabilities[i].family == brand) return &o->capabilities[i];
    return NULL;
}
static void reference_authority(Ownership *o, Reference *r, const CeType *type) {
    Capability *typed_cap = pointer(type) ? capability(o,type->base) : NULL;
    Capability *owned_cap = capability_family(o,r->family);
    if (typed_cap) r->authority = typed_cap->family;
    else if (owned_cap) r->authority = owned_cap->family;
}
static bool contains_capability(Ownership *o, const CeType *type, unsigned depth) {
    if (!type || depth > 64) return true;
    if (pointer(type)) return false;
    if (capability(o,type)) return true;
    if (type->kind == CE_T_ARRAY) return contains_capability(o,type->base,depth+1);
    if (ce_type_record(type) && type->tag && type->tag->record)
        for (size_t i = 0; i < type->tag->record->nmembers; ++i)
            if (contains_capability(o,type->tag->record->members[i].type,depth+1)) return true;
    return false;
}
static bool object_read_only(const CeType *type) {
    return type->kind == CE_T_ARRAY ? object_read_only(type->base) : (type->qualifiers & CE_Q_CONST) != 0;
}
static RecordSpec *record_spec(Ownership *o, const CeType *type) {
    if (!ce_type_record(type)) return NULL;
    for (size_t i = 0; i < o->nrecords; ++i) if (o->records[i].type->tag == type->tag) return &o->records[i];
    return NULL;
}
static FieldSpec *field_spec(Ownership *o, const CeType *type, CeText name) {
    RecordSpec *r = record_spec(o,type);
    if (r) for (size_t i = 0; i < r->len; ++i) if (same(r->fields[i].name,name)) return &r->fields[i];
    return NULL;
}
static StoredField *stored_id(Ownership *o, size_t id) {
    for (StoredField *f = o->state.fields; f; f = f->next) if (f->entity.id == id && f->active) return f;
    return NULL;
}
static Variable *binding_id(Ownership *o, size_t id) {
    if (!id) return NULL;
    if (id <= o->sema->nentities) return &o->state.variables[id-1];
    StoredField *field = stored_id(o,id);
    return field ? &field->value : NULL;
}
static bool reference_record(const CeType *type, unsigned depth) {
    if (!type || depth > 64) return true;
    if (pointer(type)) return true;
    if (type->kind == CE_T_ARRAY) return reference_record(type->base,depth+1);
    if (ce_type_record(type) && type->tag && type->tag->record)
        for (size_t i = 0; i < type->tag->record->nmembers; ++i)
            if (reference_record(type->tag->record->members[i].type,depth+1)) return true;
    return false;
}
static unsigned family(Ownership *o, CeText name) {
    for (size_t i = 0; i < o->nfamilies; ++i) if (same(name,o->families[i])) return (unsigned)i+1;
    CeText *values = ce_alloc(o->arena,(o->nfamilies+1)*sizeof(*values));
    if (o->nfamilies) memcpy(values,o->families,o->nfamilies*sizeof(*values));
    values[o->nfamilies++] = name; o->families = values; return (unsigned)o->nfamilies;
}
static Function *function_named(Ownership *o, CeText name) {
    for (size_t i = 0; i < o->nfunctions; ++i) if (same(o->functions[i].name,name)) return &o->functions[i];
    return NULL;
}
static bool natural(const CeAtom *a, size_t *out) {
    if (!a || a->kind != CE_ATOM_SYMBOL || !a->text.len) return false;
    size_t value = 0;
    for (size_t i = 0; i < a->text.len; ++i) {
        unsigned c = a->text.data[i];
        if (c < '0' || c > '9' || value > (SIZE_MAX-(c-'0'))/10) return false;
        value = value*10+(c-'0');
    }
    *out = value; return true;
}
static bool apart(Range a, Range b) {
    return a.root != b.root || a.first+a.count <= b.first || b.first+b.count <= a.first;
}
static bool within(Range a, Range b) {
    return a.root == b.root && a.first <= b.first && b.first+b.count <= a.first+a.count;
}
static Loan *loan(Ownership *o, size_t id) {
    for (size_t i = 0; i < o->state.nloans; ++i)
        if (o->state.loans[i].id == id && o->state.loans[i].live) return &o->state.loans[i];
    return NULL;
}
static bool reference_storage(Ownership *o, Range range) {
    for (size_t i = 0; i < o->sema->nentities; ++i) {
        Variable *v = &o->state.variables[i];
        CeType *type = o->sema->entities[i]->type;
        if (!v->declared || (!pointer(type) && !view(o,type))) continue;
        Loan *storage = loan(o,v->slot);
        if (storage && !apart(storage->range,range)) return true;
    }
    for (StoredField *f = o->state.fields; f; f = f->next)
        if (f->active && !apart(f->storage,range)) return true;
    return false;
}
static bool ancestor(Ownership *o, size_t parent, const Loan *child) {
    size_t next = child->parent;
    while (next != SIZE_MAX) {
        if (next == parent) return true;
        Loan *l = loan(o,next); if (!l) return false; next = l->parent;
    }
    return false;
}
static void range_plan(FILE *file, Range r) { fprintf(file,"%zu %llu %llu",r.root,r.first,r.count); }
static bool access(Ownership *o, const CeTree *site, size_t via, Range range, bool writing) {
    Loan *l = loan(o,via);
    if (!l || !within(l->range,range)) return fail(o,site,"access has no live loan covering this allocation range");
    if (writing && !l->write) return fail(o,site,"write through a shared loan");
    for (size_t i = 0; i < o->state.nloans; ++i) {
        Loan *other = &o->state.loans[i];
        if (!other->live || other->id == via || apart(range,other->range) || ancestor(o,other->id,l)) continue;
        if (writing || other->write) return fail(o,site,"overlapping loan prevents %s access",writing ? "exclusive" : "shared");
    }
    if (o->plan) {
        fprintf(o->plan," (access %zu ",via); range_plan(o->plan,range);
        fprintf(o->plan," %s)\n",writing ? "write" : "read");
    }
    return true;
}
static bool drop_fields(Ownership *, const CeTree *, size_t);
static void append_loan(Ownership *o, Loan value) {
    if (o->state.nloans == o->state.capacity) {
        size_t capacity = o->state.capacity ? o->state.capacity*2 : 32;
        Loan *values = ce_alloc(o->arena,capacity*sizeof(*values));
        if (o->state.nloans) memcpy(values,o->state.loans,o->state.nloans*sizeof(*values));
        o->state.loans = values; o->state.capacity = capacity;
    }
    o->state.loans[o->state.nloans++] = value;
}
static size_t root(Ownership *o, unsigned long long count, bool write, const char *origin, unsigned brand) {
    size_t id = o->state.next++;
    append_loan(o,(Loan){id,SIZE_MAX,{id,0,count},write,true,brand,o->state.scope});
    if (o->plan) fprintf(o->plan," (root %zu %llu %s %s %u)\n",id,count,write ? "write" : "read",origin,brand);
    return id;
}
static bool borrow(Ownership *o, const CeTree *site, Reference source, bool write, size_t *id) {
    if (source.null) { *id = SIZE_MAX; return true; }
    if (!access(o,site,source.via,source.range,write)) return false;
    /* Copying a shared reference preserves the referent's lifetime, rather
     * than making the new copy depend on the storage of an earlier copy. */
    if (!write) {
        Loan *parent = loan(o,source.via);
        while (parent && !parent->write && parent->parent != SIZE_MAX) {
            source.via = parent->parent;
            parent = loan(o,source.via);
        }
    }
    *id = o->state.next++;
    append_loan(o,(Loan){*id,source.via,source.range,write,true,0,o->state.scope});
    if (o->plan) {
        fprintf(o->plan," (borrow %zu %zu ",*id,source.via); range_plan(o->plan,source.range);
        fprintf(o->plan," %s)\n",write ? "write" : "read");
    }
    return true;
}
static bool end(Ownership *o, const CeTree *site, size_t id) {
    if (id == 0) return fail(o,site,"the immutable static lifetime cannot be ended");
    Loan *l = loan(o,id);
    if (!l) return fail(o,site,"ending an absent or already ended loan");
    if ((l->id == l->range.root && l->parent == SIZE_MAX) || l->family) {
        size_t root_id = l->range.root;
        if (!drop_fields(o,site,root_id)) return false;
        l = loan(o,id);
    }
    for (size_t i = 0; i < o->state.nloans; ++i)
        if (o->state.loans[i].live && ancestor(o,id,&o->state.loans[i]))
            return fail(o,site,"loan outlives the reference or allocation it borrows");
    l->live = false;
    if (o->plan) fprintf(o->plan," (end %zu)\n",id);
    return true;
}
static State clone(Ownership *o) {
    State result = o->state;
    result.variables = ce_alloc(o->arena,o->sema->nentities*sizeof(*result.variables));
    memcpy(result.variables,o->state.variables,o->sema->nentities*sizeof(*result.variables));
    result.loans = ce_alloc(o->arena,result.capacity*sizeof(*result.loans));
    if (result.nloans) memcpy(result.loans,o->state.loans,result.nloans*sizeof(*result.loans));
    result.fields = NULL;
    StoredField **tail = &result.fields;
    for (StoredField *f = o->state.fields; f; f = f->next) {
        *tail = ce_alloc(o->arena,sizeof(**tail)); **tail = *f;
        tail = &(*tail)->next;
    }
    *tail = NULL;
    return result;
}
static bool same_reference(Reference a, Reference b) {
    return a.via == b.via && a.null == b.null && a.exact == b.exact &&
        a.family == b.family && a.authority == b.authority && a.range.root == b.range.root &&
        a.range.first == b.range.first && a.range.count == b.range.count;
}
static bool join(Ownership *o, const CeTree *site, State a, State b) {
    size_t next = a.next > b.next ? a.next : b.next;
    if (!a.reachable || !b.reachable) { o->state = a.reachable ? a : b; o->state.next = next; return true; }
    for (size_t i = 0; i < o->sema->nentities; ++i) {
        Variable *x = &a.variables[i], *y = &b.variables[i];
        if (x->declared != y->declared || (x->declared &&
            (x->initialized != y->initialized || x->moved != y->moved ||
             !same_reference(x->reference,y->reference))))
            return fail(o,site,"branches or loop backedges disagree on ownership or borrow origin");
        if (x->declared) x->reference.nullable |= y->reference.nullable;
        if (x->declared) {
            x->output_initialized = x->output_initialized && y->output_initialized;
            if (x->output_initialized && !same_reference(x->output,y->output))
                return fail(o,site,"branches disagree on the stored reference's lifetime origin");
        }
    }
    for (StoredField *x = a.fields; x; x = x->next) if (x->active) {
        StoredField *y = b.fields;
        while (y && (!y->active || y->entity.id != x->entity.id)) y = y->next;
        if (!y || x->value.initialized != y->value.initialized || x->value.moved != y->value.moved ||
            !same_reference(x->value.reference,y->value.reference))
            return fail(o,site,"branches or loop backedges disagree on a stored field's lifetime");
        x->value.reference.nullable |= y->value.reference.nullable;
        x->value.output_initialized = x->value.output_initialized && y->value.output_initialized;
    }
    for (StoredField *y = b.fields; y; y = y->next) if (y->active) {
        StoredField *x = a.fields;
        while (x && (!x->active || x->entity.id != y->entity.id)) x = x->next;
        if (!x) return fail(o,site,"branches disagree on initialized reference fields");
    }
    for (size_t i = 0; i < a.nloans; ++i) if (a.loans[i].live) {
        bool found = false;
        for (size_t j = 0; j < b.nloans; ++j) if (b.loans[j].live &&
            a.loans[i].id == b.loans[j].id) { found = true; break; }
        if (!found) return fail(o,site,"branches or loop backedges disagree on live loans");
    }
    for (size_t i = 0; i < b.nloans; ++i) if (b.loans[i].live) {
        bool found = false;
        for (size_t j = 0; j < a.nloans; ++j) if (a.loans[j].live &&
            b.loans[i].id == a.loans[j].id) { found = true; break; }
        if (!found) return fail(o,site,"branches or loop backedges disagree on live loans");
    }
    o->state = a; o->state.next = next; return true;
}

static bool expression(Ownership *, const CeTree *, Reference *);
static bool place(Ownership *, const CeTree *, Place *);
static bool statement(Ownership *, const CeTree *);
static bool cleanup(Ownership *, const CeTree *, size_t);
static bool field(Ownership *, const CeTree *, Place, CeType *, CeText, Place *);
static bool checked_reference(Ownership *, const CeTree *, Reference, bool);
static bool view_initializer(Ownership *, const CeTree *, View *, Reference *);
static bool drop_fields(Ownership *, const CeTree *, size_t);
static bool record_initializer(Ownership *, const CeTree *, CeType *, Place);
static bool record_parts(Ownership *, Function *, size_t, CeType *, unsigned long long, bool);

static bool view_component(Ownership *o, const CeTree *t) {
    unsigned k = kind(o,t);
    if (k != CE_K_Member && k != CE_K_PointerMember) return false;
    const CeExpr *base = typed(o,t->children[0]);
    return base && view(o,k == CE_K_PointerMember && pointer(base->type) ? base->type->base : base->type);
}

static Reference static_reference(bool nullable) {
    return (Reference){{0,0,ULLONG_MAX},0,false,false,nullable,0,0,0};
}
static size_t storage_scope_depth(Ownership *o, size_t root_id, size_t depth) {
    if (depth > o->next_field) return o->state.scope;
    for (StoredField *f = o->state.fields; f; f = f->next)
        if (f->active && f->value.initialized && !f->value.moved && f->value.reference.family &&
            f->value.reference.range.root == root_id)
            return storage_scope_depth(o,f->storage.root,depth+1);
    for (size_t i = 0; i < o->state.nloans; ++i) {
        Loan *l = &o->state.loans[i];
        if (l->live && l->range.root == root_id && l->parent == SIZE_MAX) return l->scope;
    }
    return 0;
}
static size_t storage_scope(Ownership *o, size_t root_id) { return storage_scope_depth(o,root_id,0); }
static bool owns_storage(Ownership *o, size_t owner, size_t target, size_t depth) {
    if (owner == target) return true;
    if (depth > o->next_field) return false;
    for (StoredField *f = o->state.fields; f; f = f->next)
        if (f->active && f->storage.root == owner && f->value.initialized &&
            !f->value.moved && f->value.reference.family &&
            owns_storage(o,f->value.reference.range.root,target,depth+1)) return true;
    return false;
}
static StoredField *field_cell(Ownership *o, const CeTree *site, FieldSpec *spec, Range storage) {
    for (StoredField *f = o->state.fields; f; f = f->next)
        if (f->active && f->storage.root == storage.root && f->storage.first == storage.first &&
            f->storage.count == storage.count) return f;
    FieldIdentity *key = o->field_ids;
    while (key && !(key->storage.root == storage.root && key->storage.first == storage.first &&
        key->storage.count == storage.count && ce_type_similar(key->type,spec->type))) key = key->next;
    if (!key) {
        key = ce_alloc(o->arena,sizeof(*key));
        *key = (FieldIdentity){storage,spec->type,o->sema->nentities + ++o->next_field,o->field_ids};
        o->field_ids = key;
    }
    StoredField *f = ce_alloc(o->arena,sizeof(*f));
    *f = (StoredField){.entity={.kind=CE_ENTITY_OBJECT,.scope=CE_SCOPE_BLOCK,.id=key->id,
        .name=spec->name,.type=spec->type,.declaration=site},.storage=storage,.spec=spec,
        .value={.declared=true,.scope=storage_scope(o,storage.root),
            .reference={.null=true,.via=SIZE_MAX}},.active=true,.next=o->state.fields};
    o->state.fields = f;
    return f;
}
static bool drop_fields(Ownership *o, const CeTree *site, size_t root_id) {
    for (StoredField *f = o->state.fields; f; f = f->next) if (f->active && f->storage.root == root_id) {
        Variable *v = &f->value;
        if (v->initialized && !v->moved && v->reference.family)
            return fail(o,site,"record lifetime ends with an unreleased owning field");
        if (v->initialized && !v->moved && v->reference.via != SIZE_MAX && v->reference.via != 0 &&
            !end(o,site,v->reference.via)) return false;
        f->active = false;
    }
    return true;
}
static bool pointer_binding(Ownership *o, const CeTree *site, CeEntity *e, Reference source) {
    Variable *v = binding_id(o,e->id);
    if (!v) return fail(o,site,"reference binding has no live storage identity");
    Capability *target_cap = pointer(e->type) ? capability(o,e->type->base) : NULL;
    if (!source.null && source.authority && (!target_cap || source.authority != target_cap->family))
        return fail(o,site,"opaque capability cannot lose its typed provenance in a pointer binding");
    if (!source.null && pointer(e->type) && e->type->base->kind == CE_T_VOID &&
        reference_storage(o,source.range))
        return fail(o,site,"reference-bearing storage cannot lose its typed lifetime interface");
    StoredField *stored = stored_id(o,e->id);
    bool write = !reference_read_only(o,e->type);
    if (stored) {
        FieldSpec *spec = stored->spec;
        v->scope = storage_scope(o,stored->storage.root);
        write = spec->write;
        FieldPost *post = NULL;
        for (size_t i = 0; i < o->function->nposts; ++i) {
            FieldPost *candidate = &o->function->posts[i];
            Reference original = o->function->originals[candidate->parameter];
            FieldPart *part = &o->function->fields[candidate->part];
            if (candidate->field == spec && stored->storage.root == original.range.root &&
                stored->storage.first == original.range.first + part->offset) post = candidate;
        }
        if (v->parameter && !spec->is_static && !post)
            return fail(o,site,"updating a borrowed record field requires a relational field postcondition");
        if (post) {
            if (!source.null && !within(post->allowed.range,source.range))
                return fail(o,site,"stored field escapes its declared input lifetime");
            v->output_initialized = true;
        }
        if ((source.null || source.nullable) && !spec->nullable) return fail(o,site,"nullable value stored under a non-null field contract");
        if (spec->is_static && !source.null && source.range.root != 0)
            return fail(o,site,"static field would retain a non-static reference");
        if (spec->family && !source.null && source.family != spec->family)
            return fail(o,site,"owning field requires its declared allocation family");
        if (spec->family && !source.null && owns_storage(o,source.range.root,stored->storage.root,0))
            return fail(o,site,"owning field would create a containment cycle");
        if (!spec->family) { source.family = 0; source.owner = 0; }
    }
    if (v->initialized && !v->moved && source.via == v->reference.via && same_reference(source,v->reference)) {
        v->reference = source; return true;
    }
    if (v->initialized && !v->moved && v->reference.family)
        return fail(o,site,"overwriting an owner would lose its release authority");
    if (v->initialized && !v->moved && v->reference.via != SIZE_MAX && v->reference.via != 0) {
        if (source.via == v->reference.via) {
            Loan *current = loan(o,source.via);
            if (!current || !within(current->range,source.range) || source.family)
                return fail(o,site,"self-rebinding cannot widen a loan or change release authority");
            for (size_t i = 0; i < o->state.nloans; ++i)
                if (o->state.loans[i].live && ancestor(o,current->id,&o->state.loans[i]))
                    return fail(o,site,"cannot narrow a view while it is borrowed");
            current->range = source.range;
            if (o->plan) { fprintf(o->plan," (restrict %zu ",current->id); range_plan(o->plan,source.range); fputs(")\n",o->plan); }
            v->reference = source; return true;
        }
        if (!end(o,site,v->reference.via)) return false;
    }
    if (stored && !stored->spec->is_static && source.null) {
        v->reference = source; v->initialized = true; v->moved = false; return true;
    }
    if (source.null) source = static_reference(true);
    if (source.family && (stored ? stored->spec->family != 0 : write)) {
        if (source.owner) {
            Variable *previous = binding_id(o,source.owner);
            Loan *old = loan(o,source.via);
            if (!old || !previous->declared || previous->moved || !previous->reference.family)
                return fail(o,site,"owned value was already moved or released");
            for (size_t i = 0; i < o->state.nloans; ++i)
                if (o->state.loans[i].live && ancestor(o,old->id,&o->state.loans[i]))
                    return fail(o,site,"cannot move an allocation while it is borrowed");
            Loan moved = *old; old->live = false; moved.id = o->state.next++; moved.scope = v->scope;
            append_loan(o,moved);
            if (o->plan) fprintf(o->plan," (move %zu %zu)\n",source.via,moved.id);
            source.via = moved.id;
            previous->moved = true; previous->reference.family = 0;
        }
        source.owner = e->id;
    } else if (source.via != 0 || view(o,e->type)) {
        size_t id;
        if (!borrow(o,site,source,write,&id)) return false;
        source.via = id; source.family = 0; source.owner = 0;
    } else if (write) return fail(o,site,"immutable static data cannot become an exclusive reference");
    v->reference = source; v->initialized = true; v->moved = false; return true;
}
static CeEntity *named_variable(Ownership *o, const CeTree *t) {
    return kind(o,t) == CE_K_Var ? entity(o,t->children[0]) : NULL;
}
static bool narrow_result(Ownership *o, const CeTree *site, Reference *r,
                           CeType *type, const CeAtom *path) {
    if (!path || !r->exact) return true;
    if (!pointer(type)) return fail(o,site,"borrow result origin is not a pointer");
    type = type->base;
    for (size_t i = 0; i < path->len; ++i) {
        const CeAtom *name = path->items[i];
        if (name->kind != CE_ATOM_SYMBOL) return malformed(o,name,"result path requires member names");
        Place before = {r->range,r->via,r->exact,NULL,-1}, after;
        if (!field(o,site,before,type,name->text,&after)) return false;
        r->range = after.range; r->exact = after.exact;
        bool found = false;
        CeRecord *record = type->tag->record;
        for (size_t j = 0; j < record->nmembers; ++j) if (same(record->members[j].name,name->text)) {
            type = record->members[j].type;
            if (pointer(type)) {
                Variable *value = after.variable ? binding_id(o,after.variable->id) : NULL;
                if (!value || !value->initialized || value->moved)
                    return fail(o,site,"borrow result refers to an unavailable record field");
                *r = value->reference; type = type->base;
                reference_authority(o,r,record->members[j].type);
            }
            found = true; break;
        }
        if (!found) return fail(o,site,"result path member not found");
    }
    return true;
}

static void call_argument_plan(Ownership *o, Function *f, const CeTree *argument,
                               Reference reference, size_t id, size_t index) {
    CeType *type = typed(o,argument)->type, *ft = f->entity->type;
    bool is_pointer = reference_type(o,type) || type->kind == CE_T_ARRAY ||
        (index < ft->nparameters && reference_type(o,ft->parameters[index]));
    if (!is_pointer) fputs(" (value)",o->plan);
    else if (index == 0 && f->release_family) {
        if (reference.null) fputs(" (null)",o->plan);
        else fprintf(o->plan," (released %zu)",reference.via);
    } else if (index < ft->nparameters && f->modes[index] == 2)
        fprintf(o->plan," (owned %zu)",reference.via);
    else if (reference.null) fputs(" (null)",o->plan);
    else fprintf(o->plan," (%s %zu)",reference.nullable ? "maybe" : "loan",id);
}

static bool call(Ownership *o, const CeTree *t, Reference *out) {
    CeEntity *callee = named_variable(o,t->children[0]);
    Function *f = callee && callee->kind == CE_ENTITY_FUNCTION ? function_named(o,callee->name) : NULL;
    if (!f || (!f->checked && !f->foreign)) return fail(o,t,"call requires a checked body or an explicit foreign obligation");
    const CeTree *args = t->children[1];
    const CeTree *list = kind(o,args) == CE_K_Arguments ? args->children[0] : NULL;
    size_t count = list ? list->len : 0;
    CeType *ft = f->entity->type;
    if (count < ft->nparameters || (!ft->variadic && count != ft->nparameters))
        return fail(o,t,"call arity disagrees with the ownership contract");
    Reference *refs = ce_alloc(o->arena,(count ? count : 1)*sizeof(*refs));
    size_t *loans = ce_alloc(o->arena,(count ? count : 1)*sizeof(*loans));
    Reference *field_refs = ce_alloc(o->arena,(f->nfields ? f->nfields : 1)*sizeof(*field_refs));
    size_t *field_loans = ce_alloc(o->arena,(f->nfields ? f->nfields : 1)*sizeof(*field_loans));
    for (size_t i = 0; i < f->nfields; ++i) field_loans[i] = SIZE_MAX;
    size_t *global_loans = f->global_effects ? ce_alloc(o->arena,o->sema->nentities*sizeof(*global_loans)) : NULL;
    size_t nglobals = 0;
    /* C evaluates every argument before entering the callee. Call loans start
     * after preparation, including nested calls in later arguments. */
    for (size_t i = 0; i < count; ++i)
        if (!expression(o,list->children[i],&refs[i])) return false;
    for (size_t i = 0; i < count; ++i) {
        loans[i] = SIZE_MAX;
        const CeTree *arg = list->children[i]; const CeExpr *at = typed(o,arg);
        CeType *object_type = pointer(at->type) ? at->type->base : at->type;
        Capability *opaque = capability(o,object_type);
        if (!opaque) opaque = capability_family(o,refs[i].authority);
        CeType *parameter = i < ft->nparameters ? ft->parameters[i] : NULL;
        bool consumes_opaque = opaque && i < ft->nparameters &&
            ((f->modes[i] == 2 && f->families[i] == opaque->family) || f->release_family == opaque->family);
        if (opaque && (!pointer(parameter) || capability(o,parameter->base) != opaque) && !consumes_opaque)
            return fail(o,arg,"opaque capability cannot bypass its typed interface");
        if (pointer(at->type) && pointer(parameter) && parameter->base->kind == CE_T_VOID &&
            (reference_record(object_type,0) || reference_storage(o,refs[i].range)) && !consumes_opaque &&
            !(f->release_family && refs[i].family == f->release_family))
            return fail(o,arg,"reference-bearing storage cannot be erased through a void pointer interface");
        if (ce_type_record(object_type) && reference_record(object_type,0) && !record_spec(o,object_type) &&
            (pointer(at->type) || !view(o,object_type)))
            return fail(o,arg,"reference-bearing record pointers need field effects; pass a declared view by value");
        for (size_t j = 0; j < f->nfields; ++j) if (f->fields[j].parameter == i) {
            FieldPart *part = &f->fields[j];
            if (!refs[i].exact || refs[i].null || refs[i].nullable)
                return fail(o,arg,"record field arguments require a precise non-null record location");
            Range slot = {refs[i].range.root,refs[i].range.first+part->offset,ce_type_size(o->sema,part->field->type)};
            StoredField *cell = field_cell(o,arg,part->field,slot);
            if (!cell->value.initialized || cell->value.moved)
                return fail(o,arg,"call reads an uninitialized or moved record field");
            if (!access(o,arg,refs[i].via,slot,false)) return false;
            field_refs[j] = cell->value.reference;
            if (part->field->is_static && !field_refs[j].null && field_refs[j].range.root != 0)
                return fail(o,arg,"record argument violates its static field contract");
            if (!part->field->nullable && (field_refs[j].null || field_refs[j].nullable))
                return fail(o,arg,"record argument does not establish a non-null field");
        }
        bool is_pointer = reference_type(o,at->type) || at->type->kind == CE_T_ARRAY ||
            (i < ft->nparameters && reference_type(o,ft->parameters[i]));
        if (!is_pointer) continue;
        if (i < ft->nparameters && f->foreign && !f->release_family &&
            pointer(ft->parameters[i]) && pointer(ft->parameters[i]->base) &&
            !read_only(ft->parameters[i]) && f->stores[i] == -1)
            return fail(o,arg,"native pointer output requires a stores or preserves lifetime contract");
        if (i == 0 && f->release_family) {
            Reference r = refs[i];
            if (r.null) continue;
            Loan *l = loan(o,r.via);
            if (r.family != f->release_family || !l || l->family != r.family)
                return fail(o,arg,"release requires an owner from the matching allocator family");
            Variable *owner = binding_id(o,r.owner);
            if ((owner && (!owner->declared || owner->moved)) || !end(o,arg,r.via)) return false;
            if (o->plan) fprintf(o->plan," (release %zu %u)\n",r.via,r.family);
            if (owner) { owner->moved = true; owner->reference.family = 0; }
            continue;
        }
        if (i < ft->nparameters && f->nullable[i] && refs[i].null) continue;
        if (i < ft->nparameters && pointer(ft->parameters[i]) &&
            ce_type_size(o->sema,ft->parameters[i]->base) > refs[i].range.count)
            return fail(o,arg,"argument extent is smaller than the callee's complete pointee");
        if (i < ft->nparameters && f->modes[i] == 2) {
            Reference r = refs[i];
            if (r.owner && binding_id(o,r.owner)->moved)
                return fail(o,arg,"call consumes an already moved value");
            if (r.family != f->families[i] || !checked_reference(o,arg,r,true))
                return fail(o,arg,"owned parameter requires matching, live release authority");
            Loan *l = loan(o,r.via);
            if (l->family != r.family) return fail(o,arg,"owned argument has no release authority");
            for (size_t j = 0; j < o->state.nloans; ++j)
                if (o->state.loans[j].live && ancestor(o,l->id,&o->state.loans[j]))
                    return fail(o,arg,"cannot transfer an owner while it is borrowed");
            l->live = false;
            if (o->plan) fprintf(o->plan," (handoff %zu %u)\n",r.via,r.family);
            if (r.owner) { Variable *v = binding_id(o,r.owner); v->moved = true; v->reference.family = 0; }
            continue;
        }
        if (i >= ft->nparameters && !f->variadic_read)
            return fail(o,arg,"pointer variadic argument requires a non-retaining read contract");
        bool write = i < ft->nparameters && !reference_read_only(o,ft->parameters[i]);
        bool optional = i < ft->nparameters && f->nullable[i];
        if (optional && refs[i].null) continue;
        if (!checked_reference(o,arg,refs[i],!optional) || !borrow(o,arg,refs[i],write,&loans[i])) return false;
    }
    for (size_t i = 0; i < f->nfields; ++i) {
        FieldPart *part = &f->fields[i]; Reference r = field_refs[i];
        if (r.null) {
            if (!part->field->nullable) return fail(o,t,"null record field violates the call contract");
            if (part->owning && r.owner) binding_id(o,r.owner)->moved = true;
            continue;
        }
        if (part->owning) {
            Loan *l = loan(o,r.via);
            if (r.family != part->field->family || !l || l->family != r.family)
                return fail(o,t,"record call requires the field's matching release authority");
            for (size_t j = 0; j < o->state.nloans; ++j)
                if (o->state.loans[j].live && ancestor(o,l->id,&o->state.loans[j]))
                    return fail(o,t,"cannot transfer a record field while borrowed");
            l->live = false;
            if (o->plan) fprintf(o->plan," (handoff %zu %u)\n",r.via,r.family);
            Variable *owner = binding_id(o,r.owner);
            if (owner) { owner->moved = true; owner->reference.family = 0; }
        } else if (!part->field->is_static &&
            (!checked_reference(o,t,r,!part->field->nullable) || !borrow(o,t,r,part->writing,&field_loans[i]))) return false;
    }
    for (size_t i = 0; f->global_effects && i < o->sema->nentities; ++i) {
        if (f->global_effects[i]) {
            Loan *l = loan(o,o->function->global_roots[i]);
            if (!l || !l->live) return fail(o,t,"call lacks the global permission required by its transitive effects");
            Reference r = {l->range,l->id,true,false,false,0,0,0};
            if (!borrow(o,t,r,true,&global_loans[nglobals++])) return false;
        }
    }
    if (o->plan) {
        fprintf(o->plan," (invoke %.*s (arguments",(int)f->name.len,f->name.data);
        for (size_t i = 0; i < ft->nparameters; ++i)
            call_argument_plan(o,f,list->children[i],refs[i],loans[i],i);
        for (size_t i = 0; i < f->nfields; ++i) {
            FieldPart *part = &f->fields[i]; Reference r = field_refs[i];
            if (r.null) fputs(" (null)",o->plan);
            else if (part->owning) fprintf(o->plan," (owned %zu)",r.via);
            else fprintf(o->plan," (%s %zu)",r.nullable ? "maybe" : "loan",part->field->is_static ? 0 : field_loans[i]);
        }
        for (size_t i = 0; i < nglobals; ++i) fprintf(o->plan," (loan %zu)",global_loans[i]);
        for (size_t i = ft->nparameters; i < count; ++i)
            call_argument_plan(o,f,list->children[i],refs[i],loans[i],i);
        fputs("))\n",o->plan);
    }
    for (size_t i = nglobals; i > 0; --i) if (!end(o,t,global_loans[i-1])) return false;
    for (size_t i = f->nfields; i > 0; --i) if (field_loans[i-1] != SIZE_MAX && !end(o,t,field_loans[i-1])) return false;
    for (size_t i = count; i > 0; --i) if (loans[i-1] != SIZE_MAX && !end(o,t,loans[i-1])) return false;
    for (size_t i = 0; i < f->nfields; ++i) if (!f->fields[i].owning && f->fields[i].field->is_static &&
        pointer(ft->parameters[f->fields[i].parameter]) && !read_only(ft->parameters[f->fields[i].parameter])) {
        FieldPart *part = &f->fields[i]; Reference header = refs[part->parameter];
        Range slot = {header.range.root,header.range.first+part->offset,ce_type_size(o->sema,part->field->type)};
        StoredField *cell = field_cell(o,t,part->field,slot);
        cell->value.reference = static_reference(part->field->nullable);
    }
    for (size_t i = 0; i < f->nposts; ++i) {
        FieldPost *post = &f->posts[i]; FieldPart *part = &f->fields[post->part];
        Reference header = refs[post->parameter];
        Range slot = {header.range.root,header.range.first+part->offset,ce_type_size(o->sema,post->field->type)};
        StoredField *cell = field_cell(o,t,post->field,slot);
        Reference value = post->source == -2 ? static_reference(post->field->nullable) : refs[post->source];
        value.family = 0; value.owner = 0; value.exact = false;
        value.nullable = post->field->nullable;
        if (!pointer_binding(o,t,&cell->entity,value)) return false;
    }
    for (size_t i = 0; i < ft->nparameters; ++i) if (f->stores && f->stores[i] >= -2 && f->stores[i] != -1) {
        const CeTree *arg = list->children[i];
        Place target;
        if (kind(o,arg) != CE_K_AddressOf || !place(o,arg->children[0],&target) || !target.variable ||
            !pointer(target.variable->type)) return fail(o,arg,"output reference requires a local pointer slot");
        Reference r = f->stores[i] == -2 ? static_reference(true) : refs[f->stores[i]];
        if (!pointer_binding(o,arg,target.variable,r)) return false;
    }
    *out = (Reference){.null=true,.via=SIZE_MAX};
    if (reference_type(o,ft->base)) {
        if (f->result_family) {
            unsigned long long count = ce_type_size(o->sema,ft->base->base);
            if (f->allocation_count >= 0) {
                const CeExpr *size = typed(o,list->children[f->allocation_count]);
                if (!size->constant) return fail(o,t,"dynamic allocation extent requires a checked byte view");
                count = (unsigned long long)size->value;
            }
            if (!count) return fail(o,t,"owned result requires a nonempty declared extent");
            size_t id = root(o,count,true,"heap",f->result_family);
            *out = (Reference){{id,0,count},id,true,false,f->result_nullable,f->result_family,0,0};
        }
        else if (f->result_static) *out = static_reference(f->result_nullable);
        else if (f->result_from >= 0) {
            *out = refs[f->result_from]; out->nullable = f->result_nullable;
            out->family = 0; out->owner = 0;
            if (view(o,ft->base) && !out->exact)
                return fail(o,t,"returned view requires a precise input extent");
            if (!narrow_result(o,t,out,ft->parameters[f->result_from],f->result_path)) return false;
            out->family = 0; out->owner = 0; out->nullable = f->result_nullable;
            if (!view(o,ft->base)) out->exact = false;
        } else return fail(o,t,"pointer result needs a declared borrow, static or ownership origin");
    }
    reference_authority(o,out,ft->base);
    return true;
}

static void refine(Ownership *o, const CeTree *condition, bool truth) {
    unsigned k = kind(o,condition);
    if (k == CE_K_Not) { refine(o,condition->children[0],!truth); return; }
    if ((k == CE_K_And && truth) || (k == CE_K_Or && !truth)) {
        refine(o,condition->children[0],truth); refine(o,condition->children[1],truth); return;
    }
    const CeTree *candidate = condition;
    if (k == CE_K_Equal || k == CE_K_NotEqual) {
        const CeExpr *r = typed(o,condition->children[1]);
        if (!r || !r->null_pointer) return;
        candidate = condition->children[0]; truth = k == CE_K_NotEqual ? truth : !truth;
    }
    CeEntity *e = named_variable(o,candidate);
    if (truth && e && pointer(e->type) && o->state.variables[e->id-1].declared) {
        o->state.variables[e->id-1].reference.null = false;
        o->state.variables[e->id-1].reference.nullable = false;
    } else if (truth && typed(o,candidate) && pointer(typed(o,candidate)->type) &&
        (kind(o,candidate) == CE_K_Member || kind(o,candidate) == CE_K_PointerMember)) {
        Place p;
        if (place(o,candidate,&p) && p.variable) {
            Variable *v = binding_id(o,p.variable->id);
            if (v && v->initialized && !v->moved) { v->reference.null = false; v->reference.nullable = false; }
        }
    }
}

static bool initializer(Ownership *o, const CeTree *t) {
    unsigned k = kind(o,t);
    if (k == CE_K_InitializerExpression) { Reference ignored; return expression(o,t->children[0],&ignored); }
    if (k == CE_K_DesignatedInitializerElement) return initializer(o,t->children[1]);
    if (k == CE_K_InitializerElement) return initializer(o,t->children[0]);
    if (k == CE_K_InitializerList || k == CE_K_InitializerListComma ||
        k == CE_K_CompoundLiteral || k == CE_K_CompoundLiteralComma) {
        const CeTree *list = t->children[k == CE_K_CompoundLiteral || k == CE_K_CompoundLiteralComma ? 1 : 0];
        for (size_t i = 0; i < list->len; ++i) if (!initializer(o,list->children[i])) return false;
    } else if (k != CE_K_GNUEmptyInitializer && k != CE_K_GNUEmptyCompoundLiteral)
        return fail(o,t,"unsupported initializer in checked ownership body");
    return true;
}

static bool record_copy(Ownership *o, const CeTree *site, CeType *type, Place destination, Place source) {
    if (!destination.exact || !source.exact) return fail(o,site,"record copy requires a precise object location");
    if (destination.range.root == source.range.root && destination.range.first == source.range.first) return true;
    if (!access(o,site,source.via,source.range,false)) return false;
    CeRecord *record = type->tag->record;
    for (size_t i = 0; i < record->nmembers; ++i) {
        CeMember *m = &record->members[i]; Place to, from;
        if (!field(o,site,destination,type,m->name,&to) || !field(o,site,source,type,m->name,&from)) return false;
        if (pointer(m->type)) {
            Variable *value = from.variable ? binding_id(o,from.variable->id) : NULL;
            if (!value || !value->initialized || value->moved)
                return fail(o,site,"record copy reads an uninitialized or moved reference field");
            Reference saved = value->reference;
            if (!pointer_binding(o,site,to.variable,saved)) return false;
        } else if (ce_type_record(m->type) && reference_record(m->type,0)) {
            if (!record_spec(o,m->type) || !record_copy(o,site,m->type,to,from)) return false;
        } else if (reference_record(m->type,0))
            return fail(o,site,"reference-bearing array copy requires element lifetime contracts");
    }
    return true;
}

static bool record_initializer(Ownership *o, const CeTree *t, CeType *type, Place destination) {
    if (!destination.exact) return fail(o,t,"record initialization requires a precise storage location");
    unsigned k = kind(o,t);
    if (k == CE_K_InitializerExpression) {
        const CeTree *expression_tree = t->children[0];
        unsigned ek = kind(o,expression_tree);
        if (ek == CE_K_CompoundLiteral || ek == CE_K_CompoundLiteralComma || ek == CE_K_GNUEmptyCompoundLiteral)
            return record_initializer(o,expression_tree,type,destination);
        Place source;
        if (!place(o,expression_tree,&source)) return false;
        return record_copy(o,expression_tree,type,destination,source);
    }
    bool zero = !t || k == CE_K_GNUEmptyInitializer || k == CE_K_GNUEmptyCompoundLiteral;
    const CeTree *elements = zero ? NULL :
        t->children[k == CE_K_CompoundLiteral || k == CE_K_CompoundLiteralComma ? 1 : 0];
    if (!zero && k != CE_K_CompoundLiteral && k != CE_K_CompoundLiteralComma &&
        k != CE_K_InitializerList && k != CE_K_InitializerListComma)
        return fail(o,t,"unsupported reference-record initializer");
    CeRecord *record = type->tag->record;
    const CeTree **values = ce_alloc(o->arena,record->nmembers*sizeof(*values));
    memset(values,0,record->nmembers*sizeof(*values));
    size_t position = 0;
    if (elements) for (size_t i = 0; i < elements->len; ++i) {
        const CeTree *item = elements->children[i], *value;
        if (kind(o,item) == CE_K_DesignatedInitializerElement) {
            const CeTree *designators = item->children[0];
            if (designators->len != 1 || kind(o,designators->children[0]) != CE_K_MemberDesignator)
                return fail(o,item,"reference-record initialization needs direct member designators");
            CeText name = ce_sema_name(o->sema,designators->children[0]->children[0]);
            position = record->nmembers;
            for (size_t j = 0; j < record->nmembers; ++j) if (same(record->members[j].name,name)) position = j;
            value = item->children[1];
        } else if (kind(o,item) == CE_K_InitializerElement) value = item->children[0];
        else return fail(o,item,"unsupported reference-record initializer element");
        if (position >= record->nmembers || values[position]) return fail(o,item,"duplicate or unknown record initializer");
        values[position++] = value;
    }
    for (size_t i = 0; i < record->nmembers; ++i) {
        CeMember *m = &record->members[i]; Place target;
        if (!field(o,t,destination,type,m->name,&target)) return false;
        if (pointer(m->type)) {
            if (!target.variable) return fail(o,t,"pointer field has no lifetime declaration");
            Reference source = {.null=true,.via=SIZE_MAX};
            if (values[i]) {
                if (kind(o,values[i]) != CE_K_InitializerExpression ||
                    !expression(o,values[i]->children[0],&source)) return false;
            }
            if (!pointer_binding(o,t,target.variable,source)) return false;
        } else if (ce_type_record(m->type) && reference_record(m->type,0)) {
            if (!record_spec(o,m->type)) return fail(o,t,"nested record requires a reference-field declaration");
            if (!record_initializer(o,values[i],m->type,target)) return false;
        } else if (reference_record(m->type,0))
            return fail(o,t,"reference-bearing array initialization requires element lifetime contracts");
        else if (values[i] && !initializer(o,values[i])) return false;
    }
    return true;
}

static bool view_initializer(Ownership *o, const CeTree *t, View *spec, Reference *out) {
    unsigned k = kind(o,t);
    if (k == CE_K_InitializerExpression) return expression(o,t->children[0],out);
    if (k != CE_K_InitializerList && k != CE_K_InitializerListComma &&
        k != CE_K_CompoundLiteral && k != CE_K_CompoundLiteralComma)
        return fail(o,t,"bounded view requires both pointer and length initializers");
    const CeTree *items = t->children[k == CE_K_CompoundLiteral || k == CE_K_CompoundLiteralComma ? 1 : 0];
    const CeTree *data = NULL, *length = NULL;
    size_t position = 0; CeRecord *record = spec->type->tag->record;
    for (size_t i = 0; i < items->len; ++i) {
        const CeTree *item = items->children[i], *value = NULL;
        if (kind(o,item) == CE_K_DesignatedInitializerElement) {
            const CeTree *designators = item->children[0];
            if (designators->len != 1 || kind(o,designators->children[0]) != CE_K_MemberDesignator)
                return fail(o,item,"bounded view initializer needs direct member designators");
            CeText name = ce_sema_name(o->sema,designators->children[0]->children[0]);
            position = 2;
            for (size_t m = 0; m < 2; ++m) if (same(record->members[m].name,name)) position = m;
            value = item->children[1];
        } else if (kind(o,item) == CE_K_InitializerElement) value = item->children[0];
        else return fail(o,item,"unsupported bounded view initializer element");
        if (position >= 2 || kind(o,value) != CE_K_InitializerExpression)
            return fail(o,item,"bounded view initializer must initialize its two scalar fields once");
        const CeTree **target = &record->members[position] == spec->data ? &data : &length;
        if (*target) return fail(o,item,"duplicate bounded view field initializer");
        *target = value->children[0]; ++position;
    }
    if (!data || !length) return fail(o,t,"bounded view requires explicit pointer and length");
    const CeExpr *extent = typed(o,length);
    unsigned long long count = extent && extent->constant ? extent->value : 0;
    bool known = extent && extent->constant;
    if (!known && kind(o,length) == CE_K_Member) {
        CeEntity *e = named_variable(o,length->children[0]);
        View *parent = e ? view(o,e->type) : NULL;
        if (parent && same(parent->length->name,ce_sema_name(o->sema,length->children[1]))) {
            Variable *v = &o->state.variables[e->id-1];
            if (v->declared && v->initialized) { count = v->reference.range.count; known = true; }
        }
    }
    if (!known) return fail(o,length,"dynamic view length needs a checked byte constructor");
    Reference ignored;
    if (!expression(o,length,&ignored)) return false;
    Reference source;
    if (!expression(o,data,&source)) return false;
    if (source.authority) return fail(o,data,"opaque capabilities cannot be exposed as byte views");
    if (spec->family) {
        if (source.family != spec->family || count != source.range.count)
            return fail(o,data,"owning view requires the whole allocation from its declared family");
    } else if (source.family && !source.owner) return fail(o,data,"view cannot discard a temporary allocation owner");
    if (source.null && count == 0 && !spec->write) source = (Reference){{0,0,0},0,true,false,false,0,0,0};
    if (!source.exact || !checked_reference(o,data,source,true))
        return fail(o,data,"bounded view requires a precise, non-null source extent");
    if (count > source.range.count) return fail(o,length,"view length exceeds the source allocation range");
    source.range.count = count;
    if (!spec->family) { source.family = 0; source.owner = 0; }
    if (!access(o,t,source.via,source.range,spec->write)) return false;
    if (o->plan) {
        fprintf(o->plan," (view %zu ",source.via); range_plan(o->plan,source.range);
        fprintf(o->plan," %llu %s)\n",count,spec->write ? "write" : "read");
    }
    *out = source;
    return true;
}

static bool expression(Ownership *o, const CeTree *t, Reference *out) {
    *out = (Reference){.null=true,.via=SIZE_MAX};
    unsigned k = kind(o,t); const CeExpr *type = typed(o,t);
    if (k == CE_K_NoFullExpression || k == CE_K_NoExpression) return true;
    if (k == CE_K_SomeFullExpression || k == CE_K_SomeExpression) return expression(o,t->children[0],out);
    if (k == CE_K_SizeofType || k == CE_K_SizeofExpression || k == CE_K_Alignof || k == CE_K_GNUOffsetof)
        return (type && type->constant) || fail(o,t,"runtime type-layout expression requires an explicit checked operation");
    if (k == CE_K_IntConst || k == CE_K_FloatConst || k == CE_K_CharConst) return true;
    if (k == CE_K_StringLiterals) { *out = static_reference(false); return true; }
    if (k == CE_K_Var) {
        CeEntity *e = entity(o,t->children[0]);
        if (e && (e->kind == CE_ENTITY_ENUMERATOR || e->kind == CE_ENTITY_FUNCTION)) return true;
        if (type->type->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC))
            return fail(o,t,"volatile and atomic accesses require a native resource contract");
        Place p;
        if (!place(o,t,&p)) return false;
        Variable *v = &o->state.variables[p.variable->id-1];
        if ((!v->initialized && !record_spec(o,type->type)) || v->moved) return fail(o,t,"read of an uninitialized or moved value");
        if (type->type->kind == CE_T_ARRAY) {
            *out = (Reference){p.range,p.via,p.exact,false,false,0,0,0}; return true;
        }
        if (!access(o,t,p.via,p.range,false)) return false;
        if (record_spec(o,type->type)) { *out = (Reference){p.range,p.via,p.exact,false,false,0,0,0}; return true; }
        if (reference_type(o,type->type)) { *out = v->reference; reference_authority(o,out,type->type); }
        return true;
    }
    if (k == CE_K_AddressOf) {
        if (view_component(o,t->children[0])) return fail(o,t,"view fields cannot escape their pointer/length contract");
        Place p; if (!place(o,t->children[0],&p)) return false;
        if (p.variable && !binding_id(o,p.variable->id)->initialized)
            return fail(o,t,"borrow of uninitialized storage needs an output-initialization contract");
        *out = (Reference){p.range,p.via,p.exact,false,false,0,0,0};
        reference_authority(o,out,type->type); return true;
    }
    if (k == CE_K_Member || k == CE_K_PointerMember || k == CE_K_Indirection || k == CE_K_Subscript) {
        if (type->type->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC))
            return fail(o,t,"volatile and atomic accesses require a native resource contract");
        Place p;
        if (!place(o,t,&p) || !access(o,t,p.via,p.range,false)) return false;
        if (type->type->kind == CE_T_ARRAY) *out = (Reference){p.range,p.via,p.exact,false,false,0,0,0};
        else if (pointer(type->type)) {
            if (p.variable && p.variable->id > o->sema->nentities) {
                Variable *v = binding_id(o,p.variable->id);
                if (!v || !v->initialized || v->moved) return fail(o,t,"reference field is uninitialized or moved");
                *out = v->reference; reference_authority(o,out,type->type); return true;
            }
            if (k == CE_K_Member && view(o,typed(o,t->children[0])->type)) {
                CeEntity *e = named_variable(o,t->children[0]);
                if (!e) return fail(o,t,"view member access requires a named checked view");
                *out = o->state.variables[e->id-1].reference; return true;
            }
            if (p.output < 0) return fail(o,t,"pointer-valued subobject requires a field lifetime contract");
            Variable *v = &o->state.variables[o->function->parameters[p.output]->id-1];
            if (!v->output_initialized) return fail(o,t,"output reference read before initialization");
            *out = v->output;
        }
        reference_authority(o,out,type->type);
        return true;
    }
    if (k == CE_K_Call) return call(o,t,out);
    if (k == CE_K_Cast) {
        Reference r;
        if (!expression(o,t->children[1],&r)) return false;
        if (ce_type_void(type->type) && r.family && !r.owner)
            return fail(o,t,"discarded owned result loses its release authority");
        if (pointer(type->type)) {
            const CeExpr *from = typed(o,t->children[1]);
            Capability *target = capability(o,type->type->base);
            Capability *origin = pointer(from->type) ? capability(o,from->type->base) : NULL;
            if (!r.null && r.authority && (!target || target->family != r.authority))
                return fail(o,t,"opaque capability representation cannot be exposed by a pointer cast");
            if (reference_storage(o,r.range) && !ce_type_similar(type->type,from->type))
                return fail(o,t,"reference-bearing storage cannot be reinterpreted through a pointer cast");
            if (target && !from->null_pointer && origin != target && r.family != target->family)
                return fail(o,t,"opaque capability cannot be forged from another allocation or representation");
            if (!pointer(from->type) && !from->null_pointer) return fail(o,t,"integer-to-pointer cast cannot manufacture ownership");
            if (pointer(from->type) && !ce_type_similar(type->type,from->type) &&
                !(r.family && from->type->base->kind == CE_T_VOID))
                return fail(o,t,"pointer reinterpretation needs an explicit representation obligation");
            if (r.family && ce_type_size(o->sema,type->type->base) > r.range.count)
                return fail(o,t,"owned allocation is smaller than the requested pointee");
            *out = r;
        } else if (pointer(typed(o,t->children[1])->type) && !ce_type_void(type->type))
            return fail(o,t,"pointer-to-integer conversion cannot escape a checked borrow");
        return true;
    }
    if (k == CE_K_CompoundLiteral || k == CE_K_CompoundLiteralComma || k == CE_K_GNUEmptyCompoundLiteral) {
        if (contains_capability(o,type->type,0)) return fail(o,t,"opaque capability values require their declared issuer");
        View *spec = view(o,type->type);
        if (spec) return view_initializer(o,t,spec,out);
        if (reference_record(type->type,0)) return fail(o,t,"reference-bearing compound literal needs field ownership contracts");
        return initializer(o,t);
    }
    if (k == CE_K_Assign || (k >= CE_K_MultiplyAssign && k <= CE_K_BitOrAssign)) {
        if (view_component(o,t->children[0])) return fail(o,t,"replace a view as one value; its fields cannot be updated independently");
        Place p; Reference r;
        if (record_spec(o,typed(o,t->children[0])->type)) {
            if (k != CE_K_Assign || !place(o,t->children[0],&p) || !access(o,t,p.via,p.range,true)) return false;
            const CeTree *value = t->children[1];
            CeType *record_type = typed(o,t->children[0])->type;
            unsigned vk = kind(o,value);
            if (vk == CE_K_CompoundLiteral || vk == CE_K_CompoundLiteralComma || vk == CE_K_GNUEmptyCompoundLiteral) {
                if (!record_initializer(o,value,record_type,p)) return false;
            } else {
                Place source;
                if (!place(o,value,&source) || !record_copy(o,t,record_type,p,source)) return false;
            }
            if (p.variable) binding_id(o,p.variable->id)->initialized = true;
            return true;
        }
        if (!place(o,t->children[0],&p) || !expression(o,t->children[1],&r) || !access(o,t,p.via,p.range,true)) return false;
        const CeExpr *target = typed(o,t->children[0]);
        if (target->type->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC))
            return fail(o,t,"volatile and atomic accesses require a native resource contract");
        if (reference_type(o,target->type)) {
            if (k != CE_K_Assign) return fail(o,t,"pointer arithmetic assignment requires a bounded view");
            if (p.variable) { if (!pointer_binding(o,t,p.variable,r)) return false; }
            else if (p.output >= 0 && o->function->stores[p.output] >= -2 && o->function->stores[p.output] != -1) {
                int source = o->function->stores[p.output];
                if (!r.null && source == -2 && r.range.root != 0) return fail(o,t,"output would retain a non-static borrow");
                if (!r.null && source >= 0) {
                    Reference origin = o->state.variables[o->function->parameters[source]->id-1].reference;
                    if (!within(origin.range,r.range)) return fail(o,t,"output borrow has the wrong allocation origin");
                }
                Variable *v = &o->state.variables[o->function->parameters[p.output]->id-1];
                if (v->output_initialized && v->output.via != 0 && v->output.via != SIZE_MAX &&
                    !end(o,t,v->output.via)) return false;
                if (r.null) r = static_reference(true);
                if (r.via != 0) {
                    size_t id;
                    if (!borrow(o,t,r,!read_only(target->type),&id)) return false;
                    r.via = id; r.family = 0; r.owner = 0;
                }
                v->output = r; v->output_initialized = true;
            } else return fail(o,t,"pointer store has no declared lifetime destination");
            *out = p.variable ? binding_id(o,p.variable->id)->reference : r;
        } else if (p.variable) binding_id(o,p.variable->id)->initialized = true;
        return true;
    }
    if (k == CE_K_PreIncrement || k == CE_K_PreDecrement || k == CE_K_PostIncrement || k == CE_K_PostDecrement) {
        if (view_component(o,t->children[0])) return fail(o,t,"view fields cannot be updated independently");
        if (typed(o,t->children[0])->type->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC))
            return fail(o,t,"volatile and atomic accesses require a native resource contract");
        if (pointer(typed(o,t->children[0])->type)) return fail(o,t,"pointer increment requires a bounded view");
        Place p; return place(o,t->children[0],&p) && access(o,t,p.via,p.range,true);
    }
    if (k == CE_K_And || k == CE_K_Or || k == CE_K_Conditional) {
        Reference first;
        if (!expression(o,t->children[0],&first)) return false;
        State start = clone(o); Reference yes = {.null=true,.via=SIZE_MAX}, no = yes;
        if (o->plan) fputs(" (branch (then\n",o->plan);
        refine(o,t->children[0],true);
        if (k != CE_K_Or && !expression(o,t->children[1],&yes)) return false;
        State left = clone(o); o->state = start;
        if (o->plan) fputs(" ) (else\n",o->plan);
        refine(o,t->children[0],false);
        if (k == CE_K_Or && !expression(o,t->children[1],&no)) return false;
        if (k == CE_K_Conditional && !expression(o,t->children[2],&no)) return false;
        State right = clone(o);
        if (o->plan) fputs(" ))\n",o->plan);
        if (!join(o,t,left,right)) return false;
        if (k == CE_K_Conditional && reference_type(o,type->type)) {
            if (yes.null && no.range.root == 0) { *out = no; out->nullable = true; }
            else if (no.null && yes.range.root == 0) { *out = yes; out->nullable = true; }
            else if (same_reference(yes,no)) { *out = yes; out->nullable |= no.nullable; }
            else return fail(o,t,"conditional reference needs a common lifetime origin");
        }
        return true;
    }
    if (k == CE_K_Comma) {
        Reference ignored;
        if (!expression(o,t->children[0],&ignored)) return false;
        if (ignored.family && !ignored.owner) return fail(o,t,"discarded owned result loses its release authority");
        return expression(o,t->children[1],out);
    }
    if (k == CE_K_UnaryPlus || k == CE_K_Negate || k == CE_K_BitNot || k == CE_K_Not) {
        Reference ignored; return expression(o,t->children[0],&ignored);
    }
    if (k >= CE_K_Multiply && k <= CE_K_BitOr) {
        Reference a, b;
        if (!expression(o,t->children[0],&a) || !expression(o,t->children[1],&b)) return false;
        if (pointer(type->type)) return fail(o,t,"pointer arithmetic requires a bounded view");
        return true;
    }
    return fail(o,t,"unsupported expression in a checked ownership body (kind %u)",k);
}

static bool bind_variable(Ownership *o, CeEntity *e, const CeTree *site, bool initialized) {
    if (!e || e->kind != CE_ENTITY_OBJECT) return fail(o,site,"local declaration has no object identity");
    if (e->scope == CE_SCOPE_FILE) return fail(o,site,"mutable static storage requires an explicit owner");
    if (contains_capability(o,e->type,0)) return fail(o,site,"opaque capability values require their declared issuer");
    if (e->type->kind == CE_T_ARRAY && reference_record(e->type,0))
        return fail(o,site,"reference-bearing arrays require element lifetime contracts");
    if (ce_type_record(e->type) && !view(o,e->type) && !record_spec(o,e->type) && reference_record(e->type,0))
        return fail(o,site,"reference-bearing aggregate needs field ownership contracts");
    Variable *v = &o->state.variables[e->id-1];
    if (v->declared) return fail(o,site,"object redeclared in the current ownership state");
    *v = (Variable){.declared=true,.initialized=initialized,.scope=o->state.scope};
    v->reference.null = true; v->reference.via = SIZE_MAX;
    unsigned long long size = ce_type_size(o->sema,e->type);
    if (!size) return fail(o,site,"incomplete or variable-size local object");
    v->slot = root(o,size,!(e->type->qualifiers & CE_Q_CONST),"local",0);
    return true;
}

static bool checked_reference(Ownership *o, const CeTree *site, Reference r, bool dereference) {
    if (r.null || (dereference && r.nullable)) return fail(o,site,"nullable reference requires a non-null branch before access");
    return loan(o,r.via) != NULL || fail(o,site,"use after move, release or end of borrow");
}

static bool field(Ownership *o, const CeTree *site, Place base, CeType *record, CeText name, Place *out) {
    if (capability(o,record)) return fail(o,site,"opaque capability representation is private");
    if (!record || !ce_type_record(record) || !record->tag || !record->tag->record)
        return fail(o,site,"member has no complete record layout");
    if (record->kind == CE_T_UNION) return fail(o,site,"overlapping union fields require an explicit representation obligation");
    CeRecord *layout = record->tag->record;
    for (size_t i = 0; i < layout->nmembers; ++i) if (same(layout->members[i].name,name)) {
        CeMember *m = &layout->members[i]; *out = base;
        if (m->bitfield) return fail(o,site,"bit-field access requires a checked layout accessor");
        if (base.exact) {
            out->range.first += m->offset;
            out->range.count = ce_type_size(o->sema,m->type);
            if (!within(base.range,out->range)) return fail(o,site,"member range exceeds the borrowed object");
        }
        out->variable = NULL;
        FieldSpec *spec = field_spec(o,record,name);
        if (spec && base.exact) out->variable = &field_cell(o,site,spec,out->range)->entity;
        return true;
    }
    return fail(o,site,"anonymous member projection is not an ownership place");
}

static bool place(Ownership *o, const CeTree *t, Place *out) {
    unsigned k = kind(o,t);
    if (k == CE_K_Var) {
        CeEntity *e = entity(o,t->children[0]);
        if (!e || (e->kind != CE_ENTITY_OBJECT && e->kind != CE_ENTITY_PARAMETER))
            return fail(o,t,"place does not name an object");
        if (contains_capability(o,e->type,0)) return fail(o,t,"opaque capability values require their declared issuer");
        if (e->scope == CE_SCOPE_FILE && o->global_owners[e->id-1].len &&
            !same(o->global_owners[e->id-1],o->function->name) && !object_read_only(e->type))
            return fail(o,t,"global access requires its declared owner");
        Variable *v = &o->state.variables[e->id-1];
        if (!v->declared && e->scope == CE_SCOPE_FILE && object_read_only(e->type) &&
            !reference_record(e->type,0))
            *v = (Variable){.declared=true,.initialized=true,.immortal=true,.slot=0,
                .reference={.null=true,.via=SIZE_MAX}};
        if (!v->declared) return fail(o,t,"global or out-of-scope object has no declared owner");
        Loan *l = loan(o,v->slot);
        if (!l) return fail(o,t,"object lifetime has ended");
        Range range = v->immortal ? (Range){0,0,ce_type_size(o->sema,e->type)} : l->range;
        *out = (Place){range,v->slot,true,e,-1}; return true;
    }
    if (k == CE_K_Indirection || k == CE_K_PointerMember) {
        Reference r;
        if (!expression(o,t->children[0],&r) || !checked_reference(o,t,r,true)) return false;
        Place base = {r.range,r.via,r.exact,NULL,-1};
        if (k == CE_K_Indirection) {
            *out = base;
            /* An output parameter's slot is an independently checked place. */
            if (kind(o,t->children[0]) == CE_K_Var) {
                CeEntity *e = entity(o,t->children[0]->children[0]);
                for (size_t i = 0; i < o->function->nparameters; ++i)
                    if (o->function->parameters[i] == e) out->output = (int)i;
            }
            return true;
        }
        const CeExpr *p = typed(o,t->children[0]);
        return field(o,t,base,p->type->base,ce_sema_name(o->sema,t->children[1]),out);
    }
    if (k == CE_K_Member) {
        Place base;
        return place(o,t->children[0],&base) &&
            field(o,t,base,typed(o,t->children[0])->type,ce_sema_name(o->sema,t->children[1]),out);
    }
    if (k == CE_K_Subscript) {
        const CeTree *base = t->children[0], *index = t->children[1];
        const CeExpr *bt = typed(o,base), *it = typed(o,index);
        Place p;
        if (bt->type->kind == CE_T_ARRAY) {
            if (!place(o,base,&p)) return false;
        } else {
            if (!it->constant) return fail(o,t,"dynamic pointer subscript requires a checked bounded view");
            Reference r;
            if (!expression(o,base,&r) || !checked_reference(o,t,r,true)) return false;
            p = (Place){r.range,r.via,r.exact,NULL,-1};
            if (!p.exact && it->value != 0)
                return fail(o,t,"interior reference has no precise remaining extent; use a bounded view");
        }
        Reference ignored;
        if (!expression(o,index,&ignored)) return false;
        unsigned long long size = ce_type_size(o->sema,typed(o,t)->type);
        if (p.exact && it->constant) {
            unsigned long long at = it->value;
            if (!size || at >= p.range.count/size) return fail(o,t,"constant subscript exceeds the borrowed range");
            p.range.first += at*size; p.range.count = size;
        } else p.exact = false; /* unknown indices conservatively borrow the whole range */
        p.variable = NULL; *out = p; return true;
    }
    return fail(o,t,"expression is not a supported ownership place");
}

static bool cleanup(Ownership *o, const CeTree *site, size_t scope) {
    for (size_t n = o->state.nloans; n > 0; --n) {
        Loan *l = &o->state.loans[n-1];
        if (!l->live) continue;
        for (size_t i = 0; i < o->sema->nentities; ++i) {
            Variable *v = &o->state.variables[i];
            if (l->id != 0 && v->declared && v->scope >= scope && v->output_initialized && v->output.via == l->id) {
                if (!end(o,site,l->id)) return false;
                v->output_initialized = false;
            }
            if (l->id != 0 && v->declared && v->scope >= scope && v->initialized && !v->moved &&
                !v->reference.family && v->reference.via == l->id) {
                if (!end(o,site,l->id)) return false;
                v->reference.via = SIZE_MAX;
            }
        }
        for (StoredField *f = o->state.fields; f; f = f->next)
            if (l->id != 0 && f->active && storage_scope(o,f->storage.root) >= scope &&
                f->value.initialized && !f->value.moved && !f->value.reference.family &&
                f->value.reference.via == l->id) {
                if (!end(o,site,l->id)) return false;
                f->active = false;
            }
    }
    for (size_t n = o->sema->nentities; n > 0; --n) {
        Variable *v = &o->state.variables[n-1];
        if (!v->declared || v->scope < scope) continue;
        if (v->immortal) { v->declared = false; continue; }
        if (v->reference.family && !v->moved) return fail(o,site,"owned allocation leaves scope without release or transfer");
        if (!v->moved && v->initialized && v->reference.via != SIZE_MAX && v->reference.via != 0 &&
            !end(o,site,v->reference.via)) return false;
        if (!end(o,site,v->slot)) return false;
        v->declared = false;
    }
    for (size_t i = 0; i < o->state.nloans; ++i)
        if (o->state.loans[i].live && o->state.loans[i].family && o->state.loans[i].scope >= scope) {
            bool retained = false;
            for (StoredField *f = o->state.fields; f; f = f->next)
                if (f->active && f->value.initialized && !f->value.moved && f->value.reference.family &&
                    f->value.reference.via == o->state.loans[i].id && storage_scope(o,f->storage.root) < scope) retained = true;
            if (!retained) return fail(o,site,"owned allocation leaves scope without release or transfer");
        }
    return true;
}

static bool declaration(Ownership *o, const CeTree *t) {
    unsigned k = kind(o,t);
    if (k == CE_K_StaticAssertionDeclaration) return true;
    if (k != CE_K_Declaration) return fail(o,t,"unsupported declaration in checked ownership body");
    const CeTree *items = t->children[1];
    if (kind(o,items) == CE_K_NoInitDeclarators) return true;
    const CeTree *list = items->children[0];
    for (size_t i = 0; i < list->len; ++i) {
        const CeTree *item = list->children[i], *decl = item->children[0];
        CeEntity *e = entity(o,ce_declarator_name(o->sema,decl));
        if (e && (e->kind == CE_ENTITY_TYPEDEF || e->kind == CE_ENTITY_FUNCTION)) continue;
        bool initialized = kind(o,item) == CE_K_InitDeclaratorValue;
        if (!bind_variable(o,e,item,false)) return false;
        if (initialized) {
            const CeTree *init = item->children[1];
            if (record_spec(o,e->type)) {
                Loan *storage = loan(o,o->state.variables[e->id-1].slot);
                Place destination = {storage->range,storage->id,true,e,-1};
                if (!record_initializer(o,init,e->type,destination)) return false;
                o->state.variables[e->id-1].initialized = true;
            } else if (reference_type(o,e->type)) {
                Reference r;
                View *spec = view(o,e->type);
                if (spec) {
                    if (!view_initializer(o,init,spec,&r)) return false;
                } else if (kind(o,init) != CE_K_InitializerExpression || !expression(o,init->children[0],&r)) return false;
                if (!pointer_binding(o,item,e,r)) return false;
            } else {
                if (!initializer(o,init)) return false;
                o->state.variables[e->id-1].initialized = true;
            }
        }
    }
    return true;
}

static bool field_postconditions(Ownership *o, const CeTree *site) {
    Function *f = o->function;
    for (size_t i = 0; i < f->nposts; ++i) {
        FieldPost *post = &f->posts[i]; FieldPart *part = &f->fields[post->part];
        Reference header = f->originals[post->parameter];
        Range slot = {header.range.root,header.range.first+part->offset,ce_type_size(o->sema,post->field->type)};
        StoredField *cell = field_cell(o,site,post->field,slot);
        if (!cell->value.initialized || cell->value.moved || !cell->value.output_initialized)
            return fail(o,site,"field postcondition is not established on every return path");
        Reference value = cell->value.reference;
        if (o->plan) {
            size_t key = f->nparameters + post->part;
            if (value.null) fprintf(o->plan," (null-field-result %zu)\n",key);
            else {
                fprintf(o->plan," (field-result %zu %zu ",key,value.via); range_plan(o->plan,value.range);
                fprintf(o->plan," %s)\n",post->field->write ? "write" : "read");
            }
        }
    }
    return true;
}

static bool return_statement(Ownership *o, const CeTree *t) {
    Reference r;
    if (!expression(o,t->children[0],&r)) return false;
    Function *f = o->function;
    for (size_t i = 0; i < f->nparameters; ++i)
        if (f->stores[i] >= -2 && f->stores[i] != -1 && !o->state.variables[f->parameters[i]->id-1].output_initialized)
            return fail(o,t,"output lifetime contract must be established on every return path");
    if (reference_type(o,f->entity->type->base)) {
        CeType *result_type = f->entity->type->base;
        Capability *result_cap = pointer(result_type) ? capability(o,result_type->base) : NULL;
        if (!r.null && r.authority && (!result_cap || result_cap->family != r.authority))
            return fail(o,t,"opaque capability return must retain its typed provenance");
        if (r.nullable && !f->result_nullable) return fail(o,t,"nullable value returned under a non-null contract");
        if (r.null) {
            if (!f->result_nullable) return fail(o,t,"null return requires a nullable result contract");
            if (o->plan) fputs(" (null-result)\n",o->plan);
        } else if (f->result_family) {
            if (r.family != f->result_family) return fail(o,t,"owned result lacks matching release authority");
            Loan *l = loan(o,r.via);
            if (!l || l->family != r.family) return fail(o,t,"owned result was already consumed");
            for (size_t i = 0; i < o->state.nloans; ++i)
                if (o->state.loans[i].live && ancestor(o,l->id,&o->state.loans[i]))
                    return fail(o,t,"cannot return an owner while it is borrowed");
            l->live = false;
            if (o->plan) fprintf(o->plan," (give %zu %u)\n",r.via,r.family);
            if (r.owner) { Variable *v = binding_id(o,r.owner); v->moved = true; v->reference.family = 0; }
        } else if (f->result_static) {
            if (r.range.root != 0) return fail(o,t,"returned reference does not have static lifetime");
            if (o->plan) { fprintf(o->plan," (result %zu ",r.via); range_plan(o->plan,r.range); fputs(" read)\n",o->plan); }
        } else if (f->result_from >= 0) {
            Reference allowed = f->allowed_result;
            if (!within(allowed.range,r.range)) return fail(o,t,"returned borrow escapes its declared input subobject");
            if (view(o,f->entity->type->base) && (!r.exact || r.range.first != allowed.range.first ||
                r.range.count != allowed.range.count)) return fail(o,t,"returned view extent differs from its declared input subobject");
            if (!reference_read_only(o,f->entity->type->base) && !access(o,t,r.via,r.range,true)) return false;
            if (o->plan) {
                fprintf(o->plan," (result %zu ",r.via); range_plan(o->plan,r.range);
                fputs(reference_read_only(o,f->entity->type->base) ? " read)\n" : " write)\n",o->plan);
            }
        } else return fail(o,t,"pointer return has no declared lifetime origin");
    }
    if (!field_postconditions(o,t) || !cleanup(o,t,0)) return false;
    if (o->plan) fputs(" (return)\n",o->plan);
    o->state.reachable = false; return true;
}

static bool block(Ownership *o, const CeTree *t) {
    size_t scope = ++o->state.scope;
    const CeTree *items = t->children[0];
    for (size_t i = 0; i < items->len; ++i) {
        if (!o->state.reachable) break;
        const CeTree *item = items->children[i];
        if (kind(o,item) == CE_K_BlockDeclaration) { if (!declaration(o,item->children[0])) return false; }
        else if (kind(o,item) == CE_K_BlockStatement) { if (!statement(o,item->children[0])) return false; }
        else return fail(o,item,"unsupported block item in checked ownership body");
    }
    if (o->state.reachable && !cleanup(o,t,scope)) return false;
    o->state.scope = scope-1; return true;
}

static const CeTree *unwrap(Ownership *o, const CeTree *t) {
    for (;;) {
        unsigned k = kind(o,t);
        if (k == CE_K_CompoundStatement || k == CE_K_SelectionStatement || k == CE_K_IterationStatement ||
            k == CE_K_JumpStatement || k == CE_K_LabeledStatement) t = t->children[0];
        else return t;
    }
}

static bool switch_statement(Ownership *o, const CeTree *t) {
    Reference ignored;
    if (!expression(o,t->children[0],&ignored)) return false;
    const CeTree *body = unwrap(o,t->children[1]);
    if (kind(o,body) != CE_K_Block) return fail(o,t,"checked switch requires a compound body");
    State start = clone(o), joined = start; bool have = false;
    const CeTree *items = body->children[0];
    State *saved_entry = o->switch_entry; size_t saved_scope = o->switch_scope;
    o->switch_entry = &start; o->switch_scope = start.scope; ++o->switches;
    if (o->plan) fputs(" (switch\n",o->plan);
    for (size_t i = 0; i < items->len;) {
        const CeTree *item = items->children[i];
        const CeTree *label = kind(o,item) == CE_K_BlockStatement ? unwrap(o,item->children[0]) : NULL;
        if (kind(o,label) != CE_K_Case && kind(o,label) != CE_K_Default)
            return fail(o,item,"checked switch requires explicit arms without fall-through");
        o->state = start; o->state = clone(o); o->state.next = joined.next;
        ++o->state.scope;
        if (o->plan) fputs(" (arm\n",o->plan);
        if (!statement(o,label->children[kind(o,label) == CE_K_Case ? 1 : 0])) return false;
        ++i;
        while (i < items->len) {
            const CeTree *next = items->children[i];
            const CeTree *st = kind(o,next) == CE_K_BlockStatement ? unwrap(o,next->children[0]) : NULL;
            if (kind(o,st) == CE_K_Case || kind(o,st) == CE_K_Default) break;
            if (o->state.reachable) {
                if (kind(o,next) == CE_K_BlockDeclaration) { if (!declaration(o,next->children[0])) return false; }
                else if (!st || !statement(o,st)) return false;
            }
            ++i;
        }
        if (o->state.reachable && !cleanup(o,t,start.scope+1)) return false;
        /* break is represented as an arm exit, while return remains terminal. */
        o->state.scope = start.scope;
        State arm = clone(o);
        if (!arm.reachable) arm = start;
        if (o->plan) fputs(" )\n",o->plan);
        if (!have) { joined = arm; have = true; }
        else { if (!join(o,t,joined,arm)) return false; joined = clone(o); }
    }
    if (o->plan) fputs(" )\n",o->plan);
    --o->switches; o->switch_entry = saved_entry; o->switch_scope = saved_scope;
    if (!join(o,t,joined,start)) return false;
    return true;
}

static bool statement(Ownership *o, const CeTree *raw) {
    const CeTree *t = unwrap(o,raw); unsigned k = kind(o,t); Reference ignored;
    if (k == CE_K_Block) return block(o,t);
    if (k == CE_K_ExpressionStatement) {
        if (!expression(o,t->children[0],&ignored)) return false;
        return !ignored.family || ignored.owner || fail(o,t,"discarded owned result loses its release authority");
    }
    if (k == CE_K_Return) return return_statement(o,t);
    if (k == CE_K_If || k == CE_K_IfElse) {
        if (!expression(o,t->children[0],&ignored)) return false;
        State start = clone(o);
        if (o->plan) fputs(" (branch (then\n",o->plan);
        refine(o,t->children[0],true);
        if (!statement(o,t->children[1])) return false;
        State left = clone(o); o->state = start;
        if (o->plan) fputs(" ) (else\n",o->plan);
        refine(o,t->children[0],false);
        if (k == CE_K_IfElse && !statement(o,t->children[2])) return false;
        State right = clone(o);
        if (o->plan) fputs(" ))\n",o->plan);
        return join(o,t,left,right);
    }
    if (k == CE_K_While || k == CE_K_For || k == CE_K_ForDeclaration) {
        size_t scope = ++o->state.scope;
        if (k == CE_K_ForDeclaration && !declaration(o,t->children[0])) return false;
        if (k == CE_K_For && !expression(o,t->children[0],&ignored)) return false;
        State entry = clone(o), *saved_entry = o->loop_entry;
        size_t saved_scope = o->loop_scope, saved_switches = o->switches;
        o->loop_entry = &entry; o->loop_scope = scope; o->switches = 0; ++o->loops;
        if (o->plan) fputs(" (loop\n",o->plan);
        const CeTree *condition = t->children[k == CE_K_While ? 0 : 1];
        if (!expression(o,condition,&ignored)) return false;
        refine(o,condition,true);
        if (!statement(o,t->children[k == CE_K_While ? 1 : 3])) return false;
        if (o->state.reachable && k != CE_K_While && !expression(o,t->children[2],&ignored)) return false;
        if (o->state.reachable && !cleanup(o,t,scope+1)) return false;
        if (!join(o,t,entry,clone(o))) return false;
        if (o->plan) fputs(" )\n",o->plan);
        o->loop_entry = saved_entry; o->loop_scope = saved_scope; o->switches = saved_switches; --o->loops;
        if (!cleanup(o,t,scope)) return false;
        o->state.scope = scope-1; return true;
    }
    if (k == CE_K_Continue || k == CE_K_Break) {
        if (k == CE_K_Break && o->switches) {
            if (!cleanup(o,t,o->switch_scope+1) || !join(o,t,*o->switch_entry,clone(o))) return false;
            if (o->plan) fputs(" (arm-exit)\n",o->plan);
            o->state.reachable = false;
            return true;
        }
        if (!o->loop_entry || !cleanup(o,t,o->loop_scope+1)) return fail(o,t,"loop transfer outside a checked loop");
        if (!join(o,t,*o->loop_entry,clone(o))) return false;
        if (o->plan) fputs(k == CE_K_Break ? " (break)\n" : " (continue)\n",o->plan);
        o->state.reachable = false; return true;
    }
    if (k == CE_K_Switch) return switch_statement(o,t);
    return fail(o,t,"unsupported control transfer in checked ownership body (kind %u)",k);
}

static void collect_parameters(Ownership *o, const CeTree *t, Function *f) {
    if (kind(o,t) == CE_K_NamedParameter) {
        CeEntity *e = entity(o,ce_declarator_name(o->sema,t->children[1]));
        if (e && f->nparameters < f->entity->type->nparameters) f->parameters[f->nparameters++] = e;
        return;
    }
    for (size_t i = 0; i < t->len; ++i) collect_parameters(o,t->children[i],f);
}

static void collect_calls(Ownership *o, const CeTree *t, bool *edges) {
    if (kind(o,t) == CE_K_Call) {
        CeEntity *callee = named_variable(o,t->children[0]);
        Function *f = callee && callee->kind == CE_ENTITY_FUNCTION ? function_named(o,callee->name) : NULL;
        if (f) edges[(size_t)(f-o->functions)] = true;
    }
    for (size_t i = 0; i < t->len; ++i) collect_calls(o,t->children[i],edges);
}

static bool global_effects(Ownership *o) {
    bool any = false;
    for (size_t i = 0; i < o->sema->nentities; ++i) any |= o->global_owners[i].len != 0;
    if (!any) return true;
    size_t n = o->nfunctions;
    if (n && n > SIZE_MAX/n) return fail(o,NULL,"global effect graph is too large");
    bool *edges = ce_alloc(o->arena,n*n*sizeof(*edges));
    memset(edges,0,n*n*sizeof(*edges));
    for (size_t i = 0; i < n; ++i) {
        edges[i*n+i] = true;
        if (o->functions[i].body) collect_calls(o,o->functions[i].body,edges+i*n);
    }
    for (size_t k = 0; k < n; ++k)
        for (size_t i = 0; i < n; ++i) if (edges[i*n+k])
            for (size_t j = 0; j < n; ++j) edges[i*n+j] |= edges[k*n+j];
    for (size_t i = 0; i < n; ++i) {
        Function *f = &o->functions[i];
        f->global_effects = ce_alloc(o->arena,o->sema->nentities*sizeof(*f->global_effects));
        memset(f->global_effects,0,o->sema->nentities*sizeof(*f->global_effects));
        for (size_t j = 0; j < o->sema->nentities; ++j) if (o->global_owners[j].len) {
            Function *owner = function_named(o,o->global_owners[j]);
            f->global_effects[j] = edges[i*n+(size_t)(owner-o->functions)];
        }
    }
    return true;
}

static bool signature(Ownership *o, Function *f, const CeAtom *spec) {
    f->spec = spec;
    for (size_t i = 2; i < spec->len; ++i) {
        const CeAtom *part = spec->items[i];
        if (head(part,"obligation") && part->len == 2 && part->items[1]->kind == CE_ATOM_TEXT && part->items[1]->text.len) {
            if (f->obligation.len) return malformed(o,part,"duplicate foreign obligation");
            f->obligation = part->items[1]->text;
        } else if (head(part,"variadic-read") && part->len == 1) {
            if (f->variadic_read || !f->entity->type->variadic)
                return malformed(o,part,"variadic-read requires one variadic function contract");
            f->variadic_read = true;
        }
        else if (head(part,"returns-static") && (part->len == 1 || (part->len == 2 && symbol(part->items[1],"nullable")))) {
            if (!pointer(f->entity->type->base) || f->result_static || f->result_from >= 0 || f->result_family)
                return malformed(o,part,"duplicate, conflicting or non-pointer result contract");
            f->result_static = true; f->result_nullable = part->len == 2;
        } else if (head(part,"returns-own") && (part->len == 2 ||
            (part->len == 3 && symbol(part->items[2],"nullable"))) && part->items[1]->kind == CE_ATOM_SYMBOL) {
            if (!pointer(f->entity->type->base) || f->result_static || f->result_from >= 0 || f->result_family)
                return malformed(o,part,"duplicate, conflicting or non-pointer result contract");
            f->result_family = family(o,part->items[1]->text);
            f->result_nullable = part->len == 3;
        } else if (head(part,"owns") && part->len == 3) {
            size_t index;
            if (!natural(part->items[1],&index) || index >= f->entity->type->nparameters ||
                !reference_type(o,f->entity->type->parameters[index]) || part->items[2]->kind != CE_ATOM_SYMBOL)
                return malformed(o,part,"owns requires a pointer parameter index and allocator family");
            if (f->families[index]) return malformed(o,part,"duplicate owner parameter contract");
            f->modes[index] = 2; f->families[index] = family(o,part->items[2]->text);
            View *v = view(o,f->entity->type->parameters[index]);
            if (v && v->family != f->families[index]) return malformed(o,part,"owned view parameter has a different allocation family");
        } else if (head(part,"returns-borrow") && part->len >= 2) {
            if (!reference_type(o,f->entity->type->base) || f->result_static || f->result_from >= 0 || f->result_family)
                return malformed(o,part,"duplicate, conflicting or non-pointer result contract");
            size_t index;
            if (!natural(part->items[1],&index) || index >= f->entity->type->nparameters ||
                !pointer(f->entity->type->parameters[index])) return malformed(o,part,"invalid borrowed-result parameter");
            f->result_from = (int)index;
            size_t n = part->len-2;
            if (n && symbol(part->items[part->len-1],"nullable")) { f->result_nullable = true; --n; }
            CeAtom *path = ce_alloc(o->arena,sizeof(*path));
            *path = (CeAtom){.kind=CE_ATOM_LIST,.items=part->items+2,.len=n}; f->result_path = path;
        } else if (head(part,"stores") && part->len == 3) {
            size_t index, source;
            if (!natural(part->items[1],&index) || index >= f->entity->type->nparameters ||
                !pointer(f->entity->type->parameters[index]) || !pointer(f->entity->type->parameters[index]->base))
                return malformed(o,part,"stores requires a pointer-to-pointer parameter");
            if (f->stores[index] != -1) return malformed(o,part,"duplicate or conflicting pointer lifetime effect");
            if (symbol(part->items[2],"static")) f->stores[index] = -2;
            else if (natural(part->items[2],&source) && source < f->entity->type->nparameters && pointer(f->entity->type->parameters[source]))
                f->stores[index] = (int)source;
            else return malformed(o,part,"stores requires static or a pointer input index");
        } else if (head(part,"preserves") && part->len == 2) {
            size_t index;
            if (!natural(part->items[1],&index) || index >= f->entity->type->nparameters ||
                !pointer(f->entity->type->parameters[index]) || !pointer(f->entity->type->parameters[index]->base))
                return malformed(o,part,"preserves requires a pointer-to-pointer parameter index");
            if (f->stores[index] != -1) return malformed(o,part,"duplicate or conflicting pointer lifetime effect");
            f->stores[index] = -3;
        } else if (head(part,"nullable") && part->len == 2) {
            size_t index;
            if (!natural(part->items[1],&index) || index >= f->entity->type->nparameters ||
                !pointer(f->entity->type->parameters[index]) || f->nullable[index])
                return malformed(o,part,"nullable requires a distinct pointer parameter index");
            f->nullable[index] = true;
        } else if ((head(part,"stores-field") && part->len == 4) || (head(part,"takes-field") && part->len == 3)) {
            size_t index, source = 0;
            if (!natural(part->items[1],&index) || index >= f->entity->type->nparameters ||
                !pointer(f->entity->type->parameters[index]) || read_only(f->entity->type->parameters[index]) ||
                part->items[2]->kind != CE_ATOM_SYMBOL)
                return malformed(o,part,"field effect requires an exclusive record pointer parameter and field name");
            FieldSpec *field = field_spec(o,f->entity->type->parameters[index]->base,part->items[2]->text);
            if (!field) return malformed(o,part,"field effect has no declared pointer field");
            if (head(part,"takes-field")) {
                if (!field->family) return malformed(o,part,"takes-field requires an owning field");
                for (size_t i = 0; i < f->ntakes; ++i) if (f->takes[i].parameter == index && f->takes[i].field == field)
                    return malformed(o,part,"duplicate field transfer contract");
                FieldTake *items = ce_alloc(o->arena,(f->ntakes+1)*sizeof(*items));
                if (f->ntakes) memcpy(items,f->takes,f->ntakes*sizeof(*items));
                items[f->ntakes++] = (FieldTake){index,field}; f->takes = items;
            } else {
                bool is_static = symbol(part->items[3],"static");
                if (field->family || (!is_static && (!natural(part->items[3],&source) ||
                    source >= f->entity->type->nparameters || !pointer(f->entity->type->parameters[source]))))
                    return malformed(o,part,"stores-field requires a borrowed field and static or a pointer input index");
                for (size_t i = 0; i < f->nposts; ++i) if (f->posts[i].parameter == index && f->posts[i].field == field)
                    return malformed(o,part,"duplicate field postcondition");
                FieldPost *items = ce_alloc(o->arena,(f->nposts+1)*sizeof(*items));
                if (f->nposts) memcpy(items,f->posts,f->nposts*sizeof(*items));
                items[f->nposts++] = (FieldPost){.parameter=index,.field=field,.source=is_static ? -2 : (int)source}; f->posts = items;
            }
        } else return malformed(o,part,"unknown or malformed function ownership contract");
    }
    if (f->foreign && !f->obligation.len) return malformed(o,spec,"foreign function requires a named obligation");
    if (f->foreign && f->body) return malformed(o,spec,"a defined function must be checked rather than declared foreign");
    if (reference_type(o,f->entity->type->base) && !f->result_static && f->result_from < 0 && !f->result_family)
        return malformed(o,spec,"pointer return requires a lifetime origin");
    if (view(o,f->entity->type->base) && f->result_nullable)
        return malformed(o,spec,"view results carry an exact extent; optional values require a variant contract");
    return true;
}

static bool record_parts(Ownership *o, Function *f, size_t parameter, CeType *type,
                         unsigned long long offset, bool header_writing) {
    RecordSpec *spec = record_spec(o,type);
    if (!spec) return !reference_record(type,0) || fail(o,f->body,"nested reference record needs its field declaration");
    for (size_t i = 0; i < type->tag->record->nmembers; ++i) {
        CeMember *member = &type->tag->record->members[i];
        FieldSpec *field = field_spec(o,type,member->name);
        if (field) {
            FieldPart *parts = ce_alloc(o->arena,(f->nfields+1)*sizeof(*parts));
            if (f->nfields) memcpy(parts,f->fields,f->nfields*sizeof(*parts));
            bool owning = field->family && (f->families[parameter] || !pointer(f->entity->type->parameters[parameter]));
            for (size_t j = 0; j < f->ntakes; ++j) if (f->takes[j].parameter == parameter && f->takes[j].field == field) owning = true;
            parts[f->nfields++] = (FieldPart){parameter,field,offset+member->offset,0,
                owning || (header_writing && field->write),owning};
            f->fields = parts;
        } else if (ce_type_record(member->type) && reference_record(member->type,0)) {
            if (!record_parts(o,f,parameter,member->type,offset+member->offset,header_writing)) return false;
        } else if (reference_record(member->type,0)) return fail(o,f->body,"reference array fields need element contracts");
    }
    return true;
}

static bool function_body(Ownership *o, Function *f) {
    o->function = f;
    if (record_spec(o,f->entity->type->base))
        return fail(o,f->body,"reference-record results require field result contracts");
    o->state = (State){.next=1,.reachable=true};
    o->state.variables = ce_alloc(o->arena,o->sema->nentities*sizeof(*o->state.variables));
    memset(o->state.variables,0,o->sema->nentities*sizeof(*o->state.variables));
    append_loan(o,(Loan){0,SIZE_MAX,{0,0,ULLONG_MAX},false,true,0,0});
    for (size_t i = 0; i < o->sema->nentities; ++i) {
        CeEntity *e = o->sema->entities[i];
        if (e->kind == CE_ENTITY_OBJECT && e->scope == CE_SCOPE_FILE && object_read_only(e->type) &&
            !reference_record(e->type,0))
            o->state.variables[e->id-1] = (Variable){.declared=true,.initialized=true,.immortal=true,.slot=0,
                .reference={.null=true,.via=SIZE_MAX}};
    }
    for (size_t i = 0; f->global_effects && i < o->sema->nentities; ++i) if (f->global_effects[i]) {
        CeEntity *e = o->sema->entities[i]; Variable *v = &o->state.variables[e->id-1];
        *v = (Variable){.declared=true,.initialized=true,.reference={.null=true,.via=SIZE_MAX}};
        v->slot = root(o,ce_type_size(o->sema,e->type),true,"input",0);
        f->global_roots[i] = v->slot;
    }
    for (size_t i = 0; i < f->nparameters; ++i) {
        CeEntity *e = f->parameters[i]; Variable *v = &o->state.variables[e->id-1];
        *v = (Variable){.declared=true,.initialized=true,.parameter=true};
        v->reference.null = true; v->reference.via = SIZE_MAX;
        v->slot = root(o,ce_type_size(o->sema,e->type),!(e->type->qualifiers & CE_Q_CONST),"local",0);
        if (pointer(e->type)) {
            unsigned long long size = ce_type_size(o->sema,e->type->base);
            if (!size) return fail(o,e->declaration,"pointer parameter requires a complete pointee or a byte view");
            size_t id = root(o,size,!read_only(e->type),f->families[i] ? "heap" : "input",f->families[i]);
            f->input_roots[i] = id;
            v->reference = (Reference){{id,0,size},id,true,false,f->nullable[i],f->families[i],f->families[i] ? e->id : 0,0};
        } else if (view(o,e->type)) return fail(o,e->declaration,"native view parameters require a dependent extent contract; checked byte bodies provide it");
        else if (!record_spec(o,e->type) && reference_record(e->type,0)) return fail(o,e->declaration,"value parameter contains undeclared reference fields");
    }
    for (size_t i = 0; i < f->nfields; ++i) {
        FieldPart *part = &f->fields[i]; CeEntity *parameter = f->parameters[part->parameter];
        Variable *v = &o->state.variables[parameter->id-1];
        Range storage = pointer(parameter->type) ? v->reference.range : loan(o,v->slot)->range;
        storage.first += part->offset; storage.count = ce_type_size(o->sema,part->field->type);
        StoredField *cell = field_cell(o,f->body,part->field,storage);
        if (part->field->is_static) cell->value.reference = static_reference(part->field->nullable);
        else {
            unsigned long long size = ce_type_size(o->sema,part->field->type->base);
            if (!size) return fail(o,f->body,"record field parameter needs a complete target type");
            size_t id = root(o,size,part->writing,part->owning ? "heap" : "input",part->owning ? part->field->family : 0);
            part->input = id;
            cell->value.reference = (Reference){{id,0,size},id,true,false,part->field->nullable,
                part->owning ? part->field->family : 0,part->owning ? cell->entity.id : 0,0};
        }
        cell->value.initialized = true; cell->value.parameter = !part->owning;
    }
    f->originals = ce_alloc(o->arena,(f->nparameters ? f->nparameters : 1)*sizeof(*f->originals));
    for (size_t i = 0; i < f->nparameters; ++i) f->originals[i] = o->state.variables[f->parameters[i]->id-1].reference;
    for (size_t i = 0; i < f->nposts; ++i)
        f->posts[i].allowed = f->posts[i].source == -2 ? static_reference(true) : f->originals[f->posts[i].source];
    if (f->result_from >= 0) {
        f->allowed_result = o->state.variables[f->parameters[f->result_from]->id-1].reference;
        if (!narrow_result(o,f->body,&f->allowed_result,f->entity->type->parameters[f->result_from],f->result_path)) return false;
    }
    if (!block(o,f->body)) return false;
    if (o->state.reachable) {
        if (!ce_type_void(f->entity->type->base)) return fail(o,f->body,"value function may reach its end without a result");
        for (size_t i = 0; i < f->nparameters; ++i)
            if (f->stores[i] >= -2 && f->stores[i] != -1 && !o->state.variables[f->parameters[i]->id-1].output_initialized)
                return fail(o,f->body,"output lifetime contract must be established on every fall-through path");
        if (!field_postconditions(o,f->body) || !cleanup(o,f->body,0)) return false;
    }
    return true;
}

static bool result_description(Ownership *o, Function *f, size_t *input, Range *relative) {
    CeType *type = f->entity->type->parameters[f->result_from]->base;
    *input = (size_t)f->result_from;
    *relative = (Range){0,0,ce_type_size(o->sema,type)};
    if (!f->result_path) return true;
    bool followed_pointer = false;
    for (size_t i = 0; i < f->result_path->len; ++i) {
        CeText name = f->result_path->items[i]->text;
        if (!ce_type_record(type) || !type->tag || !type->tag->record)
            return fail(o,f->body,"result path is not a record member path");
        CeMember *member = NULL;
        for (size_t j = 0; j < type->tag->record->nmembers; ++j)
            if (same(type->tag->record->members[j].name,name)) member = &type->tag->record->members[j];
        if (!member) return fail(o,f->body,"result path member not found");
        relative->first += member->offset;
        type = member->type;
        if (pointer(type)) {
            if (followed_pointer) return fail(o,f->body,"result paths through multiple pointer fields require a path contract");
            followed_pointer = true;
            bool found = false;
            for (size_t j = 0; j < f->nfields; ++j)
                if (f->fields[j].parameter == (size_t)f->result_from && f->fields[j].offset == relative->first) {
                    *input = f->entity->type->nparameters + j; found = true; break;
                }
            if (!found) return fail(o,f->body,"returned pointer field has no input contract");
            relative->first = 0; type = type->base;
        }
        relative->count = ce_type_size(o->sema,type);
    }
    return true;
}

static bool write_signature(Ownership *o, FILE *destination, Function *f) {
    CeType *ft = f->entity->type;
    fputs(" (signature (parameters",destination);
    for (size_t i = 0; i < ft->nparameters; ++i) {
        CeType *type = ft->parameters[i];
        unsigned long long count = pointer(type) ? ce_type_size(o->sema,type->base) : 0;
        if (i == 0 && f->release_family) fprintf(destination," (released %u)",f->release_family);
        else if (f->families[i]) fprintf(destination," (%s %llu %u)",f->nullable[i] ? "optional-owned" : "owned",count,f->families[i]);
        else if (view(o,type)) fprintf(destination," (viewed %s)",reference_read_only(o,type) ? "read" : "write");
        else if (!pointer(type)) fputs(" (value)",destination);
        else fprintf(destination," (%s %llu %s)",f->nullable[i] ? "optional-borrowed" : "borrowed",count,read_only(type) ? "read" : "write");
    }
    for (size_t i = 0; i < f->nfields; ++i) {
        FieldPart *part = &f->fields[i];
        if (part->field->is_static) fprintf(destination," (static %s)",part->field->nullable ? "nullable" : "nonnull");
        else if (part->owning) fprintf(destination," (%s %llu %u)",part->field->nullable ? "optional-owned" : "owned",
            ce_type_size(o->sema,part->field->type->base),part->field->family);
        else fprintf(destination," (%s %llu %s)",part->field->nullable ? "optional-borrowed" : "borrowed",
            ce_type_size(o->sema,part->field->type->base),part->writing ? "write" : "read");
    }
    for (size_t i = 0; f->global_effects && i < o->sema->nentities; ++i)
        if (f->global_effects[i]) fprintf(destination," (borrowed %llu write)",ce_type_size(o->sema,o->sema->entities[i]->type));
    fprintf(destination,") (variadic %s) (result ",f->variadic_read ? "read" : "none");
    if (!reference_type(o,ft->base)) fputs("value",destination);
    else if (f->result_family) fprintf(destination,"owned %u %s",f->result_family,f->result_nullable ? "nullable" : "nonnull");
    else if (f->result_static) fprintf(destination,"static %s",f->result_nullable ? "nullable" : "nonnull");
    else if (f->result_from >= 0) {
        size_t input; Range result;
        if (!result_description(o,f,&input,&result)) return false;
        if (view(o,ft->base)) fprintf(destination,"viewed %zu %llu %llu %s",input,result.first,result.count,
            reference_read_only(o,ft->base) ? "read" : "write");
        else fprintf(destination,"borrowed %zu %llu %llu %s %s",input,result.first,result.count,
            reference_read_only(o,ft->base) ? "read" : "write",f->result_nullable ? "nullable" : "nonnull");
    } else return fail(o,f->body,"pointer return requires an admitted lifetime contract");
    fputs(") (fields",destination);
    for (size_t i = 0; i < f->nposts; ++i) {
        FieldPost *post = &f->posts[i];
        fprintf(destination," (field %zu (",f->entity->type->nparameters+post->part);
        if (post->source == -2) fprintf(destination,"static %s",post->field->nullable ? "nullable" : "nonnull");
        else fprintf(destination,"borrowed %d 0 %llu %s %s",post->source,
            ce_type_size(o->sema,f->entity->type->parameters[post->source]->base),
            post->field->write ? "write" : "read",post->field->nullable ? "nullable" : "nonnull");
        fputs("))",destination);
    }
    fputs("))",destination);
    return true;
}

static bool check_function(Ownership *o, Function *f) {
    FILE *destination = o->plan;
    char *buffer = NULL; size_t length = 0;
    if (destination && !(o->plan = open_memstream(&buffer,&length))) return fail(o,f->body,"cannot allocate ownership plan");
    bool ok = function_body(o,f);
    if (destination) {
        if (fclose(o->plan)) ok = false;
        o->plan = destination;
        if (ok) {
            fprintf(destination," (function %.*s",(int)f->name.len,f->name.data);
            if (!write_signature(o,destination,f)) ok = false;
            fputs(" (inputs",destination);
            for (size_t i = 0; i < f->nparameters; ++i) fprintf(destination," %zu",f->input_roots[i]);
            for (size_t i = 0; i < f->nfields; ++i) fprintf(destination," %zu",f->fields[i].input);
            for (size_t i = 0; i < o->sema->nentities; ++i) if (f->global_roots[i])
                fprintf(destination," %zu",f->global_roots[i]);
            fputs(") (ambient) (returns ",destination);
            if (!reference_type(o,f->entity->type->base)) fputs("value",destination);
            else if (f->result_family) fprintf(destination,"own %u %s",f->result_family,f->result_nullable ? "nullable" : "nonnull");
            else if (f->result_static) fprintf(destination,"static %s",f->result_nullable ? "nullable" : "nonnull");
            else if (view(o,f->entity->type->base)) {
                fputs("view ",destination); range_plan(destination,f->allowed_result.range);
                fprintf(destination," %s",reference_read_only(o,f->entity->type->base) ? "read" : "write");
            }
            else {
                fputs("borrow ",destination); range_plan(destination,f->allowed_result.range);
                fprintf(destination," %s %s",reference_read_only(o,f->entity->type->base) ? "read" : "write",f->result_nullable ? "nullable" : "nonnull");
            }
            fputs(") (body\n",destination);
            if (fwrite(buffer,1,length,destination) != length) ok = false;
            fputs(" ))\n",destination);
        }
        free(buffer);
    }
    return ok;
}

static bool check_pragmas(Ownership *o, const CeTree *t) {
    if (t->production == -1 && t->spelling.len >= 8 && !memcmp(t->spelling.data,"#pragma ",8)) {
        CeText rest = {t->spelling.data+8,t->spelling.len-8};
        while (rest.len && (rest.data[0] == ' ' || rest.data[0] == '\t')) { ++rest.data; --rest.len; }
        const char *accepted[] = {"GCC diagnostic ","clang diagnostic ","GCC visibility ","STDC "};
        bool known = false;
        for (size_t i = 0; i < sizeof(accepted)/sizeof(*accepted); ++i) {
            size_t n = strlen(accepted[i]);
            if (rest.len >= n && !memcmp(rest.data,accepted[i],n)) known = true;
        }
        if (!known) return fail(o,t,"pragma has no admitted layout or ownership semantics");
    }
    for (size_t i = 0; i < t->len; ++i) if (!check_pragmas(o,t->children[i])) return false;
    return true;
}

static bool check(Ownership *o, const CeTree *tree) {
    if (!check_pragmas(o,tree)) return false;
    if (kind(o,tree) == CE_K_GNUEmptyTranslationUnit) return malformed(o,o->declaration,"ownership declaration checked no function bodies");
    o->functions = ce_alloc(o->arena,o->sema->nentities*sizeof(*o->functions));
    o->global_owners = ce_alloc(o->arena,o->sema->nentities*sizeof(*o->global_owners));
    memset(o->global_owners,0,o->sema->nentities*sizeof(*o->global_owners));
    o->capabilities = ce_alloc(o->arena,o->declaration->len*sizeof(*o->capabilities));
    for (size_t i = 1; i < o->declaration->len; ++i) {
        const CeAtom *spec = o->declaration->items[i];
        if (!head(spec,"capability")) continue;
        if (spec->len != 3 || spec->items[1]->kind != CE_ATOM_SYMBOL || spec->items[2]->kind != CE_ATOM_SYMBOL)
            return malformed(o,spec,"capability requires a structure typedef and an authority family");
        CeType *type = NULL;
        for (size_t j = 0; j < o->sema->nentities; ++j) {
            CeEntity *e = o->sema->entities[j];
            if (e->kind == CE_ENTITY_TYPEDEF && e->scope == CE_SCOPE_FILE && same(e->name,spec->items[1]->text)) type = e->type;
        }
        if (!type || type->kind != CE_T_STRUCT || !type->tag || !type->tag->record ||
            !type->tag->record->complete || type->tag->record->flexible || capability(o,type))
            return malformed(o,spec,"capability requires a distinct complete structure typedef");
        unsigned brand = family(o,spec->items[2]->text);
        if (capability_family(o,brand)) return malformed(o,spec,"distinct capability types require distinct authority families");
        o->capabilities[o->ncapabilities++] = (Capability){type,brand};
    }
    o->records = ce_alloc(o->arena,o->declaration->len*sizeof(*o->records));
    for (size_t i = 1; i < o->declaration->len; ++i) {
        const CeAtom *spec = o->declaration->items[i];
        if (!head(spec,"record")) continue;
        if (spec->len < 2 || spec->items[1]->kind != CE_ATOM_SYMBOL)
            return malformed(o,spec,"record requires a structure typedef and field contracts");
        CeType *type = NULL;
        for (size_t j = 0; j < o->sema->nentities; ++j) {
            CeEntity *e = o->sema->entities[j];
            if (e->kind == CE_ENTITY_TYPEDEF && e->scope == CE_SCOPE_FILE && same(e->name,spec->items[1]->text)) type = e->type;
        }
        if (!type || type->kind != CE_T_STRUCT || !type->tag || !type->tag->record ||
            !type->tag->record->complete || type->tag->record->flexible || record_spec(o,type) || capability(o,type))
            return malformed(o,spec,"record requires a distinct complete structure typedef");
        RecordSpec *r = &o->records[o->nrecords++];
        *r = (RecordSpec){.type=type,.len=spec->len-2,.fields=ce_alloc(o->arena,(spec->len-2)*sizeof(FieldSpec))};
        for (size_t j = 0; j < r->len; ++j) {
            const CeAtom *part = spec->items[j+2];
            if (!head(part,"field") || part->len != 3 || part->items[1]->kind != CE_ATOM_SYMBOL)
                return malformed(o,part,"expected (field NAME (borrow MODE | owns FAMILY | static))");
            FieldSpec *f = &r->fields[j]; *f = (FieldSpec){.name=part->items[1]->text};
            for (size_t k = 0; k < type->tag->record->nmembers; ++k) {
                CeMember *m = &type->tag->record->members[k];
                if (same(m->name,f->name)) f->type = m->type;
            }
            if (!pointer(f->type)) return malformed(o,part,"field contract must name a pointer member");
            for (size_t k = 0; k < j; ++k) if (same(f->name,r->fields[k].name))
                return malformed(o,part,"duplicate reference field contract");
            const CeAtom *mode = part->items[2];
            if (head(mode,"static") && (mode->len == 1 || (mode->len == 2 && symbol(mode->items[1],"nullable")))) {
                if (!read_only(f->type)) return malformed(o,mode,"static field must point to const storage");
                f->is_static = true; f->nullable = mode->len == 2;
            } else if (head(mode,"borrow") && (mode->len == 2 || (mode->len == 3 && symbol(mode->items[2],"nullable"))) &&
                (symbol(mode->items[1],"read") || symbol(mode->items[1],"write"))) {
                f->write = symbol(mode->items[1],"write"); f->nullable = mode->len == 3;
                if (f->write && read_only(f->type)) return malformed(o,mode,"exclusive field cannot point to const storage");
            } else if (head(mode,"owns") && (mode->len == 2 || (mode->len == 3 && symbol(mode->items[2],"nullable"))) &&
                mode->items[1]->kind == CE_ATOM_SYMBOL) {
                f->family = family(o,mode->items[1]->text); f->write = true; f->nullable = mode->len == 3;
            } else return malformed(o,mode,"unknown field lifetime or permission contract");
            Capability *opaque = capability(o,f->type->base);
            if (opaque && f->family && opaque->family != f->family)
                return malformed(o,mode,"owning capability field has the wrong authority family");
        }
        for (size_t k = 0; k < type->tag->record->nmembers; ++k) {
            CeMember *m = &type->tag->record->members[k];
            if (pointer(m->type) && !field_spec(o,type,m->name))
                return malformed(o,spec,"every pointer field requires a lifetime contract");
        }
    }
    o->views = ce_alloc(o->arena,o->declaration->len*sizeof(*o->views));
    for (size_t i = 1; i < o->declaration->len; ++i) {
        const CeAtom *spec = o->declaration->items[i];
        if (!head(spec,"view")) continue;
        if ((spec->len != 5 && spec->len != 6) || spec->items[1]->kind != CE_ATOM_SYMBOL ||
            spec->items[2]->kind != CE_ATOM_SYMBOL || spec->items[3]->kind != CE_ATOM_SYMBOL ||
            (!symbol(spec->items[4],"read") && !symbol(spec->items[4],"write")))
            return malformed(o,spec,"view requires a type, data field, length field and read/write mode");
        CeType *type = NULL;
        for (size_t j = 0; j < o->sema->nentities; ++j) {
            CeEntity *e = o->sema->entities[j];
            if (e->kind == CE_ENTITY_TYPEDEF && same(e->name,spec->items[1]->text)) type = e->type;
        }
        if (!type || type->kind != CE_T_STRUCT || !type->tag || !type->tag->record ||
            type->tag->record->nmembers != 2 || type->tag->record->flexible || view(o,type) || record_spec(o,type) || capability(o,type))
            return malformed(o,spec,"view requires a distinct, complete two-field structure typedef");
        View *v = &o->views[o->nviews]; *v = (View){.type=type,.write=symbol(spec->items[4],"write")};
        if (spec->len == 6) {
            const CeAtom *owned = spec->items[5];
            if (!v->write || !head(owned,"owns") || owned->len != 2 || owned->items[1]->kind != CE_ATOM_SYMBOL)
                return malformed(o,spec,"owning view requires write mode and an allocation family");
            v->family = family(o,owned->items[1]->text);
        }
        for (size_t j = 0; j < 2; ++j) {
            CeMember *m = &type->tag->record->members[j];
            if (m->bitfield || m->anonymous) return malformed(o,spec,"view fields must be ordinary named fields");
            if (same(m->name,spec->items[2]->text)) v->data = m;
            if (same(m->name,spec->items[3]->text)) v->length = m;
        }
        if (!v->data || !v->length || v->data == v->length || !pointer(v->data->type) ||
            ce_type_size(o->sema,v->data->type->base) != 1 ||
            v->write == read_only(v->data->type) || !ce_type_integer(v->length->type) ||
            ce_kind_signed(o->sema->abi,ce_type_integer_kind(v->length->type)) ||
            ce_type_size(o->sema,v->length->type) != o->sema->abi->size[o->sema->abi->size_type])
            return malformed(o,spec,"view needs a byte pointer of the declared mode and an unsigned size field");
        ++o->nviews;
    }
    for (size_t i = 0; i < o->sema->nentities; ++i) {
        CeEntity *e = o->sema->entities[i]; if (e->kind != CE_ENTITY_FUNCTION) continue;
        Function *f = &o->functions[o->nfunctions++];
        *f = (Function){.name=e->name,.entity=e,.result_from=-1,.allocation_count=-1};
        size_t n = e->type->nparameters;
        f->parameters = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->parameters));
        f->stores = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->stores));
        f->modes = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->modes));
        f->families = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->families));
        f->nullable = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->nullable));
        memset(f->nullable,0,(n ? n : 1)*sizeof(*f->nullable));
        f->input_roots = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->input_roots));
        memset(f->input_roots,0,(n ? n : 1)*sizeof(*f->input_roots));
        f->global_roots = ce_alloc(o->arena,o->sema->nentities*sizeof(*f->global_roots));
        memset(f->global_roots,0,o->sema->nentities*sizeof(*f->global_roots));
        for (size_t j = 0; j < n; ++j) { f->stores[j] = -1; f->modes[j] = pointer(e->type->parameters[j]) && !read_only(e->type->parameters[j]); f->families[j] = 0; }
    }
    const CeTree *items = tree->children[0];
    for (size_t i = 0; i < items->len; ++i) if (kind(o,items->children[i]) == CE_K_ExternalFunction) {
        const CeTree *def = items->children[i]->children[0];
        CeEntity *e = entity(o,ce_declarator_name(o->sema,def->children[1]));
        Function *f = e ? function_named(o,e->name) : NULL;
        if (!f) return fail(o,def,"function definition has no bound signature");
        f->body = def->children[3];
        f->checked = true;
        collect_parameters(o,def->children[1],f);
        if (f->nparameters != f->entity->type->nparameters) return fail(o,def,"checked functions require named prototype parameters");
    }
    bool all = false;
    for (size_t i = 1; i < o->declaration->len; ++i) {
        const CeAtom *spec = o->declaration->items[i];
        if (head(spec,"view") || head(spec,"record") || head(spec,"capability")) continue;
        if (head(spec,"check-all") && spec->len == 1) { if (all) return malformed(o,spec,"duplicate check-all"); all = true; continue; }
        if (head(spec,"global") && spec->len == 3 && spec->items[1]->kind == CE_ATOM_SYMBOL && spec->items[2]->kind == CE_ATOM_SYMBOL) {
            CeEntity *global = NULL; Function *owner = function_named(o,spec->items[2]->text);
            for (size_t j = 0; j < o->sema->nentities; ++j) {
                CeEntity *e = o->sema->entities[j];
                if (e->kind == CE_ENTITY_OBJECT && e->scope == CE_SCOPE_FILE && same(e->name,spec->items[1]->text)) global = e;
            }
            if (!global || global->linkage != CE_LINKAGE_INTERNAL || !owner || !owner->body)
                return malformed(o,spec,"global ownership requires a private object and a checked owner function");
            if (pointer(global->type) || reference_record(global->type,0))
                return malformed(o,spec,"reference-bearing globals require field lifetime contracts");
            if (o->global_owners[global->id-1].len) return malformed(o,spec,"global object has more than one declared owner");
            o->global_owners[global->id-1] = owner->name;
            continue;
        }
        if (head(spec,"allocator") && spec->len == 5 && spec->items[1]->kind == CE_ATOM_SYMBOL &&
            spec->items[2]->kind == CE_ATOM_SYMBOL && spec->items[3]->kind == CE_ATOM_SYMBOL &&
            head(spec->items[4],"obligation") && spec->items[4]->len == 2 &&
            spec->items[4]->items[1]->kind == CE_ATOM_TEXT && spec->items[4]->items[1]->text.len) {
            Function *alloc = function_named(o,spec->items[2]->text), *free = function_named(o,spec->items[3]->text);
            if (!alloc || !free || alloc == free || alloc->spec || free->spec || alloc->body || free->body ||
                alloc->entity->type->nparameters != 1 || free->entity->type->nparameters != 1 ||
                !pointer(alloc->entity->type->base) || !pointer(free->entity->type->parameters[0]))
                return malformed(o,spec,"allocator requires distinct, declared allocation and release interfaces");
            unsigned brand = family(o,spec->items[1]->text);
            if (capability_family(o,brand)) return malformed(o,spec,"capability families require typed issuer contracts, not memory allocators");
            alloc->spec = free->spec = spec; alloc->foreign = free->foreign = true;
            alloc->obligation = free->obligation = spec->items[4]->items[1]->text;
            alloc->result_family = free->release_family = brand;
            alloc->allocation_count = 0; alloc->result_nullable = true;
            continue;
        }
        if ((!head(spec,"function") && !head(spec,"foreign")) || spec->len < 2 || spec->items[1]->kind != CE_ATOM_SYMBOL)
            return malformed(o,spec,"expected check-all, function or foreign declaration");
        Function *f = function_named(o,spec->items[1]->text);
        if (!f) return malformed(o,spec,"function contract has no C declaration");
        if (f->spec) return malformed(o,spec,"duplicate function ownership contract");
        f->foreign = head(spec,"foreign");
        if (!f->foreign) f->checked = true;
        if (!signature(o,f,spec)) return false;
    }
    if (!all) return malformed(o,o->declaration,"ownership currently requires check-all; no unmarked body is silently trusted");
    for (size_t i = 0; i < o->nfunctions; ++i) {
        Function *f = &o->functions[i];
        CeType *result = f->entity->type->base;
        if (contains_capability(o,result,0)) return fail(o,f->body,"opaque capability cannot be returned by value");
        Capability *returned = pointer(result) ? capability(o,result->base) : NULL;
        if (f->result_family && capability_family(o,f->result_family) != returned)
            return malformed(o,f->spec,"capability authority can be issued only by its matching typed result");
        if (returned && f->result_family && returned->family != f->result_family)
            return malformed(o,f->spec,"capability result has the wrong authority family");
        for (size_t j = 0; j < f->entity->type->nparameters; ++j) {
            CeType *type = f->entity->type->parameters[j];
            if (contains_capability(o,type,0)) return fail(o,f->body,"opaque capability cannot be passed by value");
            Capability *argument = pointer(type) ? capability(o,type->base) : NULL;
            if (argument && f->modes[j] == 2 && f->families[j] != argument->family)
                return malformed(o,f->spec,"capability parameter has the wrong authority family");
            CeType *object = pointer(type) ? type->base : type;
            if (record_spec(o,object) && !record_parts(o,f,j,object,0,!pointer(type) || !read_only(type))) return false;
        }
        for (size_t j = 0; j < f->nposts; ++j) {
            bool found = false;
            for (size_t k = 0; k < f->nfields; ++k)
                if (f->fields[k].parameter == f->posts[j].parameter && f->fields[k].field == f->posts[j].field) {
                    f->posts[j].part = k; found = true; break;
                }
            if (!found) return fail(o,f->body,"field postcondition is absent from the parameter interface");
        }
    }
    if (!global_effects(o)) return false;
    if (o->plan) fputs("(Ownership \"cesta-ownership/2\"\n",o->plan);
    if (o->plan) for (size_t i = 0; i < o->nfunctions; ++i) if (o->functions[i].foreign) {
        Function *f = &o->functions[i];
        fprintf(o->plan," (foreign %.*s",(int)f->name.len,f->name.data);
        if (!write_signature(o,o->plan,f)) return false;
        fputs(" (obligation ",o->plan);
        CeAtom text = {.kind=CE_ATOM_TEXT,.text=f->obligation}; ce_atom_write(&text,o->plan);
        fputs("))\n",o->plan);
    }
    size_t checked = 0;
    for (size_t i = 0; i < o->nfunctions; ++i) if (o->functions[i].checked) {
        Function *f = &o->functions[i];
        if (!f->body) return malformed(o,f->spec,"checked function has no body");
        if (!check_function(o,f)) return false;
        ++checked;
    }
    if (!checked) return malformed(o,o->declaration,"ownership declaration checked no function bodies");
    if (o->plan) fputs(")\n",o->plan);
    return true;
}

bool ce_ownership_check(CeArena *arena, const CeAtom *declaration, CeText text,
                        const char *identity, size_t argc, char **arguments,
                        FILE *plan, CeError *error) {
    if (!identity) return ce_error(error,declaration->offset,"ownership requires a source identity and compiler environment");
    CePPResult pp = {0}; CeGrammar grammar = {0}; CeTokens tokens = {0};
    CeDFA dfa = {0}; CeLR lr = {0}; CeForest forest = {.root=-1}; CeSema sema = {0};
    bool sema_ready = false, ok = false; CeTree *tree;
    char **args = ce_alloc(arena,(argc+5)*sizeof(*args)); size_t n = 0;
    args[n++] = "--cc"; args[n++] = arguments[0];
    for (size_t i = 1; i < argc; ++i) args[n++] = arguments[i];
    args[n++] = "-x"; args[n++] = "c"; args[n++] = (char *)identity;
    if (!ce_pp_run_source(ce_builtin_CPPGNUGrammar(),(int)n,args,text,false,&pp,error)) goto done;
    if (!ce_builtin_dialect(&grammar,true,pp.clang,error) || !ce_dfa_prepare(&grammar,&dfa,error) ||
        !ce_lr_prepare(&grammar,grammar.entries[0],&lr,error) || !ce_dfa_lex(&grammar,&dfa,pp.text,&tokens,error) ||
        !ce_lr_parse(&grammar,&lr,&tokens,&forest,error) || !ce_tree_from_forest(&grammar,&forest,&tokens,arena,&tree,error) ||
        !ce_sema_init(&sema,&grammar,pp.text,error)) goto done;
    sema_ready = true; sema.clang = pp.clang;
    if (!ce_bind(&sema,tree,error) || !ce_elaborate(&sema,tree,error)) goto done;
    Ownership o = {.arena=arena,.sema=&sema,.pp=&pp,.declaration=declaration,.identity=identity,.error=error,.plan=plan};
    ok = check(&o,tree);
done:
    if (!ok && error->offset != declaration->offset) {
        const CePPOrigin *origin = ce_pp_origin(&pp,error->offset);
        char message[sizeof(error->message)]; memcpy(message,error->message,sizeof(message));
        if (origin) ce_error(error,declaration->offset,"ownership C analysis at %s:%u: %s",origin->file,origin->line,message);
        else error->offset = declaration->offset;
    }
    if (sema_ready) ce_sema_free(&sema);
    ce_forest_free(&forest); ce_tokens_free(&tokens); ce_lr_free(&lr); ce_dfa_free(&dfa);
    ce_grammar_free(&grammar); ce_pp_result_free(&pp); return ok;
}

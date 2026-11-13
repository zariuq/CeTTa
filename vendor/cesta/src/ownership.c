#define _POSIX_C_SOURCE 200809L
#include "ownership.h"
#include "ownership_signature.h"
#include "sema.h"
#include "pp.h"
#include "digest.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

CeText ce_builtin_CPPGNUGrammar(void);

typedef struct { size_t root; unsigned long long first, count; } Range;
typedef struct Function Function;
typedef struct Callback Callback;
typedef struct { CeType *type; CeMember *data, *length; bool write; unsigned family; unsigned long long stride; } View;
typedef struct { size_t id, parent; Range range; bool write, live; unsigned family; size_t scope; } Loan;
typedef struct {
    Range range; size_t via; bool exact, null, nullable;
    unsigned family; size_t owner;
    unsigned authority;
    Function *target;
    Callback *callback;
} Reference;
typedef struct {
    bool declared, initialized, moved, parameter, immortal;
    size_t slot, scope;
    Reference reference;
    Reference output; bool output_initialized;
} Variable;
typedef struct { CeText name; CeType *type; bool write, is_static, nullable; unsigned family; Callback *callback; } FieldSpec;
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
typedef struct {
    size_t parameter; FieldSpec *field; int source; size_t part; Reference allowed;
    unsigned long long offset;
    const CeAtom *source_path;
} FieldPost;
typedef struct { size_t parameter; FieldSpec *field; } FieldTake;
struct Function {
    CeText name; const CeAtom *spec;
    CeEntity *entity; const CeTree *body;
    CeEntity **parameters; size_t nparameters;
    bool checked, foreign, unchecked, variadic_read;
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
    FieldPost *results; size_t nresults;
    bool *record_posts;
    FieldTake *takes; size_t ntakes;
    Reference *originals;
    int result_input;
    Callback **callbacks;
    /* The first place a checked body calls or names the function. */
    const CeTree *use;
    bool interface_callable;
    const CeAtom *portable_signature;
};
struct Callback {
    CeText name; CeType *type; const CeAtom *spec; Function **targets; size_t len;
    CeEntity *anchor;
    Function *prototype;
    Function **implementations;
    size_t nimplementations;
};
typedef struct {
    Variable *variables;
    StoredField *fields;
    Loan *loans; size_t nloans, capacity;
    size_t next, scope;
    bool reachable;
} State;
/* Contracts kept beside a header. NAME.h.ownership applies to every unit that
   reads NAME.h; its items follow the unit's own in the declaration. */
typedef struct { const char *name, *path; CeText text; const CeAtom *root; } Interface;
typedef struct {
    CeArena *arena; CeSema *sema; const CePPResult *pp;
    const CeAtom *declaration; const char *identity; CeError *error;
    Interface *interfaces; size_t ninterfaces, unit_items;
    bool contract_error;
    Function *functions; size_t nfunctions;
    Callback *callbacks; size_t ncallbacks;
    Function *function; State state;
    CeText *global_owners;
    CeText *families; size_t nfamilies;
    View *views; size_t nviews;
    RecordSpec *records; size_t nrecords;
    Capability *capabilities; size_t ncapabilities;
    CeText expected_abi;
    FieldIdentity *field_ids; size_t next_field;
    FILE *plan; size_t loops, loop_scope;
    State *loop_entry, *loop_exits, *switch_entry; size_t switches, switch_scope;
    const CeTree *loop_step;
    State *loop_latch;
    bool *switch_exit;
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
/* A position as the author reads it: the line #line directives give the
   unit's authored text, and the invocation of a macro, not its definition. */
static const char *origin_file(const CePPOrigin *origin) {
    return origin->presumed_file && origin->presumed_file[0] ? origin->presumed_file : origin->file;
}
static unsigned origin_line(const CePPOrigin *origin) {
    return origin->presumed_file && origin->presumed_file[0] ? origin->presumed_line : origin->line;
}
static bool fail(Ownership *o, const CeTree *site, const char *format, ...) {
    char message[640]; va_list args;
    va_start(args,format); vsnprintf(message,sizeof(message),format,args); va_end(args);
    const CePPOrigin *origin = site ? ce_pp_origin(o->pp,site->offset) : NULL;
    return ce_error(o->error,o->declaration->offset,"ownership%s%.*s%s%s%s%u: %s",
        o->function ? " in " : "",o->function ? (int)o->function->name.len : 0,
        o->function ? o->function->name.data : (const unsigned char *)"",
        origin ? " at " : "",origin ? origin_file(origin) : "",origin ? ":" : "",
        origin ? origin_line(origin) : 0,message);
}
static bool atom_within(const CeAtom *root, const CeAtom *site) {
    if (root == site) return true;
    if (root->kind == CE_ATOM_LIST) for (size_t i = 0; i < root->len; ++i)
        if (atom_within(root->items[i],site)) return true;
    return false;
}
static unsigned text_line(CeText text, size_t offset) {
    unsigned line = 1;
    for (size_t i = 0; i < offset && i < text.len; ++i) line += text.data[i] == '\n';
    return line;
}
/* A contract error is placed at its own item: in the unit, or at the line of
   the interface file that supplied it. */
static bool malformed(Ownership *o, const CeAtom *site, const char *message) {
    o->contract_error = true;
    for (size_t i = 0; i < o->ninterfaces; ++i) if (atom_within(o->interfaces[i].root,site))
        return ce_error(o->error,o->declaration->offset,"ownership: %s (%s:%u)",message,
                        o->interfaces[i].name,text_line(o->interfaces[i].text,site->offset));
    return ce_error(o->error,site->offset,"ownership: %s",message);
}
static bool imported(const Ownership *o, size_t item) { return item >= o->unit_items; }
static bool pointer(const CeType *type) { return type && type->kind == CE_T_POINTER; }
static bool object_read_only(const CeType *type) {
    return type->kind == CE_T_ARRAY ? object_read_only(type->base) : (type->qualifiers & CE_Q_CONST) != 0;
}
static bool read_only(const CeType *type) { return pointer(type) &&
    (type->base->kind == CE_T_FUNCTION || object_read_only(type->base)); }
static View *view(Ownership *o, const CeType *type) {
    if (!ce_type_record(type)) return NULL;
    for (size_t i = 0; i < o->nviews; ++i) if (o->views[i].type->tag == type->tag) return &o->views[i];
    return NULL;
}
static bool reference_type(Ownership *o, const CeType *type) { return pointer(type) || view(o,type); }
static bool reference_read_only(Ownership *o, const CeType *type) {
    View *v = view(o,type); return v ? !v->write : read_only(type);
}
static bool view_origin_compatible(Ownership *o, const CeType *target, const CeType *source) {
    View *to = view(o,target), *from = view(o,source);
    return to && from && !from->family && (!to->write || from->write) &&
        to->stride == from->stride && ce_type_similar(to->data->type->base,from->data->type->base);
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
static bool reference_contracts(Ownership *o, const CeType *type, unsigned depth) {
    if (!type || depth > 64) return false;
    if (!reference_record(type,0)) return true;
    if (type->kind == CE_T_ARRAY)
        return type->length >= 0 && reference_contracts(o,type->base,depth+1);
    return record_spec(o,type) != NULL;
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
static Callback *callback_named(Ownership *o, CeText name) {
    for (size_t i = 0; i < o->ncallbacks; ++i) if (same(o->callbacks[i].name,name)) return &o->callbacks[i];
    return NULL;
}
static const CeAtom *function_signature(Ownership *, Function *);

static Function *callback_prototype(const Callback *callback) {
    return callback->prototype ? callback->prototype : callback->len ? callback->targets[0] : NULL;
}

static bool callback_implementation(Ownership *o, Callback *contract, Function *source) {
    /* Copying a value of this same interface is not new implementation evidence. */
    if (source == contract->prototype) return true;
    if (!source || (!source->checked && !source->foreign) || source->unchecked ||
        !ce_type_compatible(o->sema,contract->type->base,source->entity->type)) return false;
    if (source->global_effects) for (size_t i = 0; i < o->sema->nentities; ++i)
        if (source->global_effects[i]) return false;
    const CeAtom *provided = function_signature(o,source);
    const CeAtom *expected = function_signature(o,contract->prototype);
    if (!provided || !expected || !ce_ownership_signature_refines(provided,expected)) return false;
    Callback *record = contract;
    for (size_t i = 0; i < record->nimplementations; ++i)
        if (record->implementations[i] == source) return true;
    if (!record->implementations) record->implementations = ce_alloc(o->arena,o->nfunctions*sizeof(*record->implementations));
    record->implementations[record->nimplementations++] = source;
    return true;
}

static bool callback_accepts(Ownership *o, Callback *contract, Reference source) {
    if (source.null) return true;
    if (contract->prototype) {
        if (source.target) return callback_implementation(o,contract,source.target);
        if (!source.callback) return false;
        if (source.callback->prototype)
            return callback_implementation(o,contract,source.callback->prototype);
        for (size_t i = 0; i < source.callback->len; ++i)
            if (!callback_implementation(o,contract,source.callback->targets[i])) return false;
        return source.callback->len != 0;
    }
    if (source.target) {
        for (size_t i = 0; i < contract->len; ++i) if (contract->targets[i] == source.target) return true;
        return false;
    }
    if (!source.callback) return false;
    if (source.callback->prototype) return false;
    for (size_t i = 0; i < source.callback->len; ++i) {
        bool found = false;
        for (size_t j = 0; j < contract->len; ++j) found |= contract->targets[j] == source.callback->targets[i];
        if (!found) return false;
    }
    return true;
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
/* A unit is an entire dynamic span, not a byte. The emitted whole-root event
 * has an arbitrary-extent interpretation; numeric pointer operations below
 * must never consume this unit as a concrete C pointee. */
static size_t whole_root(Ownership *o, bool write) {
    size_t id = o->state.next++;
    append_loan(o,(Loan){id,SIZE_MAX,{id,0,1},write,true,0,o->state.scope});
    if (o->plan) fprintf(o->plan," (whole-root %zu %s)\n",id,write ? "write" : "read");
    return id;
}
static bool whole_range(Ownership *o, Range range) {
    Function *f = o->function;
    if (!f || !range.root) return false;
    for (size_t i = 0; i < f->nparameters; ++i)
        if (view(o,f->parameters[i]->type) && f->input_roots[i] == range.root) return true;
    for (size_t i = 0; i < f->nfields; ++i)
        if (view(o,f->fields[i].field->type) && f->fields[i].input == range.root) return true;
    return false;
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
static State clone_from(Ownership *o, State state) {
    State saved = o->state;
    o->state = state;
    State copy = clone(o);
    o->state = saved;
    return copy;
}
static bool same_reference(Reference a, Reference b) {
    return a.via == b.via && a.null == b.null && a.exact == b.exact &&
        a.family == b.family && a.authority == b.authority && a.range.root == b.range.root &&
        a.range.first == b.range.first && a.range.count == b.range.count &&
        a.target == b.target && a.callback == b.callback;
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
            if (x->output_initialized) x->output.nullable |= y->output.nullable;
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
static bool loop_nullability(Ownership *o, const CeTree *site, State entry, State backedge) {
    if (!backedge.reachable) return true;
    for (size_t i = 0; i < o->sema->nentities; ++i) {
        Variable *before = &entry.variables[i], *after = &backedge.variables[i];
        if (!before->declared || !after->declared) continue;
        if ((before->initialized && after->initialized && !before->reference.nullable && after->reference.nullable) ||
            (before->output_initialized && after->output_initialized && !before->output.nullable && after->output.nullable))
            return fail(o,site,"loop backedge weakens its non-null invariant; establish a nullable entry and recheck it on each iteration");
    }
    for (StoredField *before = entry.fields; before; before = before->next) if (before->active) {
        for (StoredField *after = backedge.fields; after; after = after->next)
            if (after->active && after->entity.id == before->entity.id &&
                before->value.initialized && after->value.initialized &&
                !before->value.reference.nullable && after->value.reference.nullable)
                return fail(o,site,"loop backedge weakens a reference field's non-null invariant");
    }
    return true;
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
static bool call_with_record(Ownership *, const CeTree *, Reference *, const Place *);

static bool view_component(Ownership *o, const CeTree *t) {
    unsigned k = kind(o,t);
    if (k != CE_K_Member && k != CE_K_PointerMember) return false;
    const CeExpr *base = typed(o,t->children[0]);
    return base && view(o,k == CE_K_PointerMember && pointer(base->type) ? base->type->base : base->type);
}

static Reference static_reference(bool nullable) {
    return (Reference){{0,0,ULLONG_MAX},0,false,false,nullable,0,0,0,NULL,NULL};
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
static bool flat_static_record(Ownership *o, CeType *type) {
    if (!ce_type_record(type) || type->kind == CE_T_UNION || !record_spec(o,type)) return false;
    for (size_t i = 0; i < type->tag->record->nmembers; ++i) {
        CeMember *m = &type->tag->record->members[i];
        if (m->bitfield || (m->type->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC))) return false;
        if (pointer(m->type)) {
            FieldSpec *spec = field_spec(o,type,m->name);
            if (!spec || !spec->is_static || capability(o,m->type->base)) return false;
        } else if (reference_record(m->type,0)) return false;
    }
    return true;
}
/* Unknown row operations preserve the pointwise static contract. Their
 * footprints cover every possible row; equal container ranges do not mean
 * that the selected source and destination indices are equal. */
static bool static_field_summary(Ownership *o, const CeTree *site, Reference header,
                                 CeType *record, FieldSpec *spec, Reference *out) {
    unsigned long long stride = ce_type_size(o->sema,record), offset = 0;
    if (!flat_static_record(o,record) || !stride || !header.range.count || header.range.count % stride)
        return fail(o,site,"unknown row needs a complete flat static-reference array");
    bool member = false;
    for (size_t i = 0; i < record->tag->record->nmembers; ++i)
        if (same(record->tag->record->members[i].name,spec->name)) {offset = record->tag->record->members[i].offset; member = true;}
    if (!member) return fail(o,site,"static row field has no layout");
    bool nullable = false;
    for (unsigned long long i = 0; i < header.range.count/stride; ++i) {
        Range slot = {header.range.root,header.range.first+i*stride+offset,ce_type_size(o->sema,spec->type)};
        if (!within(header.range,slot)) return fail(o,site,"static row field exceeds its container");
        Variable *value = &field_cell(o,site,spec,slot)->value;
        if (!value->initialized || value->moved || value->reference.family || value->reference.authority ||
            (!value->reference.null && value->reference.range.root != 0) ||
            (spec->callback && !callback_accepts(o,spec->callback,value->reference)))
            return fail(o,site,"unknown row lacks its pointwise static lifetime contract");
        nullable |= value->reference.null || value->reference.nullable;
    }
    *out = static_reference(nullable); out->callback = spec->callback; return true;
}
static bool static_field_update(Ownership *o, const CeTree *site, Reference header,
                                CeType *record, FieldSpec *spec, Reference source) {
    Reference previous;
    if (!static_field_summary(o,site,header,record,spec,&previous)) return false;
    if ((!source.null && (source.range.root != 0 || source.family || source.authority)) ||
        (!spec->nullable && (source.null || source.nullable)) ||
        (spec->callback && !callback_accepts(o,spec->callback,source)))
        return fail(o,site,"unknown row store violates its static field contract");
    unsigned long long stride = ce_type_size(o->sema,record), offset = 0;
    for (size_t i = 0; i < record->tag->record->nmembers; ++i)
        if (same(record->tag->record->members[i].name,spec->name)) offset = record->tag->record->members[i].offset;
    for (unsigned long long i = 0; i < header.range.count/stride; ++i) {
        Range slot = {header.range.root,header.range.first+i*stride+offset,ce_type_size(o->sema,spec->type)};
        Variable *value = &field_cell(o,site,spec,slot)->value;
        value->reference = static_reference(value->reference.nullable || source.null || source.nullable);
        value->reference.callback = spec->callback;
    }
    return true;
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
        if (spec->callback && !callback_accepts(o,spec->callback,source))
            return fail(o,site,"callback field requires an admitted implementation");
        if (spec->callback && !source.null) { source.callback = spec->callback; source.target = NULL; }
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
            if (view(o,e->type) && (!source.exact || source.range.first != post->allowed.range.first ||
                source.range.count != post->allowed.range.count))
                return fail(o,site,"stored view must preserve the complete declared input extent");
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
    if (!stored && source.null && !view(o,e->type) && write) {
        v->reference = source; v->initialized = true; v->moved = false; return true;
    }
    if (source.null) {
        /* A null kept in a callback field is still a value under that contract. */
        source = static_reference(true);
        if (stored) source.callback = stored->spec->callback;
    }
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
    if (!path || !path->len || !r->exact) return true;
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
            if (reference_type(o,type)) {
                Variable *value = after.variable ? binding_id(o,after.variable->id) : NULL;
                if (!value || !value->initialized || value->moved)
                    return fail(o,site,"borrow result refers to an unavailable record field");
                *r = value->reference;
                if (view(o,type)) {
                    if (i+1 != path->len) return fail(o,site,"borrow path must retain a view as one value");
                } else type = type->base;
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
    else if (index < ft->nparameters && f->callbacks[index])
        fprintf(o->plan," (%s 0)",reference.nullable ? "maybe" : "loan");
    else fprintf(o->plan," (%s %zu)",reference.nullable ? "maybe" : "loan",id);
}

static bool store_output(Ownership *o, const CeTree *site, size_t parameter, Range destination,
                         CeType *target, Reference r) {
    Function *f = o->function;
    if (parameter >= f->nparameters || f->stores[parameter] < -2 || f->stores[parameter] == -1 ||
        !within(f->originals[parameter].range,destination))
        return fail(o,site,"pointer store has no declared lifetime destination");
    int source = f->stores[parameter];
    if (!r.null && source == -2 && r.range.root != 0) return fail(o,site,"output would retain a non-static borrow");
    if (!r.null && source >= 0 && !within(f->originals[source].range,r.range))
        return fail(o,site,"output borrow has the wrong allocation origin");
    Variable *v = &o->state.variables[f->parameters[parameter]->id-1];
    if (v->output_initialized && v->output.via != 0 && v->output.via != SIZE_MAX && !end(o,site,v->output.via)) return false;
    if (r.null) r = static_reference(true);
    if (r.via != 0) {
        size_t id;
        if (!borrow(o,site,r,!read_only(target),&id)) return false;
        r.via = id; r.family = 0; r.owner = 0;
    }
    v->output = r; v->output_initialized = true;
    return true;
}

static bool call_target(Ownership *, const CeTree *, Reference *, const Place *, Function *, size_t);
static bool call_with_record(Ownership *o, const CeTree *t, Reference *out, const Place *result_place) {
    CeEntity *callee = named_variable(o,t->children[0]);
    Function *f = callee && callee->kind == CE_ENTITY_FUNCTION ? function_named(o,callee->name) : NULL;
    if (f && !f->use) f->use = t;
    size_t callee_guard = SIZE_MAX;
    if (!f) {
        Reference callable;
        if (!expression(o,t->children[0],&callable)) return false;
        if (callable.null || callable.nullable) return fail(o,t,"callback invocation requires a non-null callable");
        f = callable.target ? callable.target : callable.callback ? callback_prototype(callable.callback) : NULL;
        if (callable.callback) for (size_t i = 0; i < callable.callback->len; ++i)
            if (!callable.callback->targets[i]->use) callable.callback->targets[i]->use = t;
        const CeExpr *callee_type = typed(o,t->children[0]);
        if (f && (!callee_type || !pointer(callee_type->type) ||
            !ce_type_compatible(o->sema,callee_type->type->base,f->entity->type)))
            return fail(o,t,"callback invocation cannot change its function type");
        if (f && f->global_effects) for (size_t i = 0; i < o->sema->nentities; ++i)
            if (f->global_effects[i]) return fail(o,t,"callback effects must use explicit context, not owned globals");
        Place slot;
        if (!place(o,t->children[0],&slot)) return false;
        Reference storage = {slot.range,slot.via,slot.exact,false,false,0,0,0,NULL,NULL};
        if (!borrow(o,t,storage,false,&callee_guard)) return false;
    }
    if (f && !f->use) f->use = t;
    if (!f || (!f->checked && !f->foreign)) return fail(o,t,"call requires a checked body or an explicit foreign obligation");
    return call_target(o,t,out,result_place,f,callee_guard);
}

static bool call_target(Ownership *o, const CeTree *t, Reference *out,
                        const Place *result_place, Function *f, size_t callee_guard) {
    const CeTree *args = t->children[1];
    const CeTree *list = kind(o,args) == CE_K_Arguments ? args->children[0] : NULL;
    size_t count = list ? list->len : 0;
    CeType *ft = f->entity->type;
    if (record_spec(o,ft->base) && (!f->nresults || !result_place))
        return fail(o,t,"reference-record call needs a returns-record contract and a receiving record");
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
    if (callee_guard != SIZE_MAX && !end(o,t,callee_guard)) return false;
    for (size_t i = 0; i < count; ++i) {
        loans[i] = SIZE_MAX;
        const CeTree *arg = list->children[i]; const CeExpr *at = typed(o,arg);
        CeType *object_type = pointer(at->type) ? at->type->base : at->type;
        Capability *opaque = capability(o,object_type);
        if (!opaque) opaque = capability_family(o,refs[i].authority);
        CeType *parameter = i < ft->nparameters ? ft->parameters[i] : NULL;
        if (i < ft->nparameters && f->callbacks[i] && !callback_accepts(o,f->callbacks[i],refs[i]))
            return fail(o,arg,"callback argument requires an admitted implementation");
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
            if (refs[i].null || refs[i].nullable)
                return fail(o,arg,"record field arguments require a precise non-null record location");
            if (!refs[i].exact) {
                if (f->result_from >= 0) return fail(o,arg,"unknown-row calls with borrowed results need an element-origin contract");
                for (size_t k = 0; k < f->nposts; ++k) if (f->posts[k].parameter == i)
                    return fail(o,arg,"unknown-row relational postconditions need an element contract");
                if (!part->field->is_static || !pointer(ft->parameters[i]))
                    return fail(o,arg,"unknown row arguments require flat static-reference contracts");
                if (!static_field_summary(o,arg,refs[i],ft->parameters[i]->base,part->field,&field_refs[j])) return false;
                continue;
            }
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
        if (whole_range(o,refs[i].range) && (i >= ft->nparameters || !view(o,ft->parameters[i])))
            return fail(o,arg,"whole dynamic spans require a view parameter; a fixed pointee cannot establish their bounds");
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
        if (i < ft->nparameters && f->callbacks[i]) {
            if (!checked_reference(o,arg,refs[i],!f->nullable[i])) return false;
            continue;
        }
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
        if (part->field->callback && !callback_accepts(o,part->field->callback,r))
            return fail(o,t,"callback field requires an admitted implementation");
        if (whole_range(o,r.range) && !view(o,part->field->type))
            return fail(o,t,"whole dynamic spans cannot satisfy fixed-pointee record field contracts");
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
            Reference r = {l->range,l->id,true,false,false,0,0,0,NULL,NULL};
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
        if (!header.exact) {
            Reference updated = static_reference(part->field->nullable); updated.callback = part->field->callback;
            if (!static_field_update(o,t,header,ft->parameters[part->parameter]->base,part->field,
                                     updated)) return false;
            continue;
        }
        Range slot = {header.range.root,header.range.first+part->offset,ce_type_size(o->sema,part->field->type)};
        StoredField *cell = field_cell(o,t,part->field,slot);
        cell->value.reference = static_reference(part->field->nullable);
        cell->value.reference.callback = part->field->callback;
    }
    for (size_t i = 0; i < f->nposts; ++i) {
        FieldPost *post = &f->posts[i]; FieldPart *part = &f->fields[post->part];
        Reference header = refs[post->parameter];
        Range slot = {header.range.root,header.range.first+part->offset,ce_type_size(o->sema,post->field->type)};
        StoredField *cell = field_cell(o,t,post->field,slot);
        Reference value = post->source == -2 ? static_reference(post->field->nullable) :
            (post->source < (int)ft->nparameters ? refs[post->source] : field_refs[post->source-ft->nparameters]);
        value.family = 0; value.owner = 0;
        if (!view(o,post->field->type)) value.exact = false;
        value.nullable = post->field->nullable;
        if (post->source == -2) value.callback = post->field->callback;
        if (!pointer_binding(o,t,&cell->entity,value)) return false;
    }
    for (size_t i = 0; i < ft->nparameters; ++i) if (f->stores && f->stores[i] >= -2 && f->stores[i] != -1) {
        const CeTree *arg = list->children[i];
        Reference r = f->stores[i] == -2 ? static_reference(true) : refs[f->stores[i]];
        if (kind(o,arg) == CE_K_AddressOf) {
            Place target;
            if (!place(o,arg->children[0],&target) || !target.variable || !pointer(target.variable->type))
                return fail(o,arg,"output reference requires a local pointer slot");
            if (!pointer_binding(o,arg,target.variable,r)) return false;
        } else {
            CeEntity *e = named_variable(o,arg); size_t parameter = SIZE_MAX;
            for (size_t j = 0; j < o->function->nparameters; ++j)
                if (e == o->function->parameters[j]) parameter = j;
            if (parameter == SIZE_MAX || !pointer(e->type) || !pointer(e->type->base) ||
                !store_output(o,arg,parameter,refs[i].range,e->type->base,r))
                return fail(o,arg,"output reference requires a local pointer slot or a contracted output parameter");
        }
    }
    *out = (Reference){.null=true,.via=SIZE_MAX};
    if (result_place) {
        if (!f->nresults || !result_place->exact) return fail(o,t,"record result requires a precise destination");
        for (size_t i = 0; i < f->nresults; ++i) {
            FieldPost *post = &f->results[i];
            Range slot = {result_place->range.root,result_place->range.first+post->offset,
                          ce_type_size(o->sema,post->field->type)};
            if (!within(result_place->range,slot)) return fail(o,t,"record result field exceeds its destination");
            StoredField *cell = field_cell(o,t,post->field,slot);
            Reference value;
            if (post->source == -3) {
                unsigned long long extent = ce_type_size(o->sema,post->field->type->base);
                if (!extent) return fail(o,t,"owned record result needs a complete field target type");
                size_t id = root(o,extent,true,"heap",post->field->family);
                value = (Reference){{id,0,extent},id,true,false,post->field->nullable,post->field->family,0,0,NULL,NULL};
            } else {
                value = post->source == -2 ? static_reference(post->field->nullable) :
                    (post->source < (int)ft->nparameters ? refs[post->source] : field_refs[post->source-ft->nparameters]);
                value.family = 0; value.owner = 0;
                if (!view(o,post->field->type)) value.exact = false;
            }
            value.nullable = post->field->nullable;
            if (post->source == -2) value.callback = post->field->callback;
            if (!pointer_binding(o,t,&cell->entity,value)) return false;
        }
    }
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
            *out = (Reference){{id,0,count},id,true,false,f->result_nullable,f->result_family,0,0,NULL,NULL};
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

static bool call(Ownership *o, const CeTree *t, Reference *out) {
    return call_with_record(o,t,out,NULL);
}

static void refine(Ownership *o, const CeTree *condition, bool truth) {
    unsigned k = kind(o,condition);
    if (k == CE_K_SomeFullExpression || k == CE_K_SomeExpression) {
        refine(o,condition->children[0],truth); return;
    }
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

/* The guard is evaluated anew each iteration. A pointer tested by that guard
 * may enter the invariant as nullable and be refined again on the true edge. */
static void loop_guard_entry(Ownership *o, const CeTree *condition) {
    unsigned k = kind(o,condition);
    if (k == CE_K_SomeFullExpression || k == CE_K_SomeExpression || k == CE_K_Not) {
        loop_guard_entry(o,condition->children[0]); return;
    }
    if (k == CE_K_And || k == CE_K_Or) {
        loop_guard_entry(o,condition->children[0]); loop_guard_entry(o,condition->children[1]); return;
    }
    const CeTree *candidate = condition;
    if (k == CE_K_Equal || k == CE_K_NotEqual) {
        const CeExpr *right = typed(o,condition->children[1]);
        const CeExpr *left = typed(o,condition->children[0]);
        if (right && right->null_pointer) candidate = condition->children[0];
        else if (left && left->null_pointer) candidate = condition->children[1];
        else return;
    }
    CeEntity *e = named_variable(o,candidate);
    if (e && pointer(e->type)) {
        Variable *v = &o->state.variables[e->id-1];
        if (v->declared && v->initialized && !v->moved) v->reference.nullable = true;
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
    if ((!destination.exact || !source.exact) && !flat_static_record(o,type))
        return fail(o,site,"unknown record copy requires a flat static-reference row; owning/shared origins need a precise element");
    if (!access(o,site,source.via,source.range,false)) return false;
    if (!access(o,site,destination.via,destination.range,true)) return false;
    if (type->kind == CE_T_ARRAY) {
        unsigned long long stride = ce_type_size(o->sema,type->base);
        if (type->length < 0 || !stride) return fail(o,site,"reference array needs a complete constant extent");
        for (unsigned long long i = 0; i < (unsigned long long)type->length; ++i) {
            Place to = destination, from = source;
            to.range.first += i*stride; to.range.count = stride;
            from.range.first += i*stride; from.range.count = stride;
            to.variable = from.variable = NULL;
            if (!record_copy(o,site,type->base,to,from)) return false;
        }
        return true;
    }
    CeRecord *record = type->tag->record;
    for (size_t i = 0; i < record->nmembers; ++i) {
        CeMember *m = &record->members[i]; Place to, from;
        if (!field(o,site,destination,type,m->name,&to) || !field(o,site,source,type,m->name,&from)) return false;
        if (reference_type(o,m->type)) {
            FieldSpec *spec = field_spec(o,type,m->name);
            Reference saved;
            if (!source.exact) {
                Reference header = {source.range,source.via,false,false,false,0,0,0,NULL,NULL};
                if (!static_field_summary(o,site,header,type,spec,&saved)) return false;
            } else {
                Variable *value = from.variable ? binding_id(o,from.variable->id) : NULL;
                if (!value || !value->initialized || value->moved)
                    return fail(o,site,"record copy reads an uninitialized or moved reference field");
                saved = value->reference;
            }
            if (!destination.exact) {
                Reference header = {destination.range,destination.via,false,false,false,0,0,0,NULL,NULL};
                if (!static_field_update(o,site,header,type,spec,saved)) return false;
            } else if (!pointer_binding(o,site,to.variable,saved)) return false;
        } else if (ce_type_record(m->type) && reference_record(m->type,0)) {
            if (!record_spec(o,m->type) || !record_copy(o,site,m->type,to,from)) return false;
        } else if (m->type->kind == CE_T_ARRAY && reference_record(m->type,0)) {
            if (!reference_contracts(o,m->type,0) || !record_copy(o,site,m->type,to,from)) return false;
        }
    }
    return true;
}

static bool record_initializer(Ownership *o, const CeTree *t, CeType *type, Place destination) {
    if (!destination.exact) return fail(o,t,"record initialization requires a precise storage location");
    unsigned k = kind(o,t);
    if (k == CE_K_Call) {
        Reference ignored;
        return call_with_record(o,t,&ignored,&destination);
    }
    if (type->kind == CE_T_ARRAY) {
        if (type->length < 0 || !ce_type_size(o->sema,type->base))
            return fail(o,t,"reference array needs a complete constant extent");
        if (t && k != CE_K_GNUEmptyInitializer && k != CE_K_InitializerList && k != CE_K_InitializerListComma)
            return fail(o,t,"reference array initialization needs element initializers");
        const CeTree *elements = t && k != CE_K_GNUEmptyInitializer ? t->children[0] : NULL;
        bool zero = false;
        if (elements && elements->len == 1 && kind(o,elements->children[0]) == CE_K_InitializerElement) {
            const CeTree *value = elements->children[0]->children[0];
            const CeExpr *literal = kind(o,value) == CE_K_InitializerExpression ? typed(o,value->children[0]) : NULL;
            zero = literal && kind(o,value->children[0]) == CE_K_IntConst && literal->constant && literal->value == 0;
        }
        if (elements && elements->len > (size_t)type->length) return fail(o,t,"too many reference-array initializers");
        for (size_t i = 0; i < (size_t)type->length; ++i) {
            const CeTree *value = NULL;
            if (!zero && elements && i < elements->len) {
                if (kind(o,elements->children[i]) != CE_K_InitializerElement)
                    return fail(o,t,"reference arrays require positional element initializers");
                value = elements->children[i]->children[0];
            }
            Place target = destination;
            target.range.first += i*ce_type_size(o->sema,type->base);
            target.range.count = ce_type_size(o->sema,type->base); target.variable = NULL;
            if (!record_initializer(o,value,type->base,target)) return false;
        }
        return true;
    }
    if (k == CE_K_InitializerExpression) {
        const CeTree *expression_tree = t->children[0];
        unsigned ek = kind(o,expression_tree);
        if (ek == CE_K_Call || ek == CE_K_CompoundLiteral || ek == CE_K_CompoundLiteralComma || ek == CE_K_GNUEmptyCompoundLiteral)
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
    if (elements && elements->len == 1 && kind(o,elements->children[0]) == CE_K_InitializerElement) {
        const CeTree *value = elements->children[0]->children[0];
        const CeExpr *literal = kind(o,value) == CE_K_InitializerExpression ? typed(o,value->children[0]) : NULL;
        if (literal && kind(o,value->children[0]) == CE_K_IntConst && literal->constant && literal->value == 0)
            elements = NULL;
    }
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
        if (reference_type(o,m->type)) {
            if (!target.variable) return fail(o,t,"pointer field has no lifetime declaration");
            Reference source = {.null=true,.via=SIZE_MAX};
            if (view(o,m->type)) {
                if (!values[i] || !view_initializer(o,values[i],view(o,m->type),&source))
                    return fail(o,t,"view field requires an explicit whole pointer/length initializer");
            } else if (values[i]) {
                if (kind(o,values[i]) != CE_K_InitializerExpression ||
                    !expression(o,values[i]->children[0],&source)) return false;
            }
            if (!pointer_binding(o,t,target.variable,source)) return false;
        } else if (ce_type_record(m->type) && reference_record(m->type,0)) {
            if (!record_spec(o,m->type)) return fail(o,t,"nested record requires a reference-field declaration");
            if (!record_initializer(o,values[i],m->type,target)) return false;
        } else if (m->type->kind == CE_T_ARRAY && reference_record(m->type,0)) {
            if (!reference_contracts(o,m->type,0) || !record_initializer(o,values[i],m->type,target)) return false;
        }
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
    bool known = extent && extent->constant, whole = false, length_checked = false;
    Reference length_source = {.null=true,.via=SIZE_MAX};
    if (!known && kind(o,length) == CE_K_Member) {
        const CeExpr *base = typed(o,length->children[0]);
        View *parent = base ? view(o,base->type) : NULL;
        if (parent && same(parent->length->name,ce_sema_name(o->sema,length->children[1]))) {
            if (!expression(o,length->children[0],&length_source) ||
                !checked_reference(o,length,length_source,false)) return false;
            length_checked = true; known = true;
            whole = whole_range(o,length_source.range);
            if (whole && parent->stride != spec->stride)
                return fail(o,length,"whole-view reconstruction must preserve element width");
            count = whole ? 1 : length_source.range.count / parent->stride;
        }
    }
    if (!known) return fail(o,length,"dynamic view length needs a checked span constructor");
    if (count > ULLONG_MAX / spec->stride) return fail(o,length,"view element count overflows its byte extent");
    if (!whole) count *= spec->stride;
    Reference ignored;
    if (!length_checked && !expression(o,length,&ignored)) return false;
    Reference source;
    if (!expression(o,data,&source)) return false;
    if (whole) {
        if (!whole_range(o,source.range) || !source.exact ||
            source.range.root != length_source.range.root || source.range.first != length_source.range.first ||
            source.range.count != length_source.range.count)
            return fail(o,t,"whole-view pointer and length must have the same origin");
    } else if (whole_range(o,source.range))
        return fail(o,t,"a numeric length cannot establish a subextent of a dynamic span");
    if (source.authority) return fail(o,data,"opaque capabilities cannot be exposed as byte views");
    if (spec->family) {
        if (source.family != spec->family || count != source.range.count)
            return fail(o,data,"owning view requires the whole allocation from its declared family");
    } else if (source.family && !source.owner) return fail(o,data,"view cannot discard a temporary allocation owner");
    if (source.null && count == 0 && !spec->write) source = (Reference){{0,0,0},0,true,false,false,0,0,0,NULL,NULL};
    if (!source.exact || !checked_reference(o,data,source,true))
        return fail(o,data,"bounded view requires a precise, non-null source extent");
    if (count > source.range.count) return fail(o,length,"view length exceeds the source allocation range");
    source.range.count = count;
    if (!spec->family) { source.family = 0; source.owner = 0; }
    if (!access(o,t,source.via,source.range,spec->write)) return false;
    if (o->plan) {
        if (whole) fprintf(o->plan," (whole-view %zu %zu %s)\n",source.via,source.range.root,spec->write ? "write" : "read");
        else {
            fprintf(o->plan," (view %zu ",source.via); range_plan(o->plan,source.range);
            fprintf(o->plan," %llu %s)\n",count,spec->write ? "write" : "read");
        }
    }
    *out = source;
    return true;
}

/* Unknown element reads need a common referent, not an invented element
 * address. Recheck every pointwise field; do not cache an index-dependent
 * non-null fact or infer that distinct index variables denote distinct rows. */
static bool uniform_array_reference(Ownership *o, const CeTree *site, Place storage, Reference *out) {
    if (kind(o,site) != CE_K_Member || kind(o,site->children[0]) != CE_K_Subscript)
        return fail(o,site,"unknown reference field requires a precise index or uniform array origin");
    const CeTree *element = site->children[0];
    const CeExpr *array = typed(o,element->children[0]);
    CeType *type = array ? array->type : NULL;
    if (!type || type->kind != CE_T_ARRAY || type->length <= 0 || !ce_type_record(type->base) ||
        storage.range.count != ce_type_size(o->sema,type))
        return fail(o,site,"uniform reference read requires the complete typed array footprint");
    CeText name = ce_sema_name(o->sema,site->children[1]);
    FieldSpec *spec = field_spec(o,type->base,name);
    if (!spec || spec->write || spec->family)
        return fail(o,site,"unknown element cannot copy an owning or exclusive reference field");
    CeMember *member = NULL;
    for (size_t i = 0; i < type->base->tag->record->nmembers; ++i)
        if (same(type->base->tag->record->members[i].name,name)) member = &type->base->tag->record->members[i];
    if (!member) return fail(o,site,"uniform field has no C member layout");
    unsigned long long stride = ce_type_size(o->sema,type->base);
    if (!stride) return fail(o,site,"uniform reference array has no complete element extent");
    Reference common = {.null=true,.via=SIZE_MAX}; bool found = false, nullable = false;
    for (unsigned long long i = 0; i < (unsigned long long)type->length; ++i) {
        Range range = {storage.range.root,storage.range.first+i*stride+member->offset,
                       ce_type_size(o->sema,member->type)};
        if (!within(storage.range,range)) return fail(o,site,"array field exceeds its container footprint");
        StoredField *field = field_cell(o,site,spec,range);
        Variable *value = &field->value;
        if (!value->initialized || value->moved)
            return fail(o,site,"uniform array read sees an uninitialized or moved field");
        Reference r = value->reference;
        reference_authority(o,&r,member->type);
        if (r.null) { nullable = true; continue; }
        if (r.family || r.authority || (spec->is_static && r.range.root != 0))
            return fail(o,site,"uniform shared origin cannot carry release or capability authority");
        if (!checked_reference(o,site,r,false)) return false;
        nullable |= r.nullable;
        if (!found) { common = r; found = true; }
        else {
            if (common.range.root != r.range.root || common.range.first != r.range.first ||
                common.range.count != r.range.count)
                return fail(o,site,"unknown array element has different reference origins");
            common.exact &= r.exact;
        }
    }
    common.nullable |= nullable; *out = common; return true;
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
        if (e && e->kind == CE_ENTITY_ENUMERATOR) return true;
        if (e && e->kind == CE_ENTITY_FUNCTION) {
            *out = static_reference(false); out->target = function_named(o,e->name);
            if (out->target && !out->target->use) out->target->use = t;
            return true;
        }
        if (type->type->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC))
            return fail(o,t,"volatile and atomic accesses require a native resource contract");
        Place p;
        if (!place(o,t,&p)) return false;
        Variable *v = &o->state.variables[p.variable->id-1];
        if ((!v->initialized && !record_spec(o,type->type)) || v->moved) return fail(o,t,"read of an uninitialized or moved value");
        if (type->type->kind == CE_T_ARRAY) {
            *out = (Reference){p.range,p.via,p.exact,false,false,0,0,0,NULL,NULL}; return true;
        }
        if (!access(o,t,p.via,p.range,false)) return false;
        if (record_spec(o,type->type)) { *out = (Reference){p.range,p.via,p.exact,false,false,0,0,0,NULL,NULL}; return true; }
        if (reference_type(o,type->type)) { *out = v->reference; reference_authority(o,out,type->type); }
        return true;
    }
    if (k == CE_K_AddressOf) {
        CeEntity *function = named_variable(o,t->children[0]);
        if (function && function->kind == CE_ENTITY_FUNCTION) return expression(o,t->children[0],out);
        if (view_component(o,t->children[0])) return fail(o,t,"view fields cannot escape their pointer/length contract");
        Place p; if (!place(o,t->children[0],&p)) return false;
        if (p.variable && !binding_id(o,p.variable->id)->initialized)
            return fail(o,t,"borrow of uninitialized storage needs an output-initialization contract");
        *out = (Reference){p.range,p.via,p.exact,false,false,0,0,0,NULL,NULL};
        reference_authority(o,out,type->type); return true;
    }
    if (k == CE_K_Member && view_component(o,t)) {
        if (type->type->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC))
            return fail(o,t,"volatile and atomic view metadata require a native resource contract");
        Reference span;
        if (!expression(o,t->children[0],&span) || !checked_reference(o,t,span,false)) return false;
        if (pointer(type->type)) *out = span;
        return true;
    }
    if (k == CE_K_Member || k == CE_K_PointerMember || k == CE_K_Indirection || k == CE_K_Subscript) {
        if (type->type->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC))
            return fail(o,t,"volatile and atomic accesses require a native resource contract");
        if (k == CE_K_Member && typed(o,t->children[0])->category == CE_RVALUE) {
            const CeType *base = typed(o,t->children[0])->type;
            if (base->kind != CE_T_STRUCT || !base->tag || !base->tag->record ||
                !base->tag->record->complete || base->tag->record->flexible ||
                (base->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC)) ||
                reference_record(base,0) || contains_capability(o,base,0))
                return fail(o,t,"temporary member projection requires a plain value structure");
            if (type->type->kind == CE_T_ARRAY)
                return fail(o,t,"temporary array projection requires a checked lifetime");
            Reference ignored;
            if (!expression(o,t->children[0],&ignored)) return false;
            return (ignored.null && !ignored.family && !ignored.owner && !ignored.authority) ||
                fail(o,t,"temporary value projection cannot discard reference or release authority");
        }
        Place p;
        if (!place(o,t,&p) || !access(o,t,p.via,p.range,false)) return false;
        if (type->type->kind == CE_T_ARRAY) *out = (Reference){p.range,p.via,p.exact,false,false,0,0,0,NULL,NULL};
        else if (reference_type(o,type->type)) {
            if (!p.exact && !p.variable && k == CE_K_Member)
                return uniform_array_reference(o,t,p,out);
            if (p.variable && p.variable->id > o->sema->nentities) {
                Variable *v = binding_id(o,p.variable->id);
                if (!v || !v->initialized || v->moved) return fail(o,t,"reference field is uninitialized or moved");
                *out = v->reference; reference_authority(o,out,type->type); return true;
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
            if (vk == CE_K_Call || vk == CE_K_CompoundLiteral || vk == CE_K_CompoundLiteralComma || vk == CE_K_GNUEmptyCompoundLiteral) {
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
            else if (!p.exact && kind(o,t->children[0]) == CE_K_Member &&
                     kind(o,t->children[0]->children[0]) == CE_K_Subscript) {
                const CeTree *member = t->children[0];
                CeType *record = typed(o,member->children[0])->type;
                FieldSpec *spec = field_spec(o,record,ce_sema_name(o->sema,member->children[1]));
                Reference header = {p.range,p.via,false,false,false,0,0,0,NULL,NULL};
                if (!spec) return fail(o,t,"unknown indexed pointer store requires a field contract");
                if (!static_field_update(o,t,header,record,spec,r)) return false;
            }
            else if (p.output >= 0 && o->function->stores[p.output] >= -2 && o->function->stores[p.output] != -1) {
                if (!store_output(o,t,(size_t)p.output,p.range,target->type,r)) return false;
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
            /* A borrowed origin or null keeps that origin's lifetime on both
             * paths. An optional owner instead needs a branch-specific move. */
            if (yes.null && !no.family) { *out = no; out->nullable = true; }
            else if (no.null && !yes.family) { *out = yes; out->nullable = true; }
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
    if (e->type->kind == CE_T_ARRAY && reference_record(e->type,0) && !reference_contracts(o,e->type,0))
        return fail(o,site,"reference-bearing arrays require declared element field contracts");
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
        if (whole_range(o,r.range)) return fail(o,t,"dereferencing a dynamic span needs a checked bounded operation");
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
            if (whole_range(o,r.range)) return fail(o,t,"indexing a dynamic span needs a checked bounded operation");
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
            if (record_spec(o,e->type) || (e->type->kind == CE_T_ARRAY && reference_record(e->type,0))) {
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

static bool return_record(Ownership *o, const CeTree *site, const CeTree *value) {
    Function *f = o->function;
    while (kind(o,value) == CE_K_SomeFullExpression || kind(o,value) == CE_K_SomeExpression)
        value = value->children[0];
    unsigned vk = kind(o,value);
    Place storage; bool temporary = false;
    CeType *type = f->entity->type->base;
    if (vk == CE_K_Call || vk == CE_K_CompoundLiteral || vk == CE_K_CompoundLiteralComma ||
        vk == CE_K_GNUEmptyCompoundLiteral) {
        size_t id = root(o,ce_type_size(o->sema,type),true,"local",0);
        storage = (Place){{id,0,ce_type_size(o->sema,type)},id,true,NULL,-1};
        temporary = true;
        if (!record_initializer(o,value,type,storage)) return false;
    } else if (!place(o,value,&storage)) return false;
    if (!storage.exact || !access(o,site,storage.via,storage.range,false))
        return fail(o,site,"returned record requires a precise live value");
    for (size_t i = 0; i < f->nresults; ++i) {
        FieldPost *post = &f->results[i];
        Range slot = {storage.range.root,storage.range.first+post->offset,ce_type_size(o->sema,post->field->type)};
        StoredField *cell = field_cell(o,site,post->field,slot);
        if (!cell->value.initialized || cell->value.moved)
            return fail(o,site,"returned record has an uninitialized or moved reference field");
        Reference r = cell->value.reference;
        if ((r.null || r.nullable) && !post->field->nullable)
            return fail(o,site,"nullable record result violates its non-null field contract");
        if (post->source == -3 && !r.null) {
            Loan *l = loan(o,r.via);
            if (!r.family || r.family != post->field->family || !l || !l->live || l->family != r.family)
                return fail(o,site,"returned owning field lacks its declared release authority");
            for (size_t j = 0; j < o->state.nloans; ++j)
                if (o->state.loans[j].live && ancestor(o,l->id,&o->state.loans[j]))
                    return fail(o,site,"cannot return an owning field while it is borrowed");
            l->live = false;
            if (o->plan) fprintf(o->plan," (record-give %llu %zu %u)\n",post->offset,r.via,r.family);
            if (r.owner) {
                Variable *owner = binding_id(o,r.owner);
                owner->moved = true; owner->reference.family = 0;
            }
            continue;
        }
        if (r.family) return fail(o,site,"returned owning field requires an ownership-transfer result contract");
        if (!r.null) {
            if (!within(post->allowed.range,r.range))
                return fail(o,site,"returned record field escapes its declared input lifetime");
            if (view(o,post->field->type) && (!r.exact || r.range.first != post->allowed.range.first ||
                r.range.count != post->allowed.range.count))
                return fail(o,site,"returned view must preserve the complete declared input extent");
            if (!access(o,site,r.via,r.range,post->field->write)) return false;
        }
        if (o->plan) {
            if (r.null) fprintf(o->plan," (null-record-result %llu)\n",post->offset);
            else {
                fprintf(o->plan," (record-result %llu %zu ",post->offset,r.via);
                range_plan(o->plan,r.range);
                fprintf(o->plan," %s)\n",post->field->write ? "write" : "read");
            }
        }
    }
    if (temporary && (!drop_fields(o,site,storage.range.root) || !end(o,site,storage.via))) return false;
    return true;
}

static bool return_statement(Ownership *o, const CeTree *t) {
    Function *f = o->function;
    Reference r = {.null=true,.via=SIZE_MAX};
    if (f->nresults) {
        if (!return_record(o,t,t->children[0])) return false;
    } else if (!expression(o,t->children[0],&r)) return false;
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

/* Follow a forward function-body cleanup tail with this edge's actual state.
   Replaying its permission actions preserves distinct ownership on each path;
   the native C and its labels are not rewritten. */
static bool cleanup_jump(Ownership *o, const CeTree *jump) {
    const CeTree *body = unwrap(o,o->function->body);
    if (kind(o,body) != CE_K_Block) return fail(o,jump,"cleanup jump needs a function compound body");
    const CeTree *items = body->children[0], *target = NULL; size_t at = 0;
    CeText name = ce_sema_name(o->sema,jump->children[0]);
    for (size_t i = 0; i < items->len && !target; ++i) {
        const CeTree *label = kind(o,items->children[i]) == CE_K_BlockStatement ?
            unwrap(o,items->children[i]->children[0]) : NULL;
        while (kind(o,label) == CE_K_Label) {
            if (same(name,ce_sema_name(o->sema,label->children[0]))) { target=label; at=i; break; }
            label=unwrap(o,label->children[1]);
        }
    }
    if (!target) return fail(o,jump,"cleanup jump target must be a label in the function's outer block");
    if (target->offset <= jump->offset) return fail(o,jump,"backward goto requires a checked loop invariant");
    if (!cleanup(o,jump,2)) return false;
    /* Fixed automatic storage is in scope at the label even when its
       initializer was bypassed. Create its slot without checking/executing
       that initializer; every later read still requires initialization. */
    for (size_t i=0; i<at; ++i) {
        const CeTree *item=items->children[i];
        if (item->offset<=jump->offset || kind(o,item)!=CE_K_BlockDeclaration) continue;
        const CeTree *decl=item->children[0];
        if (kind(o,decl)==CE_K_StaticAssertionDeclaration) continue;
        if (kind(o,decl)!=CE_K_Declaration) return fail(o,jump,"unsupported declaration crossed by cleanup jump");
        const CeTree *declarations=decl->children[1];
        if (kind(o,declarations)==CE_K_NoInitDeclarators) continue;
        declarations=declarations->children[0];
        for (size_t j=0; j<declarations->len; ++j) {
            const CeTree *definition=declarations->children[j];
            CeEntity *e=entity(o,ce_declarator_name(o->sema,definition->children[0]));
            if (e && e->kind!=CE_ENTITY_TYPEDEF && e->kind!=CE_ENTITY_FUNCTION &&
                !o->state.variables[e->id-1].declared && !bind_variable(o,e,definition,false)) return false;
        }
    }
    State *saved_entry=o->loop_entry, *saved_exits=o->loop_exits, *saved_latch=o->loop_latch;
    State *saved_switch=o->switch_entry; const CeTree *saved_step=o->loop_step;
    bool *saved_exit=o->switch_exit;
    size_t saved_loops=o->loops, saved_scope=o->loop_scope, saved_switches=o->switches,
           saved_switch_scope=o->switch_scope;
    o->loop_entry=o->loop_exits=o->loop_latch=o->switch_entry=NULL;
    o->loop_step=NULL; o->switch_exit=NULL; o->loops=o->switches=0;
    o->state.scope=1;
    bool ok=statement(o,target->children[1]);
    for (size_t i=at+1; ok && o->state.reachable && i<items->len; ++i) {
        const CeTree *item=items->children[i];
        if (kind(o,item)==CE_K_BlockDeclaration) ok=declaration(o,item->children[0]);
        else if (kind(o,item)==CE_K_BlockStatement) ok=statement(o,item->children[0]);
        else ok=fail(o,item,"unsupported cleanup block item");
    }
    if (ok && o->state.reachable) {
        if (!ce_type_void(o->function->entity->type->base)) ok=fail(o,target,"cleanup tail may reach the function end without a result");
        else {
            for (size_t i=0; ok && i<o->function->nparameters; ++i)
                if (o->function->stores[i]>=-2 && o->function->stores[i]!=-1 &&
                    !o->state.variables[o->function->parameters[i]->id-1].output_initialized)
                    ok=fail(o,target,"cleanup tail leaves its output lifetime uninitialized");
            if (ok) ok=field_postconditions(o,target) && cleanup(o,target,0);
            if (ok && o->plan) fputs(" (return)\n",o->plan);
            o->state.reachable=false;
        }
    }
    o->loop_entry=saved_entry; o->loop_exits=saved_exits; o->loop_latch=saved_latch;
    o->switch_entry=saved_switch; o->loop_step=saved_step; o->switch_exit=saved_exit;
    o->loops=saved_loops; o->loop_scope=saved_scope; o->switches=saved_switches;
    o->switch_scope=saved_switch_scope;
    return ok;
}

static bool do_statement(Ownership *o, const CeTree *t) {
    size_t scope=++o->state.scope;
    State *saved_entry=o->loop_entry, *saved_exits=o->loop_exits, *saved_latch=o->loop_latch;
    const CeTree *saved_step=o->loop_step;
    size_t saved_scope=o->loop_scope, saved_switches=o->switches;
    State entry=clone(o), breaks={.reachable=false}, latch={.reachable=false};
    o->loop_entry=&entry; o->loop_exits=&breaks; o->loop_latch=&latch;
    o->loop_step=NULL; o->loop_scope=scope; o->switches=0; ++o->loops;
    if (o->plan) fputs(" (once (first\n",o->plan);
    bool ok=statement(o,t->children[0]);
    if (ok && o->state.reachable) ok=cleanup(o,t,scope+1);
    if (ok) ok=join(o,t,clone(o),latch);
    if (o->plan) fputs(" ) (next\n",o->plan);
    const CeTree *condition=t->children[1]; Reference ignored;
    State after={.reachable=false};
    const CeExpr *constant=typed(o,condition);
    bool repeats=!constant || !constant->constant || constant->value!=0;
    if (ok && o->state.reachable) {
        o->state.scope=scope;
        ok=expression(o,condition,&ignored);
        if (ok) {
            State tested=clone(o);
            refine(o,condition,false); after=clone(o);
            o->state=tested; refine(o,condition,true);
            if (repeats) {
                entry=clone(o); latch=(State){.reachable=false};
                State repeated_breaks={.reachable=false}; o->loop_exits=&repeated_breaks;
                if (o->plan) fputs(" (loop (iteration (first\n",o->plan);
                ok=statement(o,t->children[0]);
                if (ok && o->state.reachable) ok=cleanup(o,t,scope+1);
                if (ok) ok=join(o,t,clone(o),latch);
                if (o->plan) fputs(" ) (next\n",o->plan);
                if (ok && o->state.reachable) {
                    o->state.scope=scope;
                    ok=expression(o,condition,&ignored);
                    if (ok) {
                        State back=clone(o);
                        refine(o,condition,false);
                        ok=join(o,t,clone_from(o,after),clone(o));
                        if (ok) after=clone(o);
                        o->state=back; refine(o,condition,true); back=clone(o);
                        if (ok) ok=loop_nullability(o,t,entry,back) && join(o,t,clone_from(o,entry),back);
                    }
                }
                if (ok && repeated_breaks.reachable)
                    ok=join(o,t,clone_from(o,entry),repeated_breaks);
                if (ok && o->state.next>after.next) after.next=o->state.next;
                if (o->plan) fputs(" )))\n",o->plan);
            }
        }
    }
    if (o->plan) fputs(" ))\n",o->plan);
    if (ok) ok=join(o,t,after,breaks);
    o->loop_entry=saved_entry; o->loop_exits=saved_exits; o->loop_latch=saved_latch;
    o->loop_step=saved_step; o->loop_scope=saved_scope; o->switches=saved_switches; --o->loops;
    if (ok && o->state.reachable) ok=cleanup(o,t,scope);
    o->state.scope=scope-1;
    return ok;
}

static bool switch_statement(Ownership *o, const CeTree *t) {
    Reference ignored;
    if (!expression(o,t->children[0],&ignored)) return false;
    const CeTree *body = unwrap(o,t->children[1]);
    if (kind(o,body) != CE_K_Block) return fail(o,t,"checked switch requires a compound body");
    State start = clone(o), joined = start; bool have = false;
    const CeTree *items = body->children[0];
    bool has_default = false, exits = false;
    for (size_t i = 0; i < items->len; ++i) {
        const CeTree *label = kind(o,items->children[i]) == CE_K_BlockStatement ? unwrap(o,items->children[i]->children[0]) : NULL;
        while (kind(o,label) == CE_K_Case || kind(o,label) == CE_K_Default) {
            has_default |= kind(o,label) == CE_K_Default;
            label = unwrap(o,label->children[kind(o,label) == CE_K_Case ? 1 : 0]);
        }
    }
    State *saved_entry = o->switch_entry; size_t saved_scope = o->switch_scope;
    bool *saved_exit = o->switch_exit; o->switch_exit = &exits;
    o->switch_entry = &start; o->switch_scope = start.scope; ++o->switches;
    if (o->plan) fputs(has_default ? " (switch-total\n" : " (switch\n",o->plan);
    for (size_t i = 0; i < items->len;) {
        const CeTree *item = items->children[i];
        const CeTree *label = kind(o,item) == CE_K_BlockStatement ? unwrap(o,item->children[0]) : NULL;
        if (kind(o,label) != CE_K_Case && kind(o,label) != CE_K_Default)
            return fail(o,item,"checked switch requires explicit arms without fall-through");
        o->state = start; o->state = clone(o); o->state.next = joined.next;
        ++o->state.scope;
        if (o->plan) fputs(" (arm\n",o->plan);
        const CeTree *arm_body = label;
        /* Adjacent labels form one arm in the C tree, rather than separate
           block items. Their shared statement has one permission effect. */
        do {
            arm_body = unwrap(o,arm_body->children[kind(o,arm_body) == CE_K_Case ? 1 : 0]);
        } while (kind(o,arm_body) == CE_K_Case || kind(o,arm_body) == CE_K_Default);
        if (!statement(o,arm_body)) return false;
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
        if (o->state.reachable && i < items->len)
            return fail(o,label,"checked switch forbids fall-through between distinct arms");
        if (o->state.reachable && !cleanup(o,t,start.scope+1)) return false;
        /* break is represented as an arm exit, while return remains terminal. */
        o->state.scope = start.scope;
        State arm = clone(o);
        if (o->plan) fputs(" )\n",o->plan);
        if (!have) { joined = arm; have = true; }
        else { if (!join(o,t,joined,arm)) return false; joined = clone(o); }
    }
    if (o->plan) fputs(" )\n",o->plan);
    --o->switches; o->switch_entry = saved_entry; o->switch_scope = saved_scope;
    o->switch_exit = saved_exit;
    if (!has_default || exits) return join(o,t,joined,start);
    o->state = joined;
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
    if (k == CE_K_Label) return statement(o,t->children[1]);
    if (k == CE_K_Goto) return cleanup_jump(o,t);
    if (k == CE_K_DoWhile) return do_statement(o,t);
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
        const CeTree *condition = t->children[k == CE_K_While ? 0 : 1];
        loop_guard_entry(o,condition);
        State entry = clone(o), exits = {.reachable=false}, *saved_entry = o->loop_entry, *saved_exits = o->loop_exits,
            *saved_latch = o->loop_latch;
        const CeTree *saved_step = o->loop_step;
        size_t saved_scope = o->loop_scope, saved_switches = o->switches;
        o->loop_entry = &entry; o->loop_exits = &exits; o->loop_scope = scope; o->switches = 0; ++o->loops;
        o->loop_latch = NULL;
        o->loop_step = k == CE_K_While ? NULL : t->children[2];
        if (o->plan) fputs(" (loop\n",o->plan);
        if (!expression(o,condition,&ignored)) return false;
        State evaluated = o->state, condition_exit = clone(o);
        o->state = condition_exit;
        refine(o,condition,false);
        condition_exit = clone(o);
        o->state = evaluated;
        refine(o,condition,true);
        if (!statement(o,t->children[k == CE_K_While ? 1 : 3])) return false;
        if (o->state.reachable && k != CE_K_While && !expression(o,t->children[2],&ignored)) return false;
        if (o->state.reachable && !cleanup(o,t,scope+1)) return false;
        State backedge = clone(o);
        if (!loop_nullability(o,t,entry,backedge) || !join(o,t,clone_from(o,entry),backedge)) return false;
        if (condition_exit.next < o->state.next) condition_exit.next = o->state.next;
        o->state = condition_exit;
        if (exits.reachable && !join(o,t,clone(o),exits)) return false;
        if (o->plan) fputs(" )\n",o->plan);
        o->loop_entry = saved_entry; o->loop_exits = saved_exits; o->loop_scope = saved_scope; o->switches = saved_switches; --o->loops;
        o->loop_latch = saved_latch;
        o->loop_step = saved_step;
        if (!cleanup(o,t,scope)) return false;
        o->state.scope = scope-1; return true;
    }
    if (k == CE_K_Continue || k == CE_K_Break) {
        if (k == CE_K_Break && o->switches) {
            if (!cleanup(o,t,o->switch_scope+1) || !join(o,t,*o->switch_entry,clone(o))) return false;
            *o->switch_exit = true;
            if (o->plan) fputs(" (arm-exit)\n",o->plan);
            o->state.reachable = false;
            return true;
        }
        if (!o->loop_entry || !cleanup(o,t,o->loop_scope+1)) return fail(o,t,"loop transfer outside a checked loop");
        if (o->loop_latch) {
            o->state.scope=o->loop_scope;
            State *target=k==CE_K_Continue ? o->loop_latch : o->loop_exits;
            if (!target || !join(o,t,*target,clone(o))) return false;
            *target=clone(o);
            if (o->plan) fputs(k==CE_K_Break ? " (break)\n" : " (continue)\n",o->plan);
            o->state.reachable=false; return true;
        }
        if (k == CE_K_Continue && o->loop_step && !expression(o,o->loop_step,&ignored)) return false;
        State outgoing = clone(o);
        if (k == CE_K_Continue && !loop_nullability(o,t,*o->loop_entry,outgoing)) return false;
        if (!join(o,t,clone_from(o,*o->loop_entry),outgoing)) return false;
        if (k == CE_K_Break) {
            if (!o->loop_exits || !join(o,t,*o->loop_exits,clone(o))) return false;
            *o->loop_exits = clone(o);
        }
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

/* Inline record/array paths stop at the reference field. Following that
   reference would require an additional lifetime contract for its target. */
static bool reference_field_path(Ownership *o, CeType *type, const CeAtom *path,
                                 FieldSpec **field, unsigned long long *offset) {
    *offset = 0; *field = NULL;
    if (!path || path->kind != CE_ATOM_LIST || !path->len)
        return malformed(o,path ? path : o->declaration,"reference path must name a field");
    for (size_t i = 0; i < path->len; ++i) {
        const CeAtom *part = path->items[i];
        if (type->kind == CE_T_ARRAY) {
            size_t index;
            unsigned long long stride = ce_type_size(o->sema,type->base);
            if (!natural(part,&index) || type->length < 0 || index >= (unsigned long long)type->length ||
                !stride || index > (ULLONG_MAX-*offset)/stride)
                return malformed(o,part,"reference path array index is outside its declared extent");
            *offset += index*stride; type = type->base; continue;
        }
        if (part->kind != CE_ATOM_SYMBOL || !ce_type_record(type) || !type->tag || !type->tag->record)
            return malformed(o,part,"reference path requires inline record members or constant array indices");
        CeMember *member = NULL;
        for (size_t j = 0; j < type->tag->record->nmembers; ++j)
            if (same(type->tag->record->members[j].name,part->text)) member = &type->tag->record->members[j];
        if (!member || member->bitfield || member->offset > ULLONG_MAX-*offset)
            return malformed(o,part,"reference path member is absent or has no ordinary layout");
        *offset += member->offset;
        if (i+1 == path->len) *field = field_spec(o,type,member->name);
        type = member->type;
    }
    return *field != NULL || malformed(o,path,"reference path must end at a declared pointer or whole-view field");
}

static bool append_field_post(Ownership *o, Function *f, const CeAtom *site,
                              size_t parameter, FieldSpec *field, unsigned long long offset,
                              int source, const CeAtom *source_path) {
    FieldPost **items = parameter == SIZE_MAX ? &f->results : &f->posts;
    size_t *count = parameter == SIZE_MAX ? &f->nresults : &f->nposts;
    for (size_t i = 0; i < *count; ++i)
        if ((*items)[i].parameter == parameter && (*items)[i].offset == offset)
            return malformed(o,site,"duplicate field postcondition");
    FieldPost *posts = ce_alloc(o->arena,(*count+1)*sizeof(*posts));
    if (*count) memcpy(posts,*items,*count*sizeof(*posts));
    posts[(*count)++] = (FieldPost){.parameter=parameter,.field=field,.source=source,
        .offset=offset,.source_path=source_path};
    *items = posts;
    return true;
}

static bool resolve_post_origin(Ownership *o, Function *f, FieldPost *post) {
    if (!post->source_path) {
        if (post->source >= 0 && f->families[post->source])
            return fail(o,f->body,"borrowed record result cannot retain a consumed owning input");
        if (post->source >= 0 && (view(o,post->field->type) != NULL) !=
            (view(o,f->entity->type->parameters[post->source]) != NULL))
            return fail(o,f->body,"a whole-view result must retain a whole-view input");
        if (post->source >= 0 && view(o,post->field->type) &&
            !view_origin_compatible(o,post->field->type,f->entity->type->parameters[post->source]))
            return fail(o,f->body,"whole-view result has incompatible elements or access rights");
        if (post->source < 0 && view(o,post->field->type))
            return fail(o,f->body,"whole-view results require an explicit input extent");
        return true;
    }
    FieldSpec *field; unsigned long long offset;
    if (!reference_field_path(o,f->entity->type->parameters[post->source]->base,
                              post->source_path,&field,&offset)) return false;
    for (size_t k = 0; k < f->nfields; ++k)
        if (f->fields[k].parameter == (size_t)post->source && f->fields[k].offset == offset) {
            if (f->fields[k].owning) return fail(o,f->body,"stored reference cannot outlive a consumed input field");
            if ((view(o,post->field->type) != NULL) != (view(o,field->type) != NULL))
                return fail(o,f->body,"a whole-view field result must retain a whole-view origin");
            if (view(o,post->field->type) && !view_origin_compatible(o,post->field->type,field->type))
                return fail(o,f->body,"whole-view field result has incompatible elements or access rights");
            post->source = field->is_static ? -2 : (int)(f->entity->type->nparameters+k);
            return true;
        }
    return fail(o,f->body,"stored reference origin is absent from the parameter interface");
}

static bool complete_record_result(Ownership *o, Function *f, CeType *type, unsigned long long offset) {
    if (type->kind == CE_T_ARRAY) {
        unsigned long long stride = ce_type_size(o->sema,type->base);
        if (type->length < 0 || !stride) return fail(o,f->body,"result array requires a complete extent");
        for (unsigned long long i = 0; i < (unsigned long long)type->length; ++i) {
            if (i > (ULLONG_MAX-offset)/stride) return fail(o,f->body,"result array offset overflows");
            if (!complete_record_result(o,f,type->base,offset+i*stride)) return false;
        }
        return true;
    }
    if (!ce_type_record(type) || !record_spec(o,type))
        return !reference_record(type,0) || fail(o,f->body,"nested result record requires declared field contracts");
    for (size_t i = 0; i < type->tag->record->nmembers; ++i) {
        CeMember *member = &type->tag->record->members[i];
        if (member->offset > ULLONG_MAX-offset) return fail(o,f->body,"result member offset overflows");
        if (reference_type(o,member->type)) {
            bool found = false;
            for (size_t j = 0; j < f->nresults; ++j)
                found |= f->results[j].offset == offset+member->offset;
            if (!found) return fail(o,f->body,"returns-record must specify every reference field");
        } else if (reference_record(member->type,0) &&
                   !complete_record_result(o,f,member->type,offset+member->offset)) return false;
    }
    return true;
}

static bool signature(Ownership *o, Function *f, const CeAtom *spec) {
    f->spec = spec;
    for (size_t i = 2; i < spec->len; ++i) {
        const CeAtom *part = spec->items[i];
        if (head(part,"calls") && part->len == 3) {
            size_t index;
            Callback *callback = part->items[2]->kind == CE_ATOM_SYMBOL ? callback_named(o,part->items[2]->text) : NULL;
            if (!natural(part->items[1],&index) || index >= f->entity->type->nparameters || !callback || f->callbacks[index] ||
                !ce_type_compatible(o->sema,f->entity->type->parameters[index],callback->type))
                return malformed(o,part,"calls requires a function-pointer parameter and matching callback contract");
            f->callbacks[index] = callback;
        } else if (head(part,"obligation") && part->len == 2 && part->items[1]->kind == CE_ATOM_TEXT && part->items[1]->text.len) {
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
                !reference_type(o,f->entity->type->parameters[index])) return malformed(o,part,"invalid borrowed-result parameter");
            f->result_from = (int)index;
            size_t n = part->len-2;
            if (n && symbol(part->items[part->len-1],"nullable")) { f->result_nullable = true; --n; }
            if (view(o,f->entity->type->parameters[index]) &&
                (!view(o,f->entity->type->base) || n || f->result_nullable))
                return malformed(o,part,"whole-view result requires the complete nonnullable input view");
            if (view(o,f->entity->type->parameters[index]) &&
                !view_origin_compatible(o,f->entity->type->base,f->entity->type->parameters[index]))
                return malformed(o,part,"whole-view result has incompatible elements or access rights");
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
        } else if ((head(part,"stores-record") && part->len >= 3) || (head(part,"returns-record") && part->len >= 2)) {
            bool returned = head(part,"returns-record");
            size_t parameter = SIZE_MAX;
            CeType *record_type = f->entity->type->base;
            if (returned) {
                if (!record_spec(o,record_type) || f->nresults)
                    return malformed(o,part,"returns-record requires a declared reference record and one complete contract");
            } else {
                if (!natural(part->items[1],&parameter) || parameter >= f->entity->type->nparameters ||
                    !pointer(f->entity->type->parameters[parameter]) || read_only(f->entity->type->parameters[parameter]))
                    return malformed(o,part,"stores-record requires an exclusive record pointer parameter");
                record_type = f->entity->type->parameters[parameter]->base;
                if (!f->record_posts) f->record_posts = ce_alloc(o->arena,f->entity->type->nparameters*sizeof(*f->record_posts));
                if (f->record_posts[parameter]) return malformed(o,part,"duplicate stores-record contract");
                f->record_posts[parameter] = true;
            }
            for (size_t j = returned ? 1 : 2; j < part->len; ++j) {
                const CeAtom *entry = part->items[j];
                if (entry->kind != CE_ATOM_LIST || entry->len != 2)
                    return malformed(o,entry,"stores-record entries pair a field path with its lifetime");
                FieldSpec *field; unsigned long long offset;
                if (!reference_field_path(o,record_type,entry->items[0],&field,&offset)) return false;
                const CeAtom *origin = entry->items[1];
                int source = -2; const CeAtom *source_path = NULL;
                if (head(origin,"owns")) {
                    if (!returned || !field->family || origin->len != 2 || origin->items[1]->kind != CE_ATOM_SYMBOL ||
                        field->family != family(o,origin->items[1]->text))
                        return malformed(o,origin,"owned record return requires its field's allocation family");
                    source = -3;
                } else if (field->family) {
                    return malformed(o,entry,"owning result field requires an explicit owns contract");
                } else if (!symbol(origin,"static")) {
                    size_t index;
                    if (!head(origin,"borrow") || origin->len < 2 || !natural(origin->items[1],&index) ||
                        index >= f->entity->type->nparameters || !reference_type(o,f->entity->type->parameters[index]))
                        return malformed(o,origin,"record lifetime requires static or borrow with an input index");
                    source = (int)index;
                    if (origin->len > 2) {
                        if (view(o,f->entity->type->parameters[index]))
                            return malformed(o,origin,"whole-view origin cannot be split into field paths");
                        CeAtom *path = ce_alloc(o->arena,sizeof(*path));
                        *path = (CeAtom){.kind=CE_ATOM_LIST,.items=origin->items+2,.len=origin->len-2,.offset=origin->offset};
                        source_path = path;
                    }
                }
                if (!append_field_post(o,f,entry,parameter,field,offset,source,source_path)) return false;
            }
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
                CeAtom path = {.kind=CE_ATOM_LIST,.items=part->items+2,.len=1,.offset=part->offset};
                unsigned long long offset; FieldSpec *resolved;
                if (!reference_field_path(o,f->entity->type->parameters[index]->base,&path,&resolved,&offset) ||
                    !append_field_post(o,f,part,index,field,offset,is_static ? -2 : (int)source,NULL)) return false;
            }
        } else return malformed(o,part,"unknown or malformed function ownership contract");
    }
    if (f->foreign && !f->obligation.len) return malformed(o,spec,"foreign function requires a named obligation");
    if (f->foreign && f->body && !f->unchecked)
        return malformed(o,spec,"a defined function must be checked, or named unchecked with its obligation");
    /* An unchecked definition may leave its pointer result without an origin;
       a checked call that receives that result is then refused. */
    if (reference_type(o,f->entity->type->base) && !f->result_static && f->result_from < 0 && !f->result_family &&
        !f->unchecked)
        return malformed(o,spec,"pointer return requires a lifetime origin");
    if (view(o,f->entity->type->base) && f->result_nullable)
        return malformed(o,spec,"view results carry an exact extent; optional values require a variant contract");
    return true;
}

static bool record_parts(Ownership *o, Function *f, size_t parameter, CeType *type,
                         unsigned long long offset, bool header_writing) {
    if (type->kind == CE_T_ARRAY) {
        unsigned long long stride = ce_type_size(o->sema,type->base);
        if (type->length < 0 || !stride) return fail(o,f->body,"reference array parameter needs a complete extent");
        for (unsigned long long i = 0; i < (unsigned long long)type->length; ++i)
            if (!record_parts(o,f,parameter,type->base,offset+i*stride,header_writing)) return false;
        return true;
    }
    RecordSpec *spec = record_spec(o,type);
    if (!spec) {
        if (!reference_record(type,0)) return true;
        CeText name = type->tag && type->tag->name.len ? type->tag->name :
            (type->tag && type->tag->naming_alias ? type->tag->naming_alias->name : (CeText){0});
        if (name.len) return fail(o,f->body,"nested reference record %.*s needs its field declaration",
            (int)name.len,name.data);
        return fail(o,f->body,"nested anonymous reference record needs its field declaration");
    }
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
        } else if (member->type->kind == CE_T_ARRAY && reference_record(member->type,0)) {
            if (!record_parts(o,f,parameter,member->type,offset+member->offset,header_writing)) return false;
        }
    }
    return true;
}

static bool function_body(Ownership *o, Function *f) {
    o->function = f;
    if (record_spec(o,f->entity->type->base) && !f->nresults)
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
        if (f->callbacks[i]) {
            v->reference = static_reference(f->nullable[i]); v->reference.callback = f->callbacks[i];
        } else if (pointer(e->type)) {
            unsigned long long size = ce_type_size(o->sema,e->type->base);
            if (!size) return fail(o,e->declaration,"pointer parameter requires a complete pointee or a byte view");
            size_t id = root(o,size,!read_only(e->type),f->families[i] ? "heap" : "input",f->families[i]);
            f->input_roots[i] = id;
            v->reference = (Reference){{id,0,size},id,true,false,f->nullable[i],f->families[i],f->families[i] ? e->id : 0,0,NULL,NULL};
        } else if (view(o,e->type)) {
            View *span = view(o,e->type);
            if (span->family || f->families[i])
                return fail(o,e->declaration,"owned dynamic inputs need an allocation-extent contract");
            size_t id = whole_root(o,span->write);
            f->input_roots[i] = id;
            v->reference = (Reference){{id,0,1},id,true,false,false,0,0,0,NULL,NULL};
        }
        else if (!record_spec(o,e->type) && reference_record(e->type,0)) return fail(o,e->declaration,"value parameter contains undeclared reference fields");
    }
    for (size_t i = 0; i < f->nfields; ++i) {
        FieldPart *part = &f->fields[i]; CeEntity *parameter = f->parameters[part->parameter];
        Variable *v = &o->state.variables[parameter->id-1];
        Range storage = pointer(parameter->type) ? v->reference.range : loan(o,v->slot)->range;
        storage.first += part->offset; storage.count = ce_type_size(o->sema,part->field->type);
        StoredField *cell = field_cell(o,f->body,part->field,storage);
        if (part->field->is_static) {
            cell->value.reference = static_reference(part->field->nullable);
            cell->value.reference.callback = part->field->callback;
        }
        else {
            bool whole = view(o,part->field->type) != NULL;
            unsigned long long size = whole ? 1 : ce_type_size(o->sema,part->field->type->base);
            if (!size) return fail(o,f->body,"record field parameter needs a complete target type");
            size_t id = whole ? whole_root(o,part->writing) :
                root(o,size,part->writing,part->owning ? "heap" : "input",part->owning ? part->field->family : 0);
            part->input = id;
            cell->value.reference = (Reference){{id,0,size},id,true,false,part->field->nullable,
                part->owning ? part->field->family : 0,part->owning ? cell->entity.id : 0,0,NULL,NULL};
        }
        cell->value.initialized = true; cell->value.parameter = !part->owning;
    }
    f->originals = ce_alloc(o->arena,(f->nparameters+f->nfields+1)*sizeof(*f->originals));
    for (size_t i = 0; i < f->nparameters; ++i) f->originals[i] = o->state.variables[f->parameters[i]->id-1].reference;
    for (size_t i = 0; i < f->nfields; ++i) {
        FieldPart *part = &f->fields[i]; CeEntity *parameter = f->parameters[part->parameter];
        Variable *v = &o->state.variables[parameter->id-1];
        Range slot = pointer(parameter->type) ? v->reference.range : loan(o,v->slot)->range;
        slot.first += part->offset; slot.count = ce_type_size(o->sema,part->field->type);
        f->originals[f->nparameters+i] = field_cell(o,f->body,part->field,slot)->value.reference;
    }
    for (size_t i = 0; i < f->nposts; ++i)
        f->posts[i].allowed = f->posts[i].source == -2 ? static_reference(true) : f->originals[f->posts[i].source];
    for (size_t i = 0; i < f->nresults; ++i)
        f->results[i].allowed = f->results[i].source < 0 ? static_reference(true) : f->originals[f->results[i].source];
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
    CeType *parameter = f->entity->type->parameters[f->result_from];
    *input = (size_t)f->result_from;
    if (view(o,parameter)) { *relative = (Range){0,0,1}; return true; }
    CeType *type = parameter->base;
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
        if (reference_type(o,type)) {
            if (followed_pointer) return fail(o,f->body,"result paths through multiple pointer fields require a path contract");
            followed_pointer = true;
            bool found = false;
            for (size_t j = 0; j < f->nfields; ++j)
                if (f->fields[j].parameter == (size_t)f->result_from && f->fields[j].offset == relative->first) {
                    *input = f->entity->type->nparameters + j; found = true; break;
                }
            if (!found) return fail(o,f->body,"returned pointer field has no input contract");
            relative->first = 0;
            if (view(o,type)) {
                if (i+1 != f->result_path->len) return fail(o,f->body,"whole-view result path must stop at the view");
                relative->count = 1; return true;
            }
            type = type->base;
        }
        relative->count = ce_type_size(o->sema,type);
    }
    return true;
}

static CeType *contract_input_type(Function *f, size_t input) {
    return input < f->entity->type->nparameters ? f->entity->type->parameters[input] :
        f->fields[input-f->entity->type->nparameters].field->type;
}

static void abi_text(FILE *file, CeText text);

/* A family is numbered in the order a unit first names it; another unit may
   number it differently, so a portable signature names it. */
static void family_out(Ownership *o, FILE *destination, unsigned brand, bool portable) {
    if (!portable) { fprintf(destination,"%u",brand); return; }
    abi_text(destination,brand && brand <= o->nfamilies ? o->families[brand-1] : (CeText){0});
}

/* The function's admitted interface, as the plan records it. The portable
   form is the same interface for another unit: families and callbacks by
   name, and without the effects on this unit's private globals, which no
   other unit can reach. A pointer result with no origin, allowed only to an
   unchecked definition, is written as unadmitted. */
static bool write_signature_as(Ownership *o, FILE *destination, Function *f, bool portable) {
    CeType *ft = f->entity->type;
    fputs(" (signature (parameters",destination);
    for (size_t i = 0; i < ft->nparameters; ++i) {
        CeType *type = ft->parameters[i];
        unsigned long long count = pointer(type) ? ce_type_size(o->sema,type->base) : 0;
        if (i == 0 && f->release_family) {
            fputs(" (released ",destination); family_out(o,destination,f->release_family,portable); fputc(')',destination);
        } else if (f->families[i]) {
            fprintf(destination," (%s %llu ",f->nullable[i] ? "optional-owned" : "owned",count);
            family_out(o,destination,f->families[i],portable); fputc(')',destination);
        } else if (f->callbacks[i] && portable) {
            fputs(" (callback ",destination); abi_text(destination,f->callbacks[i]->name);
            fprintf(destination," %s)",f->nullable[i] ? "nullable" : "nonnull");
        }
        else if (f->callbacks[i]) fprintf(destination," (static %s)",f->nullable[i] ? "nullable" : "nonnull");
        else if (view(o,type)) fprintf(destination," (viewed %s)",reference_read_only(o,type) ? "read" : "write");
        else if (!pointer(type)) fputs(" (value)",destination);
        else fprintf(destination," (%s %llu %s)",f->nullable[i] ? "optional-borrowed" : "borrowed",count,read_only(type) ? "read" : "write");
    }
    for (size_t i = 0; i < f->nfields; ++i) {
        FieldPart *part = &f->fields[i];
        if (part->field->is_static && part->field->callback && portable) {
            fputs(" (callback ",destination); abi_text(destination,part->field->callback->name);
            fprintf(destination," %s)",part->field->nullable ? "nullable" : "nonnull");
        }
        else if (part->field->is_static) fprintf(destination," (static %s)",part->field->nullable ? "nullable" : "nonnull");
        else if (view(o,part->field->type)) fprintf(destination," (viewed %s)",part->writing ? "write" : "read");
        else if (part->owning) {
            fprintf(destination," (%s %llu ",part->field->nullable ? "optional-owned" : "owned",
                ce_type_size(o->sema,part->field->type->base));
            family_out(o,destination,part->field->family,portable); fputc(')',destination);
        }
        else fprintf(destination," (%s %llu %s)",part->field->nullable ? "optional-borrowed" : "borrowed",
            ce_type_size(o->sema,part->field->type->base),part->writing ? "write" : "read");
    }
    for (size_t i = 0; !portable && f->global_effects && i < o->sema->nentities; ++i)
        if (f->global_effects[i]) fprintf(destination," (borrowed %llu write)",ce_type_size(o->sema,o->sema->entities[i]->type));
    fprintf(destination,") (variadic %s) (result ",f->variadic_read ? "read" : "none");
    if (!reference_type(o,ft->base)) fputs("value",destination);
    else if (f->result_family) {
        fputs("owned ",destination); family_out(o,destination,f->result_family,portable);
        fprintf(destination," %s",f->result_nullable ? "nullable" : "nonnull");
    }
    else if (f->result_static) fprintf(destination,"static %s",f->result_nullable ? "nullable" : "nonnull");
    else if (f->result_from >= 0) {
        size_t input; Range result;
        if (!result_description(o,f,&input,&result)) return false;
        if (view(o,ft->base) && view(o,contract_input_type(f,input)))
            fprintf(destination,"whole-viewed %zu %s",input,reference_read_only(o,ft->base) ? "read" : "write");
        else if (view(o,ft->base)) fprintf(destination,"viewed %zu %llu %llu %s",input,result.first,result.count,
            reference_read_only(o,ft->base) ? "read" : "write");
        else fprintf(destination,"borrowed %zu %llu %llu %s %s",input,result.first,result.count,
            reference_read_only(o,ft->base) ? "read" : "write",f->result_nullable ? "nullable" : "nonnull");
    } else if (portable && f->unchecked) fputs("unadmitted",destination);
    else return fail(o,f->body,"pointer return requires an admitted lifetime contract");
    fputs(") (fields",destination);
    for (size_t i = 0; i < f->nposts; ++i) {
        FieldPost *post = &f->posts[i];
        fprintf(destination," (field %zu (",f->entity->type->nparameters+post->part);
        if (post->source == -2) fprintf(destination,"static %s",post->field->nullable ? "nullable" : "nonnull");
        else if (view(o,post->field->type)) fprintf(destination,"whole-viewed %d %s",post->source,post->field->write ? "write" : "read");
        else fprintf(destination,"borrowed %d 0 %llu %s %s",post->source,
            ce_type_size(o->sema,post->source < (int)ft->nparameters ? ft->parameters[post->source]->base :
                         f->fields[post->source-ft->nparameters].field->type->base),
            post->field->write ? "write" : "read",post->field->nullable ? "nullable" : "nonnull");
        fputs("))",destination);
    }
    fputc(')',destination);
    if (f->nresults) {
        fputs(" (records",destination);
        for (size_t i = 0; i < f->nresults; ++i) {
            FieldPost *post = &f->results[i];
            fprintf(destination," (field %llu (",post->offset);
            if (post->source == -3) {
                fputs("owned ",destination); family_out(o,destination,post->field->family,portable);
                fprintf(destination," %s",post->field->nullable ? "nullable" : "nonnull");
            }
            else if (post->source == -2) fprintf(destination,"static %s",post->field->nullable ? "nullable" : "nonnull");
            else if (view(o,post->field->type)) fprintf(destination,"whole-viewed %d %s",post->source,post->field->write ? "write" : "read");
            else fprintf(destination,"borrowed %d 0 %llu %s %s",post->source,
                ce_type_size(o->sema,post->source < (int)ft->nparameters ? ft->parameters[post->source]->base :
                             f->fields[post->source-ft->nparameters].field->type->base),
                post->field->write ? "write" : "read",post->field->nullable ? "nullable" : "nonnull");
            fputs("))",destination);
        }
        fputc(')',destination);
    }
    fputc(')',destination);
    return true;
}

static bool write_signature(Ownership *o, FILE *destination, Function *f) {
    return write_signature_as(o,destination,f,false);
}

static const CeAtom *function_signature(Ownership *o, Function *f) {
    if (f->portable_signature) return f->portable_signature;
    char *buffer = NULL; size_t length = 0;
    FILE *file = open_memstream(&buffer,&length);
    if (!file) return NULL;
    bool ok = write_signature_as(o,file,f,true);
    ok = fclose(file) == 0 && ok;
    CeText text = ok ? ce_text(o->arena,(unsigned char *)buffer,length) : (CeText){0};
    free(buffer);
    CeAtom *parsed = NULL; CeError local = {{0},0};
    if (!ok || !ce_atom_read(o->arena,text,&parsed,&local)) return NULL;
    f->portable_signature = parsed;
    return parsed;
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

static void abi_text(FILE *file, CeText text) {
    CeAtom atom = {.kind=CE_ATOM_TEXT,.text=text}; ce_atom_write(&atom,file);
}

static void abi_type(Ownership *o, FILE *file, const CeType *type) {
    char *name = ce_type_text(o->sema,type,0);
    fputs("(type ",file); abi_text(file,ce_cstr(o->arena,name ? name : "anonymous"));
    fprintf(file," %llu %u)",ce_type_size(o->sema,type),ce_type_align(o->sema,type));
    free(name);
}

/* A length-delimited input to the single ABI/source fingerprint. */
static bool abi_chunk(CeDigest *digest, CeText text) {
    unsigned char length[8]; uint64_t n = text.len;
    for (unsigned i = 0; i < 8; ++i) length[i] = (unsigned char)(n>>(8*i));
    return ce_digest_update(digest,length,sizeof length) && ce_digest_update(digest,text.data,text.len);
}

/* The preprocessed unit without its layout. The readable rendering and the
   line-mapped rendering of one unit differ only in the whitespace between
   tokens, and a pin taken from either must hold for both. Literals keep
   their bytes; a directive keeps the line end that terminates it. */
static CeText abi_tokens(Ownership *o, CeText text) {
    unsigned char *out = ce_alloc(o->arena,text.len+1); size_t n = 0;
    bool gap = false, start = true, directive = false; unsigned char quote = 0;
    for (size_t i = 0; i < text.len; ++i) {
        unsigned char c = text.data[i];
        if (quote) {
            out[n++] = c;
            if (c == '\\' && i+1 < text.len) out[n++] = text.data[++i];
            else if (c == quote || c == '\n') quote = 0;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            if (c == '\n') {
                if (directive) { out[n++] = '\n'; directive = false; gap = false; start = true; continue; }
                start = true;
            }
            gap = true; continue;
        }
        if (gap && n && out[n-1] != '\n') out[n++] = ' ';
        gap = false;
        if (start) { directive = c == '#'; start = false; }
        if (c == '"' || c == '\'') quote = c;
        out[n++] = c;
    }
    return (CeText){out,n};
}

static bool abi_record(Ownership *o) {
    if (!o->plan && !o->expected_abi.len) return true;
    char *layout = NULL; size_t length = 0;
    FILE *file = open_memstream(&layout,&length);
    if (!file) return malformed(o,o->declaration,"cannot allocate ABI record");
    fputs("(layout (target ",file); abi_text(file,ce_cstr(o->arena,o->sema->abi->name));
    fprintf(file,") (char-signed %u) (scalars",o->sema->abi->char_signed);
    for (unsigned k = CE_T_BOOL; k <= CE_T_BFLOAT16; ++k)
        fprintf(file," (%u %u %u %u)",k,o->sema->abi->size[k],o->sema->abi->align[k],o->sema->abi->precision[k]);
    fputs(") (compiler-layout",file);
    const char *macros[] = {"__BYTE_ORDER__","__SIZEOF_POINTER__","__SIZE_TYPE__",
        "__PTRDIFF_TYPE__","__WCHAR_TYPE__","__BIGGEST_ALIGNMENT__","__CHAR_UNSIGNED__"};
    for (size_t i = 0; i < sizeof macros/sizeof *macros; ++i) {
        char prefix[96]; snprintf(prefix,sizeof prefix,"#define %s ",macros[i]);
        const char *found = strstr((const char *)o->pp->predefined.data,prefix);
        CeText value = {0};
        if (found) {
            found += strlen(prefix); const char *end = strchr(found,'\n');
            value = ce_text(o->arena,found,end ? (size_t)(end-found) : strlen(found));
        }
        fputs(" (macro ",file); abi_text(file,ce_cstr(o->arena,macros[i]));
        fputc(' ',file); abi_text(file,value); fputc(')',file);
    }
    fputs(")\n",file);
    for (size_t i = 0; i < o->sema->nentities; ++i) {
        CeEntity *e = o->sema->entities[i];
        if ((e->kind != CE_ENTITY_STRUCT && e->kind != CE_ENTITY_UNION) || !e->record || !e->record->complete) continue;
        fputs(" (record ",file);
        CeText name = e->name.len ? e->name : e->naming_alias ? e->naming_alias->name : (CeText){0};
        abi_text(file,name); fprintf(file," %zu %llu %u",e->id,e->record->size,e->record->align);
        for (size_t j = 0; j < e->record->nmembers; ++j) {
            CeMember *m = &e->record->members[j];
            fputs(" (field ",file); abi_text(file,m->name);
            fprintf(file," %llu %u %u ",m->offset,m->bit_offset,m->bitfield ? m->bit_width : 0);
            abi_type(o,file,m->type); fputc(')',file);
        }
        fputs(")\n",file);
    }
    for (size_t i = 0; i < o->nfunctions; ++i) {
        Function *f = &o->functions[i]; CeType *type = f->entity->type;
        if (!f->checked && !f->foreign) continue;
        if (f->interface_callable) continue;
        fputs(" (callable ",file); abi_text(file,f->name); fputc(' ',file);
        abi_type(o,file,type->base); fprintf(file," (params %u",type->variadic);
        for (size_t j = 0; j < type->nparameters; ++j) { fputc(' ',file); abi_type(o,file,type->parameters[j]); }
        fputs("))\n",file);
    }
    for (size_t i = 0; i < o->nviews; ++i) {
        View *v = &o->views[i]; fputs(" (view ",file); abi_type(o,file,v->type);
        fprintf(file," %llu %llu %llu %u)\n",v->data->offset,v->length->offset,v->stride,v->write);
    }
    fputc(')',file);
    if (fclose(file)) { free(layout); return malformed(o,o->declaration,"cannot finish ABI record"); }
    CeDigest digest; ce_digest_init(&digest);
    bool ok = abi_chunk(&digest,ce_cstr(o->arena,"cesta-abi/2")) &&
              abi_chunk(&digest,(CeText){(unsigned char *)layout,length}) && abi_chunk(&digest,abi_tokens(o,o->pp->text));
    for (size_t i = 0; ok && i < o->pp->ndependencies; ++i) {
        const char *path = o->pp->dependencies[i].file;
        if (path[0] == '<' || ce_same_file(path,o->identity)) continue;
        CeText bytes;
        ok = ce_read_file(o->arena,path,&bytes,o->error) && abi_chunk(&digest,bytes);
    }
    for (size_t i = 0; ok && i < o->ninterfaces; ++i) ok = abi_chunk(&digest,o->interfaces[i].text);
    char hex[65]; ce_digest_finish(&digest,hex);
    if (ok && o->expected_abi.len && memcmp(hex,o->expected_abi.data,64))
        ok = malformed(o,o->declaration,"ABI fingerprint differs: header, target, layout or native source changed");
    if (ok && o->plan) {
        fprintf(o->plan," (abi \"cesta-abi/2\" (sha256 \"%s\")\n ",hex);
        fwrite(layout,1,length,o->plan); fputs(")\n",o->plan);
    }
    free(layout);
    return ok || (o->error->message[0] ? false : malformed(o,o->declaration,"ABI fingerprint input exceeds SHA-256 extent"));
}

static bool has_post_test(Ownership *o, const CeTree *tree) {
    if (!tree) return false;
    if (kind(o,tree)==CE_K_DoWhile) return true;
    for (size_t i=0; i<tree->len; ++i) if (has_post_test(o,tree->children[i])) return true;
    return false;
}

/* Two items for one function state one contract when they have the same
   kind and the same parts, whether the name stands alone or in a group. */
static bool one_contract(const CeAtom *a, const CeAtom *b) {
    if (a->len != b->len || !ce_atom_equal(a->items[0],b->items[0])) return false;
    for (size_t i = 2; i < a->len; ++i) if (!ce_atom_equal(a->items[i],b->items[i])) return false;
    return true;
}

static CeEntity *typedef_named(Ownership *, CeText);

static void function_init(Ownership *o, Function *f, CeEntity *entity) {
    *f = (Function){.name=entity->name,.entity=entity,.result_from=-1,.allocation_count=-1};
    size_t n = entity->type->nparameters;
    f->parameters = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->parameters));
    f->stores = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->stores));
    f->modes = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->modes));
    f->families = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->families));
    f->nullable = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->nullable));
    f->callbacks = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->callbacks));
    memset(f->callbacks,0,(n ? n : 1)*sizeof(*f->callbacks));
    memset(f->nullable,0,(n ? n : 1)*sizeof(*f->nullable));
    f->input_roots = ce_alloc(o->arena,(n ? n : 1)*sizeof(*f->input_roots));
    memset(f->input_roots,0,(n ? n : 1)*sizeof(*f->input_roots));
    f->global_roots = ce_alloc(o->arena,o->sema->nentities*sizeof(*f->global_roots));
    memset(f->global_roots,0,o->sema->nentities*sizeof(*f->global_roots));
    for (size_t j = 0; j < n; ++j) {
        f->stores[j] = -1;
        f->modes[j] = pointer(entity->type->parameters[j]) && !read_only(entity->type->parameters[j]);
        f->families[j] = 0;
    }
}

static bool check(Ownership *o, const CeTree *tree) {
    if (!check_pragmas(o,tree)) return false;
    if (kind(o,tree) == CE_K_GNUEmptyTranslationUnit) return malformed(o,o->declaration,"ownership declaration checked no function bodies");
    o->functions = ce_alloc(o->arena,(o->sema->nentities+o->declaration->len)*sizeof(*o->functions));
    o->callbacks = ce_alloc(o->arena,o->declaration->len*sizeof(*o->callbacks));
    for (size_t i = 1; i < o->declaration->len; ++i) {
        const CeAtom *spec = o->declaration->items[i];
        if (!head(spec,"callback")) continue;
        if (spec->len < 3 || spec->items[1]->kind != CE_ATOM_SYMBOL || callback_named(o,spec->items[1]->text))
            return malformed(o,spec,"callback requires a distinct name and a field or nonempty targets");
        const CeAtom *selector = spec->items[2];
        bool field = head(selector,"field");
        CeEntity *anchor = NULL; CeType *type = NULL;
        if (field) {
            if (selector->len != 3 || selector->items[1]->kind != CE_ATOM_SYMBOL || selector->items[2]->kind != CE_ATOM_SYMBOL)
                return malformed(o,selector,"callback field expects a structure typedef and member");
            anchor = typedef_named(o,selector->items[1]->text);
            if (imported(o,i) && (!anchor || (anchor->type->kind == CE_T_STRUCT &&
                (!anchor->type->tag || !anchor->type->tag->record || !anchor->type->tag->record->complete)))) continue;
            if (!anchor || anchor->type->kind != CE_T_STRUCT || !anchor->type->tag || !anchor->type->tag->record ||
                !anchor->type->tag->record->complete)
                return malformed(o,selector,"callback field requires a complete structure typedef");
            for (size_t j = 0; j < anchor->type->tag->record->nmembers; ++j) {
                CeMember *member = &anchor->type->tag->record->members[j];
                if (same(member->name,selector->items[2]->text)) type = member->type;
            }
        } else {
            if (spec->len != 3 || !head(selector,"targets") || selector->len < 2)
                return malformed(o,spec,"callback requires a function-pointer typedef and nonempty targets");
            anchor = typedef_named(o,spec->items[1]->text);
            type = anchor ? anchor->type : NULL;
            if (!type && imported(o,i)) continue;
        }
        if (!pointer(type) || type->base->kind != CE_T_FUNCTION || !type->base->prototype || type->base->variadic)
            return malformed(o,spec,"callback requires a nonvariadic function-pointer prototype typedef");
        o->callbacks[o->ncallbacks++] = (Callback){.name=spec->items[1]->text,.type=type,.spec=spec,.anchor=anchor};
    }
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
        if (!type && imported(o,i)) continue;
        if (!type || type->kind != CE_T_STRUCT || !type->tag || !type->tag->record ||
            !type->tag->record->complete || type->tag->record->flexible || capability(o,type))
            return malformed(o,spec,"capability requires a distinct complete structure typedef");
        unsigned brand = family(o,spec->items[2]->text);
        if (capability_family(o,brand)) return malformed(o,spec,"distinct capability types require distinct authority families");
        o->capabilities[o->ncapabilities++] = (Capability){type,brand};
    }
    o->views = ce_alloc(o->arena,o->declaration->len*sizeof(*o->views));
    for (size_t i = 1; i < o->declaration->len; ++i) {
        const CeAtom *spec = o->declaration->items[i];
        if (!head(spec,"view")) continue;
        if ((spec->len < 5 || spec->len > 7) || spec->items[1]->kind != CE_ATOM_SYMBOL ||
            spec->items[2]->kind != CE_ATOM_SYMBOL || spec->items[3]->kind != CE_ATOM_SYMBOL ||
            (!symbol(spec->items[4],"read") && !symbol(spec->items[4],"write")))
            return malformed(o,spec,"view requires a type, data field, length field and read/write mode");
        CeType *type = NULL;
        for (size_t j = 0; j < o->sema->nentities; ++j) {
            CeEntity *e = o->sema->entities[j];
            if (e->kind == CE_ENTITY_TYPEDEF && same(e->name,spec->items[1]->text)) type = e->type;
        }
        if (!type && imported(o,i)) continue;
        if (!type || type->kind != CE_T_STRUCT || !type->tag || !type->tag->record ||
            type->tag->record->nmembers != 2 || type->tag->record->flexible || view(o,type) || record_spec(o,type) || capability(o,type))
            return malformed(o,spec,"view requires a distinct, complete two-field structure typedef");
        View *v = &o->views[o->nviews]; *v = (View){.type=type,.write=symbol(spec->items[4],"write"),.stride=1};
        const CeAtom *element = spec->len >= 6 && spec->items[5]->kind == CE_ATOM_SYMBOL ? spec->items[5] : NULL;
        size_t owned_index = element ? 6 : 5;
        if (spec->len > owned_index + 1) return malformed(o,spec,"view has too many clauses");
        if (spec->len > owned_index) {
            const CeAtom *owned = spec->items[owned_index];
            if (!v->write || !head(owned,"owns") || owned->len != 2 || owned->items[1]->kind != CE_ATOM_SYMBOL)
                return malformed(o,spec,"owning view requires write mode and an allocation family");
            v->family = family(o,owned->items[1]->text);
        }
        for (size_t j = 0; j < 2; ++j) {
            CeMember *m = &type->tag->record->members[j];
            if (m->bitfield || m->anonymous) return malformed(o,spec,"view fields must be ordinary named fields");
            if (m->type->qualifiers & (CE_Q_VOLATILE | CE_Q_ATOMIC))
                return malformed(o,spec,"volatile and atomic view metadata require a native resource contract");
            if (same(m->name,spec->items[2]->text)) v->data = m;
            if (same(m->name,spec->items[3]->text)) v->length = m;
        }
        if (!v->data || !v->length || v->data == v->length || !pointer(v->data->type) ||
            v->write == read_only(v->data->type) || !ce_type_integer(v->length->type) ||
            ce_kind_signed(o->sema->abi,ce_type_integer_kind(v->length->type)) ||
            ce_type_size(o->sema,v->length->type) != o->sema->abi->size[o->sema->abi->size_type])
            return malformed(o,spec,"view needs a scalar pointer of the declared mode and an unsigned size field");
        CeType *pointee = v->data->type->base;
        if (element) {
            if (symbol(element,"usize")) v->stride = o->sema->abi->size[o->sema->abi->size_type];
            else if (symbol(element,"bool") || symbol(element,"u8")) v->stride = 1;
            else if (symbol(element,"u16")) v->stride = 2;
            else if (symbol(element,"u32")) v->stride = 4;
            else if (symbol(element,"u64")) v->stride = 8;
            else return malformed(o,element,"view element must be bool, u8, u16, u32, u64 or usize");
            if (!ce_type_integer(pointee) || pointee->kind == CE_T_ENUM ||
                ce_kind_signed(o->sema->abi,ce_type_integer_kind(pointee)) ||
                (symbol(element,"bool") != (pointee->kind == CE_T_BOOL)) ||
                (symbol(element,"usize") && pointee->kind != o->sema->abi->size_type))
                return malformed(o,element,"view element disagrees with its C pointer type");
        }
        if (ce_type_size(o->sema,pointee) != v->stride)
            return malformed(o,spec,"view element width disagrees with its C pointer type");
        ++o->nviews;
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
        /* A unit that sees the type only as an incomplete structure cannot
           reach its fields, so the field contracts have nothing to govern. */
        if (imported(o,i) && (!type || (type->kind == CE_T_STRUCT && type->tag &&
                                        (!type->tag->record || !type->tag->record->complete)))) continue;
        if (!type || type->kind != CE_T_STRUCT || !type->tag || !type->tag->record ||
            !type->tag->record->complete || type->tag->record->flexible || record_spec(o,type) || view(o,type) || capability(o,type))
            return malformed(o,spec,"record requires a distinct complete structure typedef");
        RecordSpec *r = &o->records[o->nrecords++];
        *r = (RecordSpec){.type=type,.len=spec->len-2,
            .fields=ce_alloc(o->arena,(spec->len-2+type->tag->record->nmembers)*sizeof(FieldSpec))};
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
            if (head(mode,"callback") && (mode->len == 2 || (mode->len == 3 && symbol(mode->items[2],"nullable")))) {
                f->callback = mode->items[1]->kind == CE_ATOM_SYMBOL ? callback_named(o,mode->items[1]->text) : NULL;
                if (!f->callback || !ce_type_compatible(o->sema,f->type,f->callback->type))
                    return malformed(o,mode,"callback field has no matching callable contract");
                f->is_static = true; f->nullable = mode->len == 3;
            } else if (head(mode,"static") && (mode->len == 1 || (mode->len == 2 && symbol(mode->items[1],"nullable")))) {
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
            View *v = view(o,m->type);
            if (v) {
                if (v->family) return malformed(o,spec,"owning nested views need an allocation-result contract");
                r->fields[r->len++] = (FieldSpec){.name=m->name,.type=m->type,.write=v->write};
            }
        }
    }
    for (size_t i = 0; i < o->sema->nentities; ++i) {
        CeEntity *e = o->sema->entities[i]; if (e->kind != CE_ENTITY_FUNCTION) continue;
        Function *f = &o->functions[o->nfunctions++];
        function_init(o,f,e);
    }
    const CeTree *items = tree->children[0];
    for (size_t i = 0; i < items->len; ++i) if (kind(o,items->children[i]) == CE_K_ExternalFunction) {
        const CeTree *def = items->children[i]->children[0];
        while (kind(o,def) == CE_K_GNUFunctionExtension) def = def->children[0];
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
        if (head(spec,"view") || head(spec,"record") || head(spec,"capability") || head(spec,"callback")) continue;
        if (head(spec,"check-all") && spec->len == 1) { if (all) return malformed(o,spec,"duplicate check-all"); all = true; continue; }
        if (head(spec,"abi-fingerprint")) {
            if (o->expected_abi.len || spec->len != 2 || spec->items[1]->kind != CE_ATOM_TEXT || spec->items[1]->text.len != 64)
                return malformed(o,spec,"abi-fingerprint requires one SHA-256 hexadecimal string");
            o->expected_abi = spec->items[1]->text;
            for (size_t k = 0; k < 64; ++k) if (!strchr("0123456789abcdef",o->expected_abi.data[k]) || !o->expected_abi.data[k])
                return malformed(o,spec,"abi-fingerprint requires lowercase SHA-256 hexadecimal");
            continue;
        }
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
            if (imported(o,i) && (!alloc || !free)) continue;
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
        /* (unchecked (NAME...) CONTRACT... (obligation "...")) names a group
           of definitions sharing one contract and obligation. */
        if (head(spec,"unchecked") && spec->len >= 2 && spec->items[1]->kind == CE_ATOM_LIST) {
            const CeAtom *names = spec->items[1];
            if (!names->len) return malformed(o,spec,"unchecked group names no function");
            for (size_t j = 0; j < names->len; ++j) {
                if (names->items[j]->kind != CE_ATOM_SYMBOL)
                    return malformed(o,names->items[j],"unchecked group expects function names");
                Function *f = function_named(o,names->items[j]->text);
                /* From an interface, a unit that only declares the name
                   calls it under the same contract and obligation. */
                if (!f || (!f->body && !imported(o,i))) continue;
                if (f->spec && one_contract(f->spec,spec)) continue;
                if (f->spec) return malformed(o,spec,"duplicate function ownership contract");
                f->foreign = true; f->unchecked = f->body != NULL; f->checked = false;
                if (!signature(o,f,spec)) return false;
            }
            continue;
        }
        if ((!head(spec,"function") && !head(spec,"foreign") && !head(spec,"unchecked")) || spec->len < 2 ||
            spec->items[1]->kind != CE_ATOM_SYMBOL)
            return malformed(o,spec,"expected check-all, function, foreign or unchecked declaration");
        Function *f = function_named(o,spec->items[1]->text);
        /* An interface speaks for every unit that reads its header; a name
           this unit does not declare is not this unit's concern. */
        if (!f && imported(o,i)) continue;
        /* An unchecked name absent from this preprocessing configuration
           excludes nothing; a misspelt name leaves its body checked. */
        if (head(spec,"unchecked") && (!f || (!f->body && !imported(o,i)))) continue;
        if (!f) return malformed(o,spec,"function contract has no C declaration");
        if (f->spec && one_contract(f->spec,spec)) continue;
        if (f->spec) return malformed(o,spec,"duplicate function ownership contract");
        if (imported(o,i) && !f->body && !head(spec,"foreign")) {
            /* The definition is in another unit, held to this same text.
               Here the function is called under the contract. */
            bool stated = false;
            for (size_t k = 2; k < spec->len; ++k) stated |= head(spec->items[k],"obligation");
            for (size_t k = 0; !stated && k < o->ninterfaces; ++k) if (atom_within(o->interfaces[k].root,spec)) {
                char text[320];
                int n = snprintf(text,sizeof(text),"Its definition meets the contract stated in %s.",o->interfaces[k].name);
                f->obligation = ce_text(o->arena,(const unsigned char *)text,(size_t)n);
            }
            f->foreign = true;
            if (!signature(o,f,spec)) return false;
            continue;
        }
        /* An unchecked definition keeps its body out of the check. Its callers
           are checked against its declared contract, as for a foreign call. */
        f->unchecked = head(spec,"unchecked");
        f->foreign = head(spec,"foreign") || f->unchecked;
        if (f->unchecked) f->checked = false;
        else if (!f->foreign) f->checked = true;
        if (!signature(o,f,spec)) return false;
    }
    if (!all) return malformed(o,o->declaration,"ownership currently requires check-all; no unmarked body is silently trusted");
    for (size_t i = 0; i < o->ncallbacks; ++i) {
        Callback *callback = &o->callbacks[i];
        if (!head(callback->spec->items[2],"field")) continue;
        char *name = ce_alloc(o->arena,callback->name.len+11);
        memcpy(name,"@callback/",10); memcpy(name+10,callback->name.data,callback->name.len);
        name[callback->name.len+10] = 0;
        CeEntity *entity = ce_alloc(o->arena,sizeof(*entity));
        *entity = (CeEntity){.kind=CE_ENTITY_FUNCTION,.scope=CE_SCOPE_FILE,.linkage=CE_LINKAGE_EXTERNAL,
                            .name={(unsigned char *)name,callback->name.len+10},.type=callback->type->base};
        Function *prototype = &o->functions[o->nfunctions++]; function_init(o,prototype,entity);
        prototype->foreign = true; prototype->interface_callable = true;
        prototype->obligation = ce_cstr(o->arena,"The entry-supplied callback satisfies its callable interface; borrowed arguments escape only through declared postconditions.");
        CeAtom *spec = ce_atom_new(o->arena,CE_ATOM_LIST,(CeText){0},callback->spec->len-1);
        spec->len = callback->spec->len-1; spec->offset = callback->spec->offset;
        spec->items[0] = ce_atom_new(o->arena,CE_ATOM_SYMBOL,ce_cstr(o->arena,"foreign"),0);
        spec->items[1] = ce_atom_new(o->arena,CE_ATOM_SYMBOL,entity->name,0);
        for (size_t j = 3; j < callback->spec->len; ++j) spec->items[j-1] = callback->spec->items[j];
        if (!signature(o,prototype,spec)) return false;
        callback->prototype = prototype;
    }
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
            if ((record_spec(o,object) || (object->kind == CE_T_ARRAY && reference_record(object,0))) &&
                !record_parts(o,f,j,object,0,!pointer(type) || !read_only(type))) return false;
        }
        for (size_t j = 0; j < f->nposts; ++j) {
            bool found = false;
            for (size_t k = 0; k < f->nfields; ++k)
                if (f->fields[k].parameter == f->posts[j].parameter && f->fields[k].offset == f->posts[j].offset &&
                    f->fields[k].field == f->posts[j].field) {
                    f->posts[j].part = k; found = true; break;
                }
            if (!found) return fail(o,f->body,"field postcondition is absent from the parameter interface");
            if (!resolve_post_origin(o,f,&f->posts[j])) return false;
        }
        for (size_t j = 0; j < f->nresults; ++j)
            if (!resolve_post_origin(o,f,&f->results[j])) return false;
        if (f->nresults && !complete_record_result(o,f,f->entity->type->base,0)) return false;
        for (size_t j = 0; f->record_posts && j < f->entity->type->nparameters; ++j) if (f->record_posts[j]) {
            size_t count = 0;
            for (size_t k = 0; k < f->nfields; ++k) if (f->fields[k].parameter == j) {
                ++count; bool found = false;
                for (size_t p = 0; p < f->nposts; ++p) found |= f->posts[p].part == k;
                if (!found) return fail(o,f->body,"stores-record must specify every reference field");
            }
            if (!count) return fail(o,f->body,"stores-record parameter contains no reference fields");
        }
    }
    if (!global_effects(o)) return false;
    for (size_t i = 0; i < o->ncallbacks; ++i) {
        Callback *c = &o->callbacks[i]; const CeAtom *targets = c->spec->items[2];
        if (c->prototype) continue;
        c->len = targets->len-1; c->targets = ce_alloc(o->arena,c->len*sizeof(*c->targets));
        for (size_t j = 0; j < c->len; ++j) {
            Function *f = targets->items[j+1]->kind == CE_ATOM_SYMBOL ? function_named(o,targets->items[j+1]->text) : NULL;
            if (!f || (!f->checked && !f->foreign) || f->unchecked ||
                !f->entity->type->prototype || f->entity->type->variadic ||
                !ce_type_compatible(o->sema,c->type->base,f->entity->type))
                return malformed(o,c->spec,"callback target requires a checked body or explicit foreign contract with the exact prototype");
            for (size_t k = 0; k < j; ++k) if (c->targets[k] == f)
                return malformed(o,c->spec,"duplicate callback target");
            if (f->global_effects) for (size_t k = 0; k < o->sema->nentities; ++k)
                if (f->global_effects[k]) return malformed(o,c->spec,"callback effects must use explicit context, not owned globals");
            if (j) {
                /* The same contract, modulo order and explanatory obligations.
                   C-compatible prototypes alone do not establish lifetimes. */
                const CeAtom *specs[] = {f->spec,c->targets[0]->spec};
                for (size_t direction = 0; direction < 2; ++direction) {
                    const CeAtom *a = specs[direction], *b = specs[1-direction];
                    for (size_t k = 2; a && k < a->len; ++k) {
                        if (head(a->items[k],"obligation")) continue;
                        bool found = false;
                        for (size_t l = 2; b && l < b->len; ++l) found |= ce_atom_equal(a->items[k],b->items[l]);
                        if (!found) return malformed(o,c->spec,"callback targets disagree on their ownership contracts");
                    }
                }
            }
            c->targets[j] = f;
        }
    }
    bool whole_units = false;
    for (size_t i = 0; i < o->nfunctions; ++i) {
        Function *f = &o->functions[i];
        for (size_t j = 0; j < f->entity->type->nparameters; ++j)
            whole_units |= view(o,f->entity->type->parameters[j]) && (f->checked || f->result_from == (int)j);
        for (size_t j = 0; j < f->nfields; ++j) whole_units |= view(o,f->fields[j].field->type) != NULL;
        for (size_t j = 0; j < f->nresults; ++j) whole_units |= view(o,f->results[j].field->type) != NULL;
    }
    bool post_test=false;
    for (size_t i=0; i<o->nfunctions; ++i)
        post_test |= o->functions[i].checked && has_post_test(o,o->functions[i].body);
    bool interfaces = false, closed_callbacks = false;
    for (size_t i = 0; i < o->ncallbacks; ++i) {
        interfaces |= o->callbacks[i].prototype != NULL;
        closed_callbacks |= o->callbacks[i].prototype == NULL;
    }
    if (o->plan) fprintf(o->plan,"(Ownership \"cesta-ownership/%u\"\n",interfaces ? 7u : post_test ? 6u : o->ncallbacks ? 5u : whole_units ? 4u : 3u);
    if (!abi_record(o)) return false;
    if (o->plan && closed_callbacks) {
        fputs(" (callbacks",o->plan);
        for (size_t i = 0; i < o->ncallbacks; ++i) {
            Callback *c = &o->callbacks[i];
            if (c->prototype) continue;
            fprintf(o->plan," (%.*s",(int)c->name.len,c->name.data);
            for (size_t j = 0; j < c->len; ++j)
                fprintf(o->plan," %.*s",(int)c->targets[j]->name.len,c->targets[j]->name.data);
            fputc(')',o->plan);
        }
        fputs(")\n",o->plan);
    }
    if (o->plan && interfaces) {
        fputs(" (callback-interfaces",o->plan);
        for (size_t i = 0; i < o->ncallbacks; ++i) {
            Callback *c = &o->callbacks[i];
            if (c->prototype) fprintf(o->plan," (%.*s %.*s)",(int)c->name.len,c->name.data,
                                     (int)c->prototype->name.len,c->prototype->name.data);
        }
        fputs(")\n",o->plan);
    }
    if (o->plan) for (size_t i = 0; i < o->nfunctions; ++i) if (o->functions[i].foreign) {
        Function *f = &o->functions[i];
        if (f->unchecked && reference_type(o,f->entity->type->base) && !f->result_static &&
            f->result_from < 0 && !f->result_family) continue;  /* no checked call can use it */
        fprintf(o->plan," (foreign %.*s",(int)f->name.len,f->name.data);
        if (!write_signature(o,o->plan,f)) return false;
        fputs(" (obligation ",o->plan);
        CeAtom text = {.kind=CE_ATOM_TEXT,.text=f->obligation}; ce_atom_write(&text,o->plan);
        fputs("))\n",o->plan);
    }
    /* A survey records the first refusal of every function body instead of
       stopping at the first one. It never admits the unit. */
    const char *survey = getenv("CESTA_OWNERSHIP_SURVEY");
    size_t checked = 0, refused = 0;
    for (size_t i = 0; i < o->nfunctions; ++i) if (o->functions[i].checked) {
        Function *f = &o->functions[i];
        if (!f->body) return malformed(o,f->spec,"checked function has no body");
        if (!check_function(o,f)) {
            if (!survey || !*survey) return false;
            FILE *record = fopen(survey,"a");
            if (!record) return ce_error(o->error,o->declaration->offset,"cannot append the ownership survey %s",survey);
            fprintf(record,"%s\t%.*s\t%s\n",o->identity,(int)f->name.len,f->name.data,o->error->message);
            if (fclose(record)) return ce_error(o->error,o->declaration->offset,"cannot write the ownership survey %s",survey);
            o->function = NULL; o->loops = o->loop_scope = o->switches = o->switch_scope = 0;
            o->loop_entry = o->loop_exits = o->loop_latch = o->switch_entry = NULL; o->loop_step = NULL; o->switch_exit = NULL;
            ++refused;
            continue;
        }
        ++checked;
    }
    if (refused)
        return ce_error(o->error,o->declaration->offset,"ownership survey: %zu of %zu function bodies refused; see %s",
                        refused,refused+checked,survey);
    size_t named = 0;
    for (size_t i = 0; i < o->nfunctions; ++i) named += o->functions[i].unchecked;
    if (!checked && !named) return malformed(o,o->declaration,"ownership declaration checked no function bodies");
    if (o->plan && interfaces) {
        bool any = false;
        for (size_t i = 0; i < o->ncallbacks; ++i) any |= o->callbacks[i].nimplementations != 0;
        if (any) {
            fputs(" (callback-implementations",o->plan);
            for (size_t i = 0; i < o->ncallbacks; ++i) {
                Callback *c = &o->callbacks[i];
                for (size_t j = 0; j < c->nimplementations; ++j)
                    fprintf(o->plan," (%.*s %.*s)",(int)c->name.len,c->name.data,
                            (int)c->implementations[j]->name.len,c->implementations[j]->name.data);
            }
            fputs(")\n",o->plan);
        }
    }
    if (o->plan) fputs(")\n",o->plan);
    return true;
}

/* ---- The unit's link record ----------------------------------------------
   Units checked apart compose when each call one unit checks against a
   contract reaches a definition checked under that same contract. The record
   names the functions the unit defines and those its checked bodies call in
   other units. Each carries its contract in a form that does not depend on
   the unit: the portable signature, the C function type written
   structurally, the shared contract of every callback it passes, and the
   layout of every complete record its types reach. */

typedef struct { const CeEntity **tags; size_t len; bool *seen; } LinkRecords;
typedef struct { Callback **items; size_t len; } LinkCallbacks;

static const char *const link_kinds[CE_T_KINDS] = {
    [CE_T_VOID]="void",[CE_T_BOOL]="bool",[CE_T_CHAR]="char",[CE_T_SCHAR]="schar",[CE_T_UCHAR]="uchar",
    [CE_T_SHORT]="short",[CE_T_USHORT]="ushort",[CE_T_INT]="int",[CE_T_UINT]="uint",[CE_T_LONG]="long",
    [CE_T_ULONG]="ulong",[CE_T_LLONG]="llong",[CE_T_ULLONG]="ullong",[CE_T_INT128]="int128",
    [CE_T_UINT128]="uint128",[CE_T_FLOAT]="float",[CE_T_DOUBLE]="double",[CE_T_LDOUBLE]="ldouble",
    [CE_T_FLOAT32]="float32",[CE_T_FLOAT64]="float64",[CE_T_FLOAT128]="float128",[CE_T_FLOAT32X]="float32x",
    [CE_T_FLOAT64X]="float64x",[CE_T_FLOAT16]="float16",[CE_T_BFLOAT16]="bfloat16"};

static void link_type(Ownership *o, FILE *file, const CeType *t, bool parameter, LinkRecords *records, unsigned depth);

static void link_layout(Ownership *o, FILE *file, const CeEntity *tag, LinkRecords *records, unsigned depth) {
    fprintf(file,"%llu %u",tag->record->size,tag->record->align);
    for (size_t i = 0; i < tag->record->nmembers; ++i) {
        const CeMember *m = &tag->record->members[i];
        fputs(" (field ",file); abi_text(file,m->name);
        fprintf(file," %llu %u %u ",m->offset,m->bit_offset,m->bitfield ? m->bit_width : 0);
        link_type(o,file,m->type,false,records,depth+1); fputc(')',file);
    }
}

/* A C type, compared across units as C compares types of one name in
   separate translation units: structures by tag and members, an enumeration
   as its compatible integer type, parameters without their top-level
   qualifiers. A named record is written by name; its layout is listed once. */
static void link_type(Ownership *o, FILE *file, const CeType *t, bool parameter, LinkRecords *records, unsigned depth) {
    if (!t || depth > 64) { fputs("unknown",file); return; }
    static const char *const qualifier_names[] = {"const","volatile","restrict","atomic"};
    static const unsigned qualifier_bits[] = {CE_Q_CONST,CE_Q_VOLATILE,CE_Q_RESTRICT,CE_Q_ATOMIC};
    unsigned qualifiers = parameter ? 0 : t->qualifiers, wrapped = 0;
    for (size_t k = 0; k < 4; ++k) if (qualifiers & qualifier_bits[k]) { fprintf(file,"(%s ",qualifier_names[k]); ++wrapped; }
    if (t->kind == CE_T_POINTER) {
        fputs("(pointer ",file); link_type(o,file,t->base,false,records,depth+1); fputc(')',file);
    } else if (t->kind == CE_T_ARRAY) {
        fprintf(file,"(array %lld ",t->length); link_type(o,file,t->base,false,records,depth+1); fputc(')',file);
    } else if (t->kind == CE_T_COMPLEX) {
        fputs("(complex ",file); link_type(o,file,t->base,false,records,depth+1); fputc(')',file);
    } else if (t->kind == CE_T_FUNCTION) {
        fputs("(function ",file); link_type(o,file,t->base,false,records,depth+1); fputs(" (",file);
        for (size_t i = 0; i < t->nparameters; ++i) {
            if (i) fputc(' ',file);
            link_type(o,file,t->parameters[i],true,records,depth+1);
        }
        fprintf(file,") %s %s)",t->variadic ? "variadic" : "fixed",t->prototype ? "prototype" : "unprototyped");
    } else if (t->kind == CE_T_ENUM) {
        unsigned integer = ce_type_integer_kind(t);
        fputs(integer < CE_T_KINDS && link_kinds[integer] ? link_kinds[integer] : "enum",file);
    } else if (t->kind == CE_T_STRUCT || t->kind == CE_T_UNION) {
        const CeEntity *tag = t->tag;
        bool complete = tag && tag->record && tag->record->complete;
        fprintf(file,"(%s ",t->kind == CE_T_STRUCT ? "struct" : "union");
        if (tag && tag->name.len) {
            abi_text(file,tag->name);
            if (complete && tag->id <= o->sema->nentities && !records->seen[tag->id]) {
                records->seen[tag->id] = true; records->tags[records->len++] = tag;
            }
        } else {
            fputs("\"\"",file);
            if (complete) { fputc(' ',file); link_layout(o,file,tag,records,depth+1); }
        }
        fputc(')',file);
    } else fputs(t->kind < CE_T_KINDS && link_kinds[t->kind] ? link_kinds[t->kind] : "unknown",file);
    while (wrapped--) fputc(')',file);
}

static void link_callback_add(LinkCallbacks *list, Callback *callback) {
    if (!callback) return;
    for (size_t i = 0; i < list->len; ++i) if (list->items[i] == callback) return;
    list->items[list->len++] = callback;
}

typedef struct { const CeType **items; size_t len, capacity; } LinkTypes;

static void link_type_add(LinkTypes *list, const CeType *type) {
    if (!type) return;
    for (size_t i = 0; i < list->len; ++i) if (list->items[i] == type) return;
    if (list->len == list->capacity) {
        list->capacity = list->capacity ? 2*list->capacity : 16;
        list->items = ce_resize(list->items,list->capacity,sizeof(*list->items));
    }
    list->items[list->len++] = type;
}

static void link_callbacks_of(Ownership *o, LinkCallbacks *list, const Function *f) {
    if (!o->ncallbacks) return;
    for (size_t i = 0; i < f->entity->type->nparameters; ++i) link_callback_add(list,f->callbacks[i]);
    for (size_t i = 0; i < f->nfields; ++i) link_callback_add(list,f->fields[i].field->callback);
    /* Associated record contracts also travel with returned values and
       pointers. Walk the finite declaration graph, including recursive types. */
    LinkTypes types = {0}; link_type_add(&types,f->entity->type);
    for (size_t i = 0; i < types.len; ++i) {
        const CeType *type = types.items[i];
        if (pointer(type) || type->kind == CE_T_ARRAY) link_type_add(&types,type->base);
        if (type->kind == CE_T_FUNCTION) {
            link_type_add(&types,type->base);
            for (size_t j = 0; j < type->nparameters; ++j) link_type_add(&types,type->parameters[j]);
        }
        RecordSpec *record = record_spec(o,type);
        if (record) for (size_t j = 0; j < record->len; ++j) link_callback_add(list,record->fields[j].callback);
        if (ce_type_record(type) && type->tag && type->tag->record)
            for (size_t j = 0; j < type->tag->record->nmembers; ++j)
                link_type_add(&types,type->tag->record->members[j].type);
    }
    free(types.items);
}

static int callback_target_compare(const void *left, const void *right) {
    const Function *a = *(Function *const *)left, *b = *(Function *const *)right;
    size_t count = a->name.len < b->name.len ? a->name.len : b->name.len;
    int compared = memcmp(a->name.data,b->name.data,count);
    return compared ? compared : (a->name.len > b->name.len) - (a->name.len < b->name.len);
}

static bool link_contract(Ownership *o, FILE *file, Function *f) {
    LinkRecords records = {.tags=ce_alloc(o->arena,(o->sema->nentities+1)*sizeof(*records.tags)),
                           .seen=ce_alloc(o->arena,(o->sema->nentities+1)*sizeof(*records.seen))};
    LinkCallbacks callbacks = {.items=ce_alloc(o->arena,(o->ncallbacks+1)*sizeof(*callbacks.items))};
    fputs(" (contract",file);
    if (!write_signature_as(o,file,f,true)) return false;
    fputs(" (type ",file); link_type(o,file,f->entity->type,false,&records,0); fputc(')',file);
    /* A callback's targets share one contract; the callee relies on it. */
    link_callbacks_of(o,&callbacks,f);
    fputs(" (callbacks",file);
    for (size_t i = 0; i < callbacks.len; ++i) {
        Callback *c = callbacks.items[i]; Function *target = callback_prototype(c);
        fputs(" (callback ",file); abi_text(file,c->name);
        if (c->prototype) fputs(" (open)",file);
        else {
            Function **ordered = ce_alloc(o->arena,c->len*sizeof(*ordered));
            memcpy(ordered,c->targets,c->len*sizeof(*ordered));
            qsort(ordered,c->len,sizeof(*ordered),callback_target_compare);
            fputs(" (targets",file);
            for (size_t j = 0; j < c->len; ++j) {
                const Function *implementation = ordered[j];
                bool internal = implementation->entity->linkage == CE_LINKAGE_INTERNAL;
                fputs(internal ? " (internal " : " (external ",file);
                if (internal) { abi_text(file,ce_cstr(o->arena,o->identity)); fputc(' ',file); }
                abi_text(file,implementation->name); fputc(')',file);
            }
            fputc(')',file);
        }
        if (!write_signature_as(o,file,target,true)) return false;
        fputs(" (type ",file); link_type(o,file,target->entity->type,false,&records,0); fputs("))",file);
        link_callbacks_of(o,&callbacks,target);
    }
    fputs(") (records",file);
    for (size_t i = 0; i < records.len; ++i) {
        const CeEntity *tag = records.tags[i];
        fprintf(file," (%s ",tag->kind == CE_ENTITY_UNION ? "union" : "struct"); abi_text(file,tag->name);
        fputc(' ',file); link_layout(o,file,tag,&records,0); fputc(')',file);
    }
    fputs("))",file);
    return true;
}

static void link_at(Ownership *o, FILE *file, const CeTree *site) {
    const CePPOrigin *origin = site ? ce_pp_origin(o->pp,site->offset) : NULL;
    fputs(" (at ",file); abi_text(file,ce_cstr(o->arena,origin ? origin_file(origin) : o->identity));
    fprintf(file," %u)",origin ? origin_line(origin) : 0);
}

static void link_obligation(FILE *file, CeText text) {
    fputs(" (obligation ",file); abi_text(file,text); fputc(')',file);
}

/* (unit IDENTITY (abi NAME) (char-signed 0|1)
     (define NAME checked|unchecked external|internal (at FILE LINE) [(obligation TEXT)] [CONTRACT])
     (assume NAME (at FILE LINE) (obligation TEXT) CONTRACT))
   A checked or unchecked definition with external linkage carries its
   contract; an unchecked one with internal linkage is listed for the
   program's account of native bodies. An assumption is a function this unit
   only declares and its checked bodies call or name. */
static bool link_record(Ownership *o, FILE *file) {
    fputs("(unit ",file); abi_text(file,ce_cstr(o->arena,o->identity));
    fputs(" (abi ",file); abi_text(file,ce_cstr(o->arena,o->sema->abi->name));
    /* Plain char follows the compiler, which -funsigned-char can change. */
    bool unsigned_char = strstr((const char *)o->pp->predefined.data,"#define __CHAR_UNSIGNED__ 1\n") != NULL;
    fprintf(file,") (char-signed %u)\n",unsigned_char ? 0u : 1u);
    for (size_t i = 0; i < o->nfunctions; ++i) {
        Function *f = &o->functions[i];
        bool external = f->entity->linkage == CE_LINKAGE_EXTERNAL;
        if (f->body && (f->checked || f->unchecked) && (external || f->unchecked)) {
            fputs(" (define ",file); fwrite(f->name.data,1,f->name.len,file);
            fprintf(file," %s %s",f->unchecked ? "unchecked" : "checked",external ? "external" : "internal");
            link_at(o,file,f->entity->definition ? f->entity->definition : f->body);
            if (f->unchecked) link_obligation(file,f->obligation);
            if (external && !link_contract(o,file,f)) return false;
            fputs(")\n",file);
        } else if (!f->body && f->foreign && f->use && external) {
            fputs(" (assume ",file); fwrite(f->name.data,1,f->name.len,file);
            link_at(o,file,f->use);
            link_obligation(file,f->obligation);
            if (!link_contract(o,file,f)) return false;
            fputs(")\n",file);
        }
    }
    fputs(")\n",file);
    return true;
}

/* ---- An interface written from a unit ------------------------------------
   The contracts the unit gives what a header declares: the type contracts of
   its typedefs, an allocator whose functions it declares, and the contract of
   each function it declares that the unit defines, checked or unchecked, or
   states as foreign. A checked definition with no contract of its own has
   the default one, written (function NAME). A group of unchecked names is
   written name by name. */
static bool declared_in(Ownership *o, const CeEntity *e, const char *header) {
    const CePPOrigin *origin = e && e->declaration ? ce_pp_origin(o->pp,e->declaration->offset) : NULL;
    return origin && origin->file && ce_same_file(origin->file,header);
}

static CeEntity *typedef_named(Ownership *o, CeText name) {
    for (size_t j = 0; j < o->sema->nentities; ++j) {
        CeEntity *e = o->sema->entities[j];
        if (e->kind == CE_ENTITY_TYPEDEF && e->scope == CE_SCOPE_FILE && same(e->name,name)) return e;
    }
    return NULL;
}

static void interface_item(FILE *file, const CeAtom **written, size_t *count, const CeAtom *item) {
    for (size_t i = 0; i < *count; ++i) if (ce_atom_equal(written[i],item)) return;
    written[(*count)++] = item;
    fputs("  ",file); ce_atom_write(item,file); fputc('\n',file);
}

static void interface_items(Ownership *o, const char *header, FILE *file) {
    const CeAtom **written = ce_alloc(o->arena,(o->declaration->len+o->nfunctions+1)*sizeof(*written));
    size_t count = 0;
    fputs("(cesta ownership-interface/1\n",file);
    for (size_t i = 1; i < o->declaration->len; ++i) {
        const CeAtom *spec = o->declaration->items[i];
        bool typed = head(spec,"record") || head(spec,"view") || head(spec,"capability") || head(spec,"callback");
        bool field_callback = head(spec,"callback") && spec->len >= 3 && head(spec->items[2],"field");
        if (typed && !field_callback && spec->len >= 2 && spec->items[1]->kind == CE_ATOM_SYMBOL &&
            declared_in(o,typedef_named(o,spec->items[1]->text),header)) interface_item(file,written,&count,spec);
        if (field_callback) {
            Callback *callback = callback_named(o,spec->items[1]->text);
            if (callback && declared_in(o,callback->anchor,header)) interface_item(file,written,&count,spec);
        }
        if (head(spec,"allocator") && spec->len == 5 && spec->items[2]->kind == CE_ATOM_SYMBOL &&
            spec->items[3]->kind == CE_ATOM_SYMBOL) {
            Function *alloc = function_named(o,spec->items[2]->text), *release = function_named(o,spec->items[3]->text);
            if ((alloc && declared_in(o,alloc->entity,header)) || (release && declared_in(o,release->entity,header)))
                interface_item(file,written,&count,spec);
        }
    }
    for (size_t i = 0; i < o->nfunctions; ++i) {
        Function *f = &o->functions[i];
        if (f->interface_callable) continue;
        if (!declared_in(o,f->entity,header) || (f->entity->linkage == CE_LINKAGE_INTERNAL && !f->unchecked)) continue;
        if (f->spec && head(f->spec,"allocator")) continue;
        CeAtom *item = NULL;
        if (f->spec && head(f->spec,"unchecked") && f->spec->len >= 2 && f->spec->items[1]->kind == CE_ATOM_LIST) {
            item = ce_atom_new(o->arena,CE_ATOM_LIST,(CeText){0},f->spec->len);
            item->len = f->spec->len;
            item->items[0] = f->spec->items[0];
            item->items[1] = ce_atom_new(o->arena,CE_ATOM_SYMBOL,f->name,0);
            for (size_t k = 2; k < f->spec->len; ++k) item->items[k] = f->spec->items[k];
        } else if (f->spec) item = (CeAtom *)f->spec;
        else if (f->body && f->checked) {
            item = ce_atom_new(o->arena,CE_ATOM_LIST,(CeText){0},2); item->len = 2;
            item->items[0] = ce_atom_new(o->arena,CE_ATOM_SYMBOL,ce_cstr(o->arena,"function"),0);
            item->items[1] = ce_atom_new(o->arena,CE_ATOM_SYMBOL,f->name,0);
        }
        if (item) interface_item(file,written,&count,item);
    }
    fputs(")\n",file);
}

/* Reads NAME.ownership beside every header NAME the unit read, and appends
   their contracts to the unit's declaration. An item equal to one already
   present is one contract, not two. */
static bool read_interfaces(CeArena *arena, const CePPResult *pp, const char *identity, const CeAtom *declaration,
                            Interface **interfaces, size_t *count, const CeAtom **merged, CeError *error) {
    Interface *list = ce_alloc(arena,(pp->ndependencies+1)*sizeof(*list)); size_t n = 0, extra = 0;
    for (size_t i = 0; i < pp->ndependencies; ++i) {
        const char *path = pp->dependencies[i].file;
        if (path[0] == '<' || ce_same_file(path,identity)) continue;
        size_t length = strlen(path); char *beside = ce_alloc(arena,length+11);
        memcpy(beside,path,length); memcpy(beside+length,".ownership",11);
        FILE *probe = fopen(beside,"rb");
        if (!probe) continue;
        fclose(probe);
        bool seen = false;
        for (size_t k = 0; k < n; ++k) seen |= ce_same_file(list[k].path,beside);
        if (seen) continue;
        Interface *in = &list[n]; *in = (Interface){.path=beside};
        const char *slash = strrchr(beside,'/'); in->name = slash ? slash+1 : beside;
        CeAtom *root; CeError local = {{0},0};
        if (!ce_read_file(arena,beside,&in->text,&local))
            return ce_error(error,declaration->offset,"ownership: cannot read interface %s",in->name);
        if (!ce_atom_read(arena,in->text,&root,&local))
            return ce_error(error,declaration->offset,"ownership: %s (%s:%u)",local.message,in->name,text_line(in->text,local.offset));
        if (root->kind != CE_ATOM_LIST || root->len < 2 || !symbol(root->items[0],"cesta") ||
            !symbol(root->items[1],"ownership-interface/1"))
            return ce_error(error,declaration->offset,"ownership: expected (cesta ownership-interface/1 CONTRACT...) (%s:1)",in->name);
        for (size_t j = 2; j < root->len; ++j)
            if (head(root->items[j],"check-all") || head(root->items[j],"abi-fingerprint") || head(root->items[j],"global"))
                return ce_error(error,declaration->offset,
                    "ownership: an interface holds contracts; check-all, abi-fingerprint and global belong to a unit (%s:%u)",
                    in->name,text_line(in->text,root->items[j]->offset));
        in->root = root; extra += root->len-2; ++n;
    }
    *interfaces = list; *count = n; *merged = declaration;
    if (!n) return true;
    CeAtom *all = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},declaration->len+extra);
    all->offset = declaration->offset; all->len = declaration->len;
    for (size_t i = 0; i < declaration->len; ++i) all->items[i] = declaration->items[i];
    for (size_t i = 0; i < n; ++i) for (size_t j = 2; j < list[i].root->len; ++j) {
        bool present = false;
        for (size_t k = 1; k < all->len; ++k) present |= ce_atom_equal(all->items[k],list[i].root->items[j]);
        if (!present) all->items[all->len++] = list[i].root->items[j];
    }
    *merged = all;
    return true;
}

bool ce_ownership_check(CeArena *arena, const CeAtom *declaration, CeText text,
                        const char *identity, size_t argc, char **arguments,
                        FILE *plan, CeOwnershipResult *result, CeError *error) {
    if (result) *result = (CeOwnershipResult){.interface_of=result->interface_of};
    if (!identity) return ce_error(error,declaration->offset,"ownership requires a source identity and compiler environment");
    CePPResult pp = {0}; CeGrammar grammar = {0}; CeTokens tokens = {0};
    CeDFA dfa = {0}; CeLR lr = {0}; CeForest forest = {.root=-1}; CeSema sema = {0};
    bool sema_ready = false, ok = false, contract = false; CeTree *tree;
    char **args = ce_alloc(arena,(argc+6)*sizeof(*args)); size_t n = 0;
    bool standard = false;
    args[n++] = "--cc"; args[n++] = arguments[0];
    for (size_t i = 1; i < argc; ++i) {
        args[n++] = arguments[i];
        standard |= !strncmp(arguments[i],"-std=",5);
    }
    /* The native ownership profile reads C11; an explicit standard is kept. */
    if (!standard) args[n++] = "-std=c11";
    args[n++] = "-x"; args[n++] = "c"; args[n++] = (char *)identity;
    if (!ce_pp_run_source(ce_builtin_CPPGNUGrammar(),(int)n,args,text,false,&pp,error)) goto done;
    if (!ce_builtin_dialect(&grammar,true,pp.clang,error) || !ce_dfa_prepare(&grammar,&dfa,error) ||
        !ce_lr_prepare(&grammar,grammar.entries[0],&lr,error) || !ce_dfa_lex(&grammar,&dfa,pp.text,&tokens,error) ||
        !ce_lr_parse(&grammar,&lr,&tokens,&forest,error) || !ce_tree_from_forest(&grammar,&forest,&tokens,arena,&tree,error) ||
        !ce_sema_init(&sema,&grammar,pp.text,error)) goto done;
    sema_ready = true; sema.clang = pp.clang;
    sema.abi = ce_abi_predefined((const char *)pp.predefined.data);
    if (!sema.abi) { ce_error(error,declaration->offset,"ownership has no ABI model for the selected compiler target"); goto done; }
    if (!ce_bind(&sema,tree,error) || !ce_elaborate(&sema,tree,error)) goto done;
    Ownership o = {.arena=arena,.sema=&sema,.pp=&pp,.declaration=declaration,.identity=identity,.error=error,.plan=plan,
                   .unit_items=declaration->len};
    if (!read_interfaces(arena,&pp,identity,declaration,&o.interfaces,&o.ninterfaces,&o.declaration,error)) goto done;
    if (result && o.ninterfaces) {
        result->interfaces = ce_alloc(arena,o.ninterfaces*sizeof(*result->interfaces));
        result->ninterfaces = o.ninterfaces;
        for (size_t i = 0; i < o.ninterfaces; ++i) result->interfaces[i] = o.interfaces[i].path;
    }
    ok = check(&o,tree);
    contract = o.contract_error;
    if (ok && result && result->interface_of) {
        char *buffer = NULL; size_t length = 0; FILE *items = open_memstream(&buffer,&length);
        if (!items) ok = ce_error(error,declaration->offset,"ownership: cannot allocate the interface");
        else {
            interface_items(&o,result->interface_of,items);
            if (fclose(items)) ok = ce_error(error,declaration->offset,"ownership: cannot finish the interface");
            else result->interface = ce_text(arena,buffer,length);
            free(buffer);
        }
    }
    if (ok && result) {
        char *buffer = NULL; size_t length = 0; FILE *record = open_memstream(&buffer,&length);
        if (!record) ok = ce_error(error,declaration->offset,"ownership: cannot allocate the link record");
        else {
            ok = link_record(&o,record);
            if (fclose(record)) ok = ok && ce_error(error,declaration->offset,"ownership: cannot finish the link record");
            if (ok) result->contracts = ce_text(arena,buffer,length);
            free(buffer);
        }
    }
done:
    /* A contract error already names its own item; an analysis error names a
       place in the preprocessed C, reported by its source line. */
    if (!ok && !contract && error->offset != declaration->offset) {
        const CePPOrigin *origin = ce_pp_origin(&pp,error->offset);
        char message[sizeof(error->message)]; memcpy(message,error->message,sizeof(message));
        if (origin) ce_error(error,declaration->offset,"ownership C analysis at %s:%u: %s",origin_file(origin),origin_line(origin),message);
        else error->offset = declaration->offset;
    }
    if (sema_ready) ce_sema_free(&sema);
    ce_forest_free(&forest); ce_tokens_free(&tokens); ce_lr_free(&lr); ce_dfa_free(&dfa);
    ce_grammar_free(&grammar); ce_pp_result_free(&pp); return ok;
}

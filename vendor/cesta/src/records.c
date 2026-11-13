#define _POSIX_C_SOURCE 200809L
#include "records.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    CeText name, owner, callee, obligation;
    CeType *type;
    CeEntity *function;
    bool macro, address, write, counted;
} Policy;
typedef struct {
    const CeMember *member, *count, *when;
    size_t index;
    unsigned mode;               /* 0 value, 1 borrow, 2 own */
    bool write, nullable;
    CeText owner, family, keep;
    Policy *policy;
    const CeAtom *site;
} Field;
typedef struct {
    CeText name, emitted;
    CeType *type, *context;
    Field *fields;
    size_t nfields;
    bool macro, write;
} Record;
typedef struct {
    CeArena *arena;
    CeSema *sema;
    CeError *error;
    const char *path;
    CeText text;
    CeText *owners;
    size_t nowners;
    Policy *policies;
    size_t npolicies;
    Record *records;
    size_t nrecords;
} Schema;

static bool symbol(const CeAtom *a, const char *s) { return a && a->kind == CE_ATOM_SYMBOL && ce_is(a->text,s); }
static bool head(const CeAtom *a, const char *s) { return a && a->kind == CE_ATOM_LIST && a->len && symbol(a->items[0],s); }
static bool name(const CeAtom *a, CeText *out) {
    if (!a || (a->kind != CE_ATOM_TEXT && a->kind != CE_ATOM_SYMBOL) || !a->text.len) return false;
    *out = a->text; return true;
}
static bool identifier(CeText text) {
    for (size_t i = 0; i < text.len; ++i) {
        unsigned c = text.data[i];
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (i && c >= '0' && c <= '9'))) return false;
    }
    return text.len != 0;
}
static bool fail(Schema *s, const CeAtom *site, const char *message) {
    size_t line = 1;
    for (size_t i = 0; site && i < site->offset && i < s->text.len; ++i) line += s->text.data[i] == '\n';
    return ce_error(s->error,0,"%s:%zu: %s",s->path,line,message);
}
static CeText trim(CeText t) {
    while (t.len && (t.data[0] == ' ' || t.data[0] == '\t')) { ++t.data; --t.len; }
    while (t.len && (t.data[t.len-1] == ' ' || t.data[t.len-1] == '\t')) --t.len;
    return t;
}
static bool word(CeText *text, const char *name) {
    size_t n = strlen(name);
    if (text->len < n || memcmp(text->data,name,n) ||
        (text->len > n && text->data[n] != ' ' && text->data[n] != '\t' && text->data[n] != '*')) return false;
    text->data += n; text->len -= n; *text = trim(*text); return true;
}
static CeType *named_type(Schema *s, CeText text) {
    text = trim(text);
    unsigned tag = 0;
    if (word(&text,"struct")) tag = CE_ENTITY_STRUCT;
    else if (word(&text,"union")) tag = CE_ENTITY_UNION;
    for (size_t i = 0; i < s->sema->nentities; ++i) {
        CeEntity *e = s->sema->entities[i];
        if (e->scope != CE_SCOPE_FILE || !ce_equal(e->name,text)) continue;
        if (!tag && e->kind == CE_ENTITY_TYPEDEF) return e->type;
        if (tag && e->kind == tag) {
            CeType *type = ce_type_new(s->sema,tag == CE_ENTITY_STRUCT ? CE_T_STRUCT : CE_T_UNION);
            type->tag = e; return type;
        }
    }
    static const struct { const char *name; unsigned kind; } basic[] = {
        {"void",CE_T_VOID},{"_Bool",CE_T_BOOL},{"bool",CE_T_BOOL},{"char",CE_T_CHAR},
        {"signed char",CE_T_SCHAR},{"unsigned char",CE_T_UCHAR},{"short",CE_T_SHORT},
        {"unsigned short",CE_T_USHORT},{"int",CE_T_INT},{"unsigned",CE_T_UINT},
        {"unsigned int",CE_T_UINT},{"long",CE_T_LONG},{"unsigned long",CE_T_ULONG},
        {"long long",CE_T_LLONG},{"unsigned long long",CE_T_ULLONG},
        {"float",CE_T_FLOAT},{"double",CE_T_DOUBLE},{"long double",CE_T_LDOUBLE}
    };
    for (size_t i = 0; !tag && i < sizeof(basic)/sizeof(*basic); ++i)
        if (ce_is(text,basic[i].name)) {
            unsigned kind = basic[i].kind;
            if (!s->sema->basic[kind]) s->sema->basic[kind] = ce_type_new(s->sema,kind);
            return s->sema->basic[kind];
        }
    return NULL;
}
/* Interface expectations use existing C typedefs/tags and pointer qualifiers.
   Complex array/function types can be named by an actual C typedef. */
static CeType *type_name(Schema *s, CeText text) {
    text = trim(text); unsigned qualifiers = 0;
    for (;;) {
        if (word(&text,"const")) qualifiers |= CE_Q_CONST;
        else if (word(&text,"volatile")) qualifiers |= CE_Q_VOLATILE;
        else break;
    }
    size_t n = 0;
    while (n < text.len && text.data[n] != '*') ++n;
    CeType *type = named_type(s,(CeText){text.data,n});
    if (!type) return NULL;
    if (qualifiers) type = ce_type_qualified(s->sema,type,type->qualifiers|qualifiers);
    text.data += n; text.len -= n;
    while (text.len) {
        if (text.data[0] != '*') return NULL;
        ++text.data; --text.len; text = trim(text);
        type = ce_type_pointer(s->sema,type); qualifiers = 0;
        for (;;) {
            if (word(&text,"const")) qualifiers |= CE_Q_CONST;
            else if (word(&text,"volatile")) qualifiers |= CE_Q_VOLATILE;
            else if (word(&text,"restrict")) qualifiers |= CE_Q_RESTRICT;
            else break;
        }
        if (qualifiers) type = ce_type_qualified(s->sema,type,qualifiers);
    }
    return type;
}
static bool owner_exists(const Schema *s, CeText name) {
    for (size_t i = 0; i < s->nowners; ++i) if (ce_equal(s->owners[i],name)) return true;
    return false;
}
static bool parse_policy(Schema *s, const CeAtom *a, Policy *p) {
    if (a->len < 3 || !name(a->items[1],&p->name)) return fail(s,a,"visitor needs a name and contract");
    for (size_t i = 0; i < s->npolicies; ++i)
        if (ce_equal(s->policies[i].name,p->name)) return fail(s,a,"duplicate visitor name");
    unsigned seen = 0;
    for (size_t i = 2; i < a->len; ++i) {
        const CeAtom *c = a->items[i]; unsigned key = 0;
        if (head(c,"counted") && c->len == 1) { key = 64; p->counted = true; }
        else if (c->kind == CE_ATOM_LIST && c->len == 2) {
            CeText value;
            if (!name(c->items[1],&value)) return fail(s,c,"visitor clause needs a name or text");
            if (head(c,"owner")) { key = 1; p->owner = value; }
            else if (head(c,"type")) { key = 2; p->type = type_name(s,value); if (!p->type) return fail(s,c,"visitor type must name an actual C type"); }
            else if (head(c,"call") || head(c,"macro")) { key = 4; p->callee = value; p->macro = head(c,"macro"); }
            else if (head(c,"argument")) {
                key = 8;
                if (!symbol(c->items[1],"value") && !symbol(c->items[1],"address")) return fail(s,c,"visitor argument is value or address");
                p->address = symbol(c->items[1],"address");
            } else if (head(c,"effects")) {
                key = 16;
                if (!symbol(c->items[1],"read") && !symbol(c->items[1],"write")) return fail(s,c,"visitor effects are read or write");
                p->write = symbol(c->items[1],"write");
            } else if (head(c,"obligation")) { key = 32; p->obligation = value; }
        }
        if (!key || (seen & key)) return fail(s,c,"unknown or repeated visitor clause");
        seen |= key;
    }
    if ((seen & 63) != 63 || !identifier(p->callee) || !owner_exists(s,p->owner))
        return fail(s,a,"visitor needs a declared owner, type, callee, argument, effects and native obligation");
    if (p->counted && p->address) return fail(s,a,"counted visitors take their buffer by value");
    if (!p->macro) {
        for (size_t i = 0; i < s->sema->nentities; ++i) {
            CeEntity *e = s->sema->entities[i];
            if (e->scope == CE_SCOPE_FILE && e->kind == CE_ENTITY_FUNCTION && ce_equal(e->name,p->callee)) p->function = e;
        }
        if (!p->function || !p->function->type || !p->function->type->prototype ||
            p->function->type->variadic || p->function->type->base->kind != CE_T_BOOL ||
            p->function->type->nparameters != (p->counted ? 3u : 2u))
            return fail(s,a,"native visitor must have a declared nonvariadic bool prototype");
    }
    return true;
}
static const CeMember *member(CeType *type, CeText name, size_t *index) {
    if (!ce_type_record(type) || !type->tag->record || !type->tag->record->complete) return NULL;
    for (size_t i = 0; i < type->tag->record->nmembers; ++i) {
        const CeMember *m = &type->tag->record->members[i];
        if (ce_equal(m->name,name)) { if (index) *index = i; return m; }
    }
    return NULL;
}
static bool accepts(CeSema *s, CeType *actual, CeType *expected) {
    if (ce_type_compatible(s,actual,expected)) return true;
    if (actual->kind != CE_T_POINTER || expected->kind != CE_T_POINTER) return false;
    const CeType *object = actual->base;
    while (object->kind == CE_T_ARRAY) object = object->base;
    if (object->qualifiers & ~expected->base->qualifiers) return false;
    if (expected->base->kind == CE_T_VOID && actual->base->kind != CE_T_FUNCTION) return true;
    CeType *a = ce_type_unqualified(s,actual->base), *b = ce_type_unqualified(s,expected->base);
    return ce_type_compatible(s,a,b);
}
static bool interface_failure(Schema *s, const CeAtom *site, const char *label,
                              CeType *actual, CeType *expected) {
    char *a = ce_type_text(s->sema,actual,CE_PRINT_ANYWAY);
    char *b = ce_type_text(s->sema,expected,CE_PRINT_ANYWAY);
    const char *message = ce_format(s->arena,"native visitor %s expects %s; supplied %s",label,b,a);
    free(a); free(b);
    return fail(s,site,message);
}
static bool parse_field(Schema *s, Record *r, const CeAtom *a, Field *f) {
    CeText field;
    if (a->len < 3 || !name(a->items[1],&field) || !(f->member = member(r->type,field,&f->index)))
        return fail(s,a,"field must name a member of the actual C record");
    f->site = a;
    for (size_t i = 0; i < r->nfields; ++i)
        if (r->fields[i].member == f->member) return fail(s,a,"record field classified more than once");
    unsigned seen = 0;
    for (size_t i = 2; i < a->len; ++i) {
        const CeAtom *c = a->items[i]; unsigned key = 0;
        if (symbol(c,"value")) key = 1;
        else if (head(c,"borrow") && (c->len == 2 || c->len == 3)) {
            key = 1; f->mode = 1;
            if (!symbol(c->items[1],"read") && !symbol(c->items[1],"write")) return fail(s,c,"borrow access is read or write");
            f->write = symbol(c->items[1],"write");
            if (c->len == 3 && !symbol(c->items[2],"nullable")) return fail(s,c,"borrow accepts nullable");
            f->nullable = c->len == 3;
        } else if (head(c,"owns") && c->len == 2 && name(c->items[1],&f->family)) { key = 1; f->mode = 2; }
        else if (c->kind == CE_ATOM_LIST && c->len == 2) {
            CeText value;
            if (!name(c->items[1],&value)) return fail(s,c,"field clause needs a name or text");
            if (head(c,"owner")) { key = 2; f->owner = value; }
            else if (head(c,"keep")) { key = 4; f->keep = value; }
            else if (head(c,"visit")) {
                key = 4;
                for (size_t j = 0; j < s->npolicies; ++j) if (ce_equal(s->policies[j].name,value)) f->policy = &s->policies[j];
                if (!f->policy) return fail(s,c,"unknown field visitor");
            } else if (head(c,"count") || head(c,"when")) {
                key = head(c,"count") ? 8 : 16;
                const CeMember *m = member(r->type,value,NULL);
                if (!m) return fail(s,c,"count/when must name a field in the actual record");
                if (key == 8) f->count = m; else f->when = m;
            }
        }
        if (!key || (seen & key)) return fail(s,c,"unknown or repeated field clause");
        seen |= key;
    }
    bool ref = ce_type_contains_pointer(f->member->type);
    if (!(seen & 1) || ref != (f->mode != 0)) return fail(s,a,"reference-bearing fields need borrow/owns; value fields contain no pointers");
    if (f->mode && (!owner_exists(s,f->owner) || !(seen & 4)))
        return fail(s,a,"reference field needs a declared owner and visit/keep contract");
    if (!f->mode && (f->owner.len || f->keep.len || f->policy)) return fail(s,a,"value classification has no owner or visitor");
    if (f->nullable && f->member->type->kind != CE_T_POINTER) return fail(s,a,"nullable field must be a pointer");
    if (f->member->anonymous) return fail(s,a,"anonymous members need a named native adapter");
    if (!f->policy) {
        if (f->count || f->when) return fail(s,a,"count/when requires a visitor");
        return true;
    }
    Policy *p = f->policy;
    if (!ce_equal(f->owner,p->owner)) return fail(s,a,"field owner differs from its visitor's owner contract");
    if (!ce_type_compatible(s->sema,f->member->type,p->type)) return fail(s,a,"field type differs from the visitor's C interface");
    if (p->write && (!r->write || f->mode == 2 || (!p->address && !f->write)))
        return fail(s,a,"mutating visitor needs a write receiver and a compatible borrowed field");
    if ((f->count != NULL) != p->counted) return fail(s,a,"counted visitor and count field must be specified together");
    if (f->count) {
        CeType *t = f->member->type, *n = f->count->type;
        if ((t->kind != CE_T_POINTER && t->kind != CE_T_ARRAY) || !ce_type_size(s->sema,t->base) ||
            !ce_type_integer(n) || n->kind == CE_T_BOOL ||
            ce_kind_signed(s->sema->abi,ce_type_integer_kind(n)) ||
            ce_type_size(s->sema,n) > s->sema->abi->size[s->sema->abi->size_type] ||
            (n->qualifiers & (CE_Q_VOLATILE|CE_Q_ATOMIC)))
            return fail(s,a,"counted fields need a complete element type and an ordinary unsigned count");
    }
    if (f->when && (!ce_type_integer(f->when->type) || (f->when->type->qualifiers & (CE_Q_ATOMIC|CE_Q_VOLATILE))))
        return fail(s,a,"when needs an ordinary integer or bool field");
    if (!p->macro) {
        CeType *ft = p->function->type;
        CeType *field_type = f->member->type;
        if (!r->write) field_type = ce_type_qualified(s->sema,field_type,field_type->qualifiers|CE_Q_CONST);
        CeType *argument = p->address ? ce_type_pointer(s->sema,field_type) :
                           field_type->kind == CE_T_ARRAY ? ce_type_pointer(s->sema,field_type->base) :
                           ce_type_unqualified(s->sema,field_type);
        CeType *context = ce_type_pointer(s->sema,r->context);
        if (!accepts(s->sema,context,ft->parameters[0]))
            return interface_failure(s,a,"context",context,ft->parameters[0]);
        if (!accepts(s->sema,argument,ft->parameters[1]))
            return interface_failure(s,a,"field argument",argument,ft->parameters[1]);
        CeType *count = ce_type_basic(s->sema,s->sema->abi->size_type);
        if (p->counted && !ce_type_compatible(s->sema,ft->parameters[2],count))
            return interface_failure(s,a,"count",count,ft->parameters[2]);
    }
    return true;
}
static bool parse_record(Schema *s, const CeAtom *a, Record *r) {
    if (a->len < 3 || !name(a->items[1],&r->name) || !(r->type = type_name(s,r->name)) ||
        r->type->kind != CE_T_STRUCT || !r->type->tag->record || !r->type->tag->record->complete)
        return fail(s,a,"record must name a complete C structure");
    if (r->type->qualifiers & (CE_Q_CONST|CE_Q_VOLATILE|CE_Q_ATOMIC)) return fail(s,a,"record type must be an ordinary unqualified structure");
    unsigned seen = 0;
    for (size_t i = 2; i < a->len; ++i) {
        const CeAtom *c = a->items[i]; unsigned key = 0;
        if (head(c,"field")) continue;
        if ((head(c,"function") || head(c,"macro")) && c->len == 2 && name(c->items[1],&r->emitted)) {
            key = 1; r->macro = head(c,"macro");
        } else if (head(c,"context") && c->len == 2) {
            CeText type;
            if (name(c->items[1],&type)) r->context = type_name(s,type);
            if (!r->context) return fail(s,c,"context must name an actual C type");
            if (r->context->kind == CE_T_ARRAY || r->context->kind == CE_T_FUNCTION)
                return fail(s,c,"context must be an object or void type");
            key = 2;
        } else if (head(c,"receiver") && c->len == 2) {
            if (!symbol(c->items[1],"read") && !symbol(c->items[1],"write")) return fail(s,c,"receiver is read or write");
            key = 4; r->write = symbol(c->items[1],"write");
        }
        if (!key || (seen & key)) return fail(s,c,"unknown or repeated record clause");
        seen |= key;
    }
    if (seen != 7 || !identifier(r->emitted)) return fail(s,a,"record needs an emitted function/macro, context and receiver contract");
    for (size_t i = 0; i < s->nrecords; ++i)
        if (ce_equal(s->records[i].emitted,r->emitted)) return fail(s,a,"duplicate emitted visitor name");
    r->fields = ce_alloc(s->arena,a->len*sizeof(*r->fields));
    for (size_t i = 2; i < a->len; ++i) if (head(a->items[i],"field")) {
        if (!parse_field(s,r,a->items[i],&r->fields[r->nfields])) return false;
        ++r->nfields;
    }
    if (r->nfields != r->type->tag->record->nmembers) {
        for (size_t i = 0; i < r->type->tag->record->nmembers; ++i) {
            const CeMember *m = &r->type->tag->record->members[i]; bool found = false;
            for (size_t j = 0; j < r->nfields; ++j) found |= r->fields[j].member == m;
            if (!found) return fail(s,a,ce_format(s->arena,"unclassified C field %.*s; every field needs a contract",(int)m->name.len,m->name.data));
        }
        return fail(s,a,"every actual C field must be classified exactly once");
    }
    for (size_t i = 0; i < r->nfields; ++i) if (!r->fields[i].member->bitfield) {
        char *printed = ce_type_text(s->sema,ce_type_pointer(s->sema,r->fields[i].member->type),CE_PRINT_COMPATIBLE);
        if (!printed) return fail(s,r->fields[i].site,"field needs a nameable C type for independent interface checks");
        free(printed);
    }
    return true;
}

static void text(FILE *file, CeText t) { fwrite(t.data,1,t.len,file); }
static void type(FILE *file, Schema *s, CeType *t) {
    char *name = ce_type_text(s->sema,t,CE_PRINT_COMPATIBLE);
    if (name) { fputs(name,file); free(name); }
}
static void field_expression(FILE *file, const Record *r, const CeMember *m) {
    fputs(r->macro ? "((PAYLOAD)->" : "record->",file); text(file,m->name);
    if (r->macro) fputc(')',file);
}
static void line_end(FILE *file, const Record *r) { fputs(r->macro ? " \\\n" : "\n",file); }
static void failure(FILE *file, const Record *r) { fputs(r->macro ? "FAIL;" : "return false;",file); }
static void emit_assertions(Schema *s, FILE *file, const Record *r) {
    fputs("_Static_assert(sizeof(",file); type(file,s,r->type);
    fprintf(file,") == %llu && _Alignof(",r->type->tag->record->size); type(file,s,r->type);
    fprintf(file,") == %u, \"record visitor layout drift\");",ce_type_align(s->sema,r->type)); line_end(file,r);
    for (size_t i = 0; i < r->nfields; ++i) {
        const CeMember *m = r->fields[i].member;
        if (m->bitfield) continue;
        fputs("_Static_assert(offsetof(",file); type(file,s,r->type); fputs(", ",file); text(file,m->name);
        fprintf(file,") == %llu, \"record visitor field drift\");",m->offset); line_end(file,r);
        fputs("_Static_assert(_Generic(&((",file); type(file,s,r->type); fputs(" *)0)->",file); text(file,m->name);
        fputs(", ",file); type(file,s,ce_type_pointer(s->sema,m->type));
        fputs(": 1, default: 0), \"record visitor field type drift\");",file); line_end(file,r);
    }
}
static void emit_record(Schema *s, FILE *file, const Record *r) {
    fputs("#ifndef CESTA_RECORD_VISITOR_",file); text(file,r->emitted);
    fputs("\n#define CESTA_RECORD_VISITOR_",file); text(file,r->emitted); fputc('\n',file);
    if (!r->macro) emit_assertions(s,file,r);
    if (r->macro) { fputs("#define ",file); text(file,r->emitted); fputs("(CONTEXT, PAYLOAD, FAIL) do {",file); line_end(file,r); }
    else {
        fputs("static inline bool ",file); text(file,r->emitted); fputc('(',file); type(file,s,r->context);
        fputs(" *context, ",file); if (!r->write) fputs("const ",file); type(file,s,r->type);
        fputs(" *record) {\n    if (!context || !record) return false;\n",file);
    }
    if (r->macro) {
        fputs("    _Static_assert(_Generic((CONTEXT), ",file); type(file,s,ce_type_pointer(s->sema,r->context));
        fputs(": 1, default: 0), \"record visitor context type\");",file); line_end(file,r);
        fputs("    _Static_assert(_Generic((PAYLOAD), ",file);
        CeType *receiver = r->write ? r->type : ce_type_qualified(s->sema,r->type,CE_Q_CONST);
        type(file,s,ce_type_pointer(s->sema,receiver));
        if (!r->write) { fputs(": 1, ",file); type(file,s,ce_type_pointer(s->sema,r->type)); }
        fputs(": 1, default: 0), \"record visitor receiver type\");",file); line_end(file,r);
        emit_assertions(s,file,r);
    }
    for (size_t i = 0; i < r->nfields; ++i) {
        const Field *f = &r->fields[i];
        if (!f->count) continue;
        fputs("    if (",file); if (f->when) { field_expression(file,r,f->when); fputs(" && (",file); }
        field_expression(file,r,f->count); fputs(" > SIZE_MAX / sizeof(*",file); field_expression(file,r,f->member); fputc(')',file);
        if (f->member->type->kind == CE_T_ARRAY && f->member->type->length >= 0) {
            fputs(" || ",file); field_expression(file,r,f->count); fprintf(file," > %llu",(unsigned long long)f->member->type->length);
        } else if (f->member->type->kind == CE_T_POINTER) {
            fputs(" || (",file); field_expression(file,r,f->count); fputs(" && !",file);
            field_expression(file,r,f->member); fputc(')',file);
        }
        if (f->when) fputc(')',file);
        fputs(") { ",file); failure(file,r); fputs(" }",file); line_end(file,r);
    }
    for (size_t i = 0; i < r->nfields; ++i) {
        const Field *f = &r->fields[i]; const Policy *p = f->policy;
        if (!p) continue;
        fputs("    ",file);
        if (f->when || f->count) {
            fputs("if (",file);
            if (f->when) field_expression(file,r,f->when);
            if (f->when && f->count) fputs(" && ",file);
            if (f->count) field_expression(file,r,f->count);
            fputs(") { ",file);
        }
        if (!p->macro) fputs("if (!",file);
        text(file,p->callee); fputc('(',file); fputs(r->macro ? "(CONTEXT), " : "context, ",file);
        if (p->address) fputc('&',file);
        field_expression(file,r,f->member);
        if (f->count) { fputs(", ",file); field_expression(file,r,f->count); }
        if (p->macro) { fputs(", ",file); fputs(r->macro ? "FAIL" : "return false",file); }
        fputc(')',file);
        if (p->macro) fputc(';',file);
        else { fputs(") { ",file); failure(file,r); fputs(" }",file); }
        if (f->when || f->count) fputs(" }",file);
        line_end(file,r);
    }
    fputs(r->macro ? "} while (0)\n\n" : "    return true;\n}\n\n",file);
    fputs("#endif\n\n",file);
}
static void emit_facts(Schema *s, FILE *file) {
    fputs("(cesta record-visitors-facts/1\n (target ",file); ce_quote(file,ce_cstr(s->arena,s->sema->abi->name)); fputs(")\n",file);
    fputs(" (owners",file);
    for (size_t i = 0; i < s->nowners; ++i) { fputc(' ',file); ce_quote(file,s->owners[i]); }
    fputs(")\n",file);
    for (size_t i = 0; i < s->npolicies; ++i) {
        Policy *p = &s->policies[i];
        fputs(" (visitor ",file); ce_quote(file,p->name); fputc(' ',file); ce_quote(file,p->owner);
        fprintf(file," %s %s %s ",p->write ? "write" : "read",p->address ? "address" : "value",p->counted ? "counted" : "single");
        ce_quote(file,p->obligation); fputs(" (interface ",file);
        char *ctype = ce_type_text(s->sema,p->type,CE_PRINT_ANYWAY);
        ce_quote(file,ce_cstr(s->arena,ctype)); free(ctype);
        fprintf(file," %s ",p->macro ? "macro" : "function"); ce_quote(file,p->callee); fputs("))\n",file);
    }
    for (size_t i = 0; i < s->nrecords; ++i) {
        Record *r = &s->records[i];
        fputs(" (record ",file); ce_quote(file,r->name); fprintf(file," %s\n  (declared",r->write ? "write" : "read");
        for (size_t j = 0; j < r->type->tag->record->nmembers; ++j) {
            const CeMember *m = &r->type->tag->record->members[j];
            fputs(" (field ",file); ce_quote(file,m->name); fprintf(file," %s ",ce_type_contains_pointer(m->type) ? "reference" : "value");
            char *ctype = ce_type_text(s->sema,m->type,CE_PRINT_ANYWAY);
            ce_quote(file,ce_cstr(s->arena,ctype)); free(ctype); fputc(')',file);
        }
        fputs(")\n  (classified",file);
        for (size_t j = 0; j < r->nfields; ++j) {
            Field *f = &r->fields[j];
            fputs(" (field ",file); ce_quote(file,f->member->name); fprintf(file," %s ",f->mode == 2 ? "own" : f->mode ? "borrow" : "value");
            ce_quote(file,f->owner); fputc(' ',file);
            if (f->policy) ce_quote(file,f->policy->name); else fputs("()",file);
            fputc(' ',file); ce_quote(file,f->keep);
            fprintf(file," (access %s %s)",f->write ? "write" : "read",f->nullable ? "nullable" : "nonnull");
            if (f->family.len) { fputs(" (family ",file); ce_quote(file,f->family); fputc(')',file); }
            if (f->count) { fputs(" (count ",file); ce_quote(file,f->count->name); fputc(')',file); }
            if (f->when) { fputs(" (when ",file); ce_quote(file,f->when->name); fputc(')',file); }
            fputc(')',file);
        }
        fputs("))\n",file);
    }
    fputs(")\n",file);
}

bool ce_record_visitors(CeArena *arena, CeSema *sema, const char *schema_path,
                        CeRecordVisitors *output, CeError *error) {
    Schema s = {.arena=arena,.sema=sema,.error=error,.path=schema_path};
    const CeAtom *root;
    if (!ce_read_file(arena,schema_path,&s.text,error)) return false;
    CeAtom *parsed;
    CeError syntax = {{0},0};
    if (!ce_atom_read(arena,s.text,&parsed,&syntax)) {
        CeAtom site = {.offset=syntax.offset};
        return fail(&s,&site,syntax.message);
    }
    root = parsed;
    if (!head(root,"cesta") || root->len < 2 || !symbol(root->items[1],"record-visitors/1"))
        return fail(&s,root,"expected cesta record-visitors/1");
    s.owners = ce_alloc(arena,root->len*sizeof(*s.owners));
    s.policies = ce_alloc(arena,root->len*sizeof(*s.policies));
    s.records = ce_alloc(arena,root->len*sizeof(*s.records));
    for (size_t i = 2; i < root->len; ++i) if (head(root->items[i],"owner")) {
        const CeAtom *a = root->items[i]; CeText owner;
        if (a->len != 2 || !name(a->items[1],&owner) || owner_exists(&s,owner)) return fail(&s,a,"owner names must be unique");
        s.owners[s.nowners++] = owner;
    }
    for (size_t i = 2; i < root->len; ++i) if (head(root->items[i],"visitor")) {
        if (!parse_policy(&s,root->items[i],&s.policies[s.npolicies])) return false;
        ++s.npolicies;
    }
    for (size_t i = 2; i < root->len; ++i) {
        const CeAtom *a = root->items[i];
        if (head(a,"owner") || head(a,"visitor")) continue;
        if (!head(a,"record")) return fail(&s,a,"invalid record visitor declaration");
        if (!parse_record(&s,a,&s.records[s.nrecords])) return false;
        ++s.nrecords;
    }
    if (!s.nrecords) return fail(&s,root,"record visitor module has no records");
    char *code = NULL, *facts = NULL; size_t nc = 0, nf = 0;
    FILE *c = open_memstream(&code,&nc), *f = open_memstream(&facts,&nf);
    if (!c || !f) {
        if (c) fclose(c);
        if (f) fclose(f);
        free(code); free(facts);
        return fail(&s,root,"cannot open generated record output");
    }
    fputs("/* Generated record visitors. Native policies supply traversal semantics.\n"
          " * Failure may leave an already visited prefix updated. */\n"
          "#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n\n",c);
    for (size_t i = 0; i < s.nrecords; ++i) emit_record(&s,c,&s.records[i]);
    emit_facts(&s,f);
    bool ok = !ferror(c) && !ferror(f); ok = fclose(c) == 0 && ok; ok = fclose(f) == 0 && ok;
    if (ok) *output = (CeRecordVisitors){ce_text(arena,code,nc),ce_text(arena,facts,nf)};
    free(code); free(facts);
    return ok || fail(&s,root,"cannot write generated record output");
}

bool ce_record_output(const char *path, CeText text, CeError *error) {
    size_t n = strlen(path);
    if (n > SIZE_MAX-8) return ce_error(error,0,"output path is too long");
    char *temporary = malloc(n+8);
    if (!temporary) return ce_error(error,0,"out of memory");
    memcpy(temporary,path,n); memcpy(temporary+n,".XXXXXX",8);
    int fd = mkstemp(temporary); FILE *file = fd < 0 ? NULL : fdopen(fd,"wb");
    if (!file) {
        if (fd >= 0) { close(fd); unlink(temporary); }
        free(temporary); return ce_error(error,0,"cannot write %s: %s",path,strerror(errno));
    }
    bool ok = fwrite(text.data,1,text.len,file) == text.len && !fchmod(fd,0644);
    ok = fclose(file) == 0 && ok;
    if (ok && rename(temporary,path)) ok = false;
    if (!ok) { unlink(temporary); ce_error(error,0,"cannot write %s: %s",path,strerror(errno)); }
    free(temporary); return ok;
}

#define _POSIX_C_SOURCE 200809L
#include "durable_value.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define VALUE_BYTES (1024u*1024u)
#define VALUE_NODES 100000u
#define VALUE_DEPTH 128u

_Static_assert(sizeof(double)==8 && DBL_MANT_DIG==53 && DBL_MAX_EXP==1024,
               "CDV1 requires binary64 floats");

typedef struct {
    unsigned char *data;
    size_t length, capacity, nodes;
} Writer;

static CettaDurableStatus append(Writer *w, const void *data, size_t n) {
    if (n > VALUE_BYTES-w->length) return DURABLE_LIMIT;
    if (w->length+n > w->capacity) {
        size_t capacity = w->capacity ? w->capacity*2 : 256;
        if (capacity < w->length+n) capacity = w->length+n;
        if (capacity > VALUE_BYTES) capacity = VALUE_BYTES;
        unsigned char *p = realloc(w->data,capacity);
        if (!p) return DURABLE_NOMEM;
        w->data=p; w->capacity=capacity;
    }
    memcpy(w->data+w->length,data,n); w->length+=n;
    return DURABLE_OK;
}

static CettaDurableStatus number(Writer *w, uint64_t value, size_t n) {
    unsigned char bytes[8];
    for (size_t i=0; i<n; ++i) bytes[i]=(unsigned char)(value>>(8*i));
    return append(w,bytes,n);
}

static CettaDurableStatus encode(Writer *w, const Atom *a, unsigned depth) {
    if (!a) return DURABLE_INVALID;
    if (depth>VALUE_DEPTH || ++w->nodes>VALUE_NODES) return DURABLE_LIMIT;
    if (a->flags & (ATOM_FLAG_HAS_REGISTRY_REFS | ATOM_FLAG_HAS_IDENTITY_GROUNDED))
        return DURABLE_INVALID;
    const char *text=NULL; unsigned char tag=0; uint64_t bits=0;
    if (a->kind==ATOM_SYMBOL) {
        tag='S'; text=symbol_bytes(g_symbols,a->sym_id);
        if (!text || strlen(text)!=symbol_len(g_symbols,a->sym_id)) return DURABLE_INVALID;
    }
    else if (a->kind==ATOM_EXPR) {
        if (a->expr.len && atom_is_symbol(a->expr.elems[0],"NativeHandle")) return DURABLE_INVALID;
        if (a->expr.len>VALUE_NODES) return DURABLE_LIMIT;
        tag='E';
    } else if (a->kind==ATOM_GROUNDED) {
        switch (a->ground.gkind) {
        case GV_INT: tag='I'; bits=(uint64_t)a->ground.ival; break;
        case GV_FLOAT: tag='F'; memcpy(&bits,&a->ground.fval,8); break;
        case GV_BOOL: tag=a->ground.bval?'Y':'N'; break;
        case GV_STRING: tag='T'; text=a->ground.sval; break;
        case GV_BIGINT: tag='J'; text=atom_bigint_cstr(a); break;
        case GV_RATIONAL: tag='Q'; text=atom_rational_cstr(a); break;
        default: return DURABLE_INVALID;
        }
    } else return DURABLE_INVALID;
    if ((tag=='S'||tag=='T'||tag=='J'||tag=='Q') && !text) return DURABLE_INVALID;
    CettaDurableStatus s=append(w,&tag,1);
    if (s!=DURABLE_OK) return s;
    if (text) {
        size_t n=tag=='T'?atom_string_len(a):strnlen(text,VALUE_BYTES+1);
        if (n>VALUE_BYTES) return DURABLE_LIMIT;
        s=number(w,n,4); return s==DURABLE_OK?append(w,text,n):s;
    }
    if (tag=='I' || tag=='F') return number(w,bits,8);
    if (tag=='E') {
        s=number(w,a->expr.len,4);
        for (CettaExprLen i=0; s==DURABLE_OK && i<a->expr.len; ++i)
            s=encode(w,a->expr.elems[i],depth+1);
    }
    return s;
}

CettaDurableStatus cetta_durable_value_encode(const Atom *a, unsigned char **bytes, size_t *n) {
    if (!bytes || !n) return DURABLE_INVALID;
    *bytes=NULL; *n=0;
    Writer w={0};
    CettaDurableStatus s=append(&w,"CDV1",4);
    if (s==DURABLE_OK) s=encode(&w,a,0);
    if (s!=DURABLE_OK) { free(w.data); return s; }
    *bytes=w.data; *n=w.length; return DURABLE_OK;
}

static CettaDurableStatus fields(Writer *w, const CettaDurableField *v, unsigned depth) {
    if (!v) return DURABLE_INVALID;
    if (depth>VALUE_DEPTH || ++w->nodes>VALUE_NODES) return DURABLE_LIMIT;
    unsigned char tag;
    switch (v->kind) {
    case DURABLE_FIELD_SYMBOL: tag='S'; break;
    case DURABLE_FIELD_TEXT: tag='T'; break;
    case DURABLE_FIELD_INT: tag='I'; break;
    case DURABLE_FIELD_BOOL: tag=v->boolean?'Y':'N'; break;
    case DURABLE_FIELD_EXPR: tag='E'; break;
    default: return DURABLE_INVALID;
    }
    CettaDurableStatus s=append(w,&tag,1);
    if (s!=DURABLE_OK) return s;
    if (tag=='I') return number(w,(uint64_t)v->integer,8);
    if (tag=='S' || tag=='T') {
        size_t n=v->text.size;
        if (n>VALUE_BYTES) return DURABLE_LIMIT;
        if ((!v->text.data && n) || (n && memchr(v->text.data,0,n))) return DURABLE_INVALID;
        s=number(w,n,4);
        return s==DURABLE_OK && n?append(w,v->text.data,n):s;
    }
    if (tag=='E') {
        size_t n=v->expression.count;
        if (n>VALUE_NODES) return DURABLE_LIMIT;
        if (!v->expression.items && n) return DURABLE_INVALID;
        /* Keep the same reserved-handle rule as the Atom encoder. */
        if (n && v->expression.items[0].kind==DURABLE_FIELD_SYMBOL) {
            const CettaDurableField *h=&v->expression.items[0];
            if (h->text.size==12 && h->text.data && !memcmp(h->text.data,"NativeHandle",12)) return DURABLE_INVALID;
        }
        s=number(w,n,4);
        for (size_t i=0;s==DURABLE_OK && i<n;++i) s=fields(w,&v->expression.items[i],depth+1);
    }
    return s;
}
CettaDurableStatus cetta_durable_fields_encode(const CettaDurableField *v,
        unsigned char **bytes, size_t *n) {
    if (!bytes || !n) return DURABLE_INVALID;
    *bytes=NULL; *n=0;
    Writer w={0};
    CettaDurableStatus s=append(&w,"CDV1",4);
    if (s==DURABLE_OK) s=fields(&w,v,0);
    if (s!=DURABLE_OK) { free(w.data); return s; }
    *bytes=w.data; *n=w.length; return DURABLE_OK;
}

typedef struct { const unsigned char *data; size_t size, pos, nodes; } Reader;

static bool read_number(Reader *r, size_t n, uint64_t *out) {
    if (n>r->size-r->pos) return false;
    uint64_t v=0;
    for (size_t i=0;i<n;++i) v|=(uint64_t)r->data[r->pos++]<<(8*i);
    *out=v; return true;
}

static bool digits(const char *s) {
    if (*s=='-') ++s;
    if (!*s) return false;
    for (;*s;++s) if (*s<'0'||*s>'9') return false;
    return true;
}

static CettaDurableStatus decode(Reader *r, Arena *arena, unsigned depth, Atom **out) {
    if (depth>VALUE_DEPTH || ++r->nodes>VALUE_NODES) return DURABLE_LIMIT;
    uint64_t tag,n;
    if (!read_number(r,1,&tag)) return DURABLE_CORRUPT;
    if (tag=='I'||tag=='F') {
        if (!read_number(r,8,&n)) return DURABLE_CORRUPT;
        if (tag=='I') { int64_t x; memcpy(&x,&n,8); *out=atom_int(arena,x); }
        else { double x; memcpy(&x,&n,8); *out=atom_float(arena,x); }
    } else if (tag=='Y'||tag=='N') *out=atom_bool(arena,tag=='Y');
    else if (tag=='E') {
        if (!read_number(r,4,&n) || n>VALUE_NODES-r->nodes || n>r->size-r->pos)
            return DURABLE_CORRUPT;
        Atom **items=arena_alloc(arena,n*sizeof(*items));
        for (size_t i=0;i<n;++i) {
            CettaDurableStatus s=decode(r,arena,depth+1,&items[i]);
            if (s!=DURABLE_OK) return s;
        }
        if (n && atom_is_symbol(items[0],"NativeHandle")) return DURABLE_INVALID;
        *out=atom_expr(arena,items,n);
    } else if (tag=='S'||tag=='T'||tag=='J'||tag=='Q') {
        if (!read_number(r,4,&n) || n>r->size-r->pos ||
            (tag!='T' && memchr(r->data+r->pos,0,n)))
            return DURABLE_CORRUPT;
        char *text=arena_alloc(arena,n+1);
        memcpy(text,r->data+r->pos,n); text[n]=0; r->pos+=n;
        if (tag=='S') *out=atom_symbol(arena,text);
        if (tag=='T') *out=atom_string_n(arena,text,n);
        if (tag=='J') {
            if (!digits(text)) return DURABLE_CORRUPT;
            *out=atom_bigint(arena,text);
        }
        if (tag=='Q') {
            char *sep=strchr(text,'r');
            if (!sep) sep=strchr(text,'/');
            if (!sep) return DURABLE_CORRUPT;
            char c=*sep; *sep=0;
            bool valid=digits(text) && digits(sep+1) && sep[1]!='-' &&
                strspn(sep+1,"0")!=strlen(sep+1);
            *sep=c;
            if (!valid) return DURABLE_CORRUPT;
            *out=atom_rational(arena,text);
        }
    } else return DURABLE_CORRUPT;
    return *out ? DURABLE_OK : DURABLE_NOMEM;
}

CettaDurableStatus cetta_durable_value_decode(Arena *arena, const unsigned char *bytes,
        size_t n, Atom **out) {
    if (!arena || !bytes || !out) return DURABLE_INVALID;
    *out=NULL;
    if (n>VALUE_BYTES) return DURABLE_LIMIT;
    if (n<4 || memcmp(bytes,"CDV",3) || bytes[3]<'1' || bytes[3]>'9') return DURABLE_CORRUPT;
    if (bytes[3]!='1') return DURABLE_VERSION;
    Reader r={bytes,n,4,0}; Atom *a=NULL;
    CettaDurableStatus s=decode(&r,arena,0,&a);
    if (s==DURABLE_OK && r.pos!=r.size) s=DURABLE_CORRUPT;
    if (s==DURABLE_OK) *out=a;
    return s;
}

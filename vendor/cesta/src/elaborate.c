/* Elaboration: C's own types for every declaration and expression of a
   translation unit (N1570 §6.3, §6.5–6.9), over the bindings from bind.c and
   the type language of ctype.c. Every implicit conversion becomes an explicit
   step recorded on the operand it applies to. */

#include "sema.h"

#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

const char *const ce_conversion_names[] = {
    "",
#define CE_CONVERSION_NAME(name) #name,
    CE_CONVERSIONS(CE_CONVERSION_NAME)
#undef CE_CONVERSION_NAME
};

typedef struct {
    unsigned aligned;             /* largest aligned(N); 0 when absent */
    unsigned enum_layout;         /* first packed (1) or aligned (2) attribute */
    bool packed, has_mode, transparent_union;
    unsigned mode_bytes;          /* mode(QI/HI/SI/DI/TI/word) */
    bool mode_complex, mode_float;
} Attributes;

typedef struct {
    CeType *type;
    bool is_typedef, external, internal;
    unsigned alignas_;            /* _Alignas, the strictest */
    Attributes attributes;
} Spec;

typedef struct CaseRange {
    unsigned long long low, high;
    struct CaseRange *next;
} CaseRange;
typedef struct {
    CaseRange *ranges;
    bool has_default;
} SwitchLabels;

typedef struct {
    CeSema *sema;
    const CeGrammar *g;
    CeError *error;
    CeType *return_type;          /* the enclosing function's return type */
    CeEntity *function;
    const CeTree *switch_node;
    CeType *switch_type;
    unsigned folded;              /* inside an array size or array designator */
    CeType *function_type;        /* the enclosing definition's own declarator type */
    unsigned loop_depth;
    SwitchLabels *switch_labels;
} Elab;

static bool fail(Elab *e, const CeTree *node, const char *fmt, ...) {
    char message[400];
    va_list args; va_start(args,fmt); vsnprintf(message,sizeof(message),fmt,args); va_end(args);
    return ce_error(e->error,node ? node->offset : 0,"%s",message);
}
static unsigned kind_of(Elab *e, const CeTree *t) { return ce_sema_kind(e->sema,t); }
static CeType *basic(Elab *e, unsigned kind) { return ce_type_basic(e->sema,kind); }

/* A VLA is a complete object type although its byte count is not constant. */
static bool complete_object(const CeType *type) {
    return type->kind == CE_T_ARRAY ? type->length != -1 && complete_object(type->base) : ce_type_complete(type);
}
static CeType *array_type(Elab *e, CeType *element, long long length, const CeTree *node) {
    if (!complete_object(element)) {
        fail(e,node,"an array element needs a complete object type"); return NULL;
    }
    return ce_type_array(e->sema,element,length);
}

const CeExpr *ce_sema_expr(const CeSema *sema, const CeTree *node) {
    return ce_pointer_get(&sema->exprs,node);
}
static CeExpr *new_expr(Elab *e, const CeTree *node, CeType *type, unsigned category) {
    CeExpr *x = ce_alloc(&e->sema->arena,sizeof(*x));
    *x = (CeExpr){.type=type,.category=category,.folded=e->folded > 0};
    ce_pointer_put(&e->sema->exprs,node,x);
    return x;
}

/* ---- integer constants ---------------------------------------------------- */

/* Truncate `bits` to the width and signedness of an integer type. */
static unsigned long long truncate_value(Elab *e, const CeType *t, unsigned long long bits) {
    unsigned kind = ce_type_integer_kind(t);
    if (kind == CE_T_BOOL) return bits != 0;
    unsigned width = ce_kind_width(e->sema->abi,kind);
    if (width >= 64) return bits;
    unsigned long long mask = (1ULL << width)-1;
    bits &= mask;
    if (ce_kind_signed(e->sema->abi,kind) && (bits >> (width-1)) & 1) bits |= ~mask;
    return bits;
}
static bool signed_type(Elab *e, const CeType *t) {
    return ce_type_integer(t) && ce_kind_signed(e->sema->abi,ce_type_integer_kind(t));
}
static bool value_negative(Elab *e, const CeExpr *x) {
    return signed_type(e,x->type) && (long long)x->value < 0;
}

/* ---- attributes ------------------------------------------------------------ */

static CeText attribute_word(Elab *e, const CeTree *name) {
    if (kind_of(e,name) == CE_K_GNUNamedAttribute) name = name->children[0];
    else return (CeText){(unsigned char *)"const",5};
    CeText w = name->spelling;
    if (w.len > 4 && w.data[0] == '_' && w.data[1] == '_' && w.data[w.len-1] == '_' && w.data[w.len-2] == '_')
        w = (CeText){w.data+2,w.len-4};
    return w;
}
static bool expression(Elab *e, const CeTree *t, CeExpr **out);
static bool constant_value(Elab *e, const CeTree *t, long long *value);

static bool attributes(Elab *e, const CeTree *t, Attributes *a) {
    if (!t) return true;
    if (t->production == -2) {
        for (size_t i = 0; i < t->len; ++i) if (!attributes(e,t->children[i],a)) return false;
        return true;
    }
    switch (kind_of(e,t)) {
    case CE_K_GNUAttribute: return attributes(e,t->children[1],a);
    case CE_K_GNUEnumAttributes:
        return attributes(e,t->children[0],a);
    case CE_K_GNUAttributeQualifier: return attributes(e,t->children[0],a);
    case CE_K_GNUAttributeItem: {
        CeText w = attribute_word(e,t->children[0]);
        if (ce_is(w,"packed")) { a->packed = true; if (!a->enum_layout) a->enum_layout = 1; }
        else if (ce_is(w,"aligned")) {
            if (!a->enum_layout) a->enum_layout = 2;
            if (e->sema->abi->max_align > a->aligned) a->aligned = e->sema->abi->max_align;
        }
        else if (ce_is(w,"transparent_union")) a->transparent_union = true;
        return true;
    }
    case CE_K_GNUAttributeCall: {
        CeText w = attribute_word(e,t->children[0]);
        const CeTree *arguments = t->children[1];
        const CeTree *list = kind_of(e,arguments) == CE_K_GNUAttributeArguments ? arguments->children[0] : NULL;
        if (ce_is(w,"aligned") && list && list->len == 1) {
            if (!a->enum_layout) a->enum_layout = 2;
            long long n;
            if (!constant_value(e,list->children[0],&n)) return false;
            if ((unsigned)n > a->aligned) a->aligned = (unsigned)n;
        } else if (ce_is(w,"mode") && list && list->len == 1) {
            const CeTree *v = list->children[0];
            if (kind_of(e,v) != CE_K_Var) return fail(e,v,"mode attribute needs a machine mode name");
            CeText m = v->children[0]->spelling;
            if (m.len > 4 && m.data[0] == '_' && m.data[1] == '_') m = (CeText){m.data+2,m.len-4};
            a->has_mode = true;
            if (ce_is(m,"QI") || ce_is(m,"byte")) a->mode_bytes = 1;
            else if (ce_is(m,"HI")) a->mode_bytes = 2;
            else if (ce_is(m,"SI")) a->mode_bytes = 4;
            else if (ce_is(m,"DI")) a->mode_bytes = 8;
            else if (ce_is(m,"word")) a->mode_bytes = e->sema->abi->size[e->sema->abi->word_type];
            else if (ce_is(m,"pointer")) a->mode_bytes = e->sema->abi->size[CE_T_POINTER];
            else if (ce_is(m,"TI")) a->mode_bytes = 16;
            else if (ce_is(m,"SF")) { a->mode_bytes = 4; a->mode_float = true; }
            else if (ce_is(m,"DF")) { a->mode_bytes = 8; a->mode_float = true; }
            else if (ce_is(m,"XF")) { a->mode_bytes = 10; a->mode_float = true; }
            else if (ce_is(m,"TF")) { a->mode_bytes = 16; a->mode_float = true; }
            else if (ce_is(m,"SC")) { a->mode_bytes = 4; a->mode_float = a->mode_complex = true; }
            else if (ce_is(m,"DC")) { a->mode_bytes = 8; a->mode_float = a->mode_complex = true; }
            else if (ce_is(m,"XC")) { a->mode_bytes = 10; a->mode_float = a->mode_complex = true; }
            else if (ce_is(m,"TC")) { a->mode_bytes = 16; a->mode_float = a->mode_complex = true; }
            else return fail(e,v,"unsupported machine mode %.*s",(int)m.len,m.data);
        } else if (ce_is(w,"vector_size")) {
            return fail(e,t,"GNU vector types are outside the elaborated C type language");
        }
        return true;
    }
    default:
        for (size_t i = 0; i < t->len; ++i) if (!attributes(e,t->children[i],a)) return false;
        return true;
    }
}

/* A machine mode replaces an arithmetic type of the same signedness. */
static CeType *apply_mode(Elab *e, CeType *t, const Attributes *a, const CeTree *at) {
    if (!a->has_mode) return t;
    const CeAbi *abi = e->sema->abi;
    if (a->mode_float) {
        unsigned kind = a->mode_bytes == 4 ? CE_T_FLOAT : a->mode_bytes == 8 ? CE_T_DOUBLE :
            a->mode_bytes == 10 ? CE_T_LDOUBLE : CE_T_FLOAT128;
        CeType *real = basic(e,kind);
        if (!a->mode_complex) return ce_type_qualified(e->sema,real,t->qualifiers);
        CeType *c = ce_type_new(e->sema,CE_T_COMPLEX); c->base = real; c->qualifiers = t->qualifiers; return c;
    }
    if (!ce_type_integer(t)) { fail(e,at,"an integer machine mode applies to an integer type"); return NULL; }
    bool is_signed = ce_kind_signed(abi,ce_type_integer_kind(t));
    static const unsigned signed_kinds[] = {CE_T_SCHAR,CE_T_SHORT,CE_T_INT,CE_T_LONG,CE_T_LLONG,CE_T_INT128};
    static const unsigned unsigned_kinds[] = {CE_T_UCHAR,CE_T_USHORT,CE_T_UINT,CE_T_ULONG,CE_T_ULLONG,CE_T_UINT128};
    for (size_t i = 0; i < 6; ++i) if (abi->size[signed_kinds[i]] == a->mode_bytes)
        return ce_type_qualified(e->sema,basic(e,is_signed ? signed_kinds[i] : unsigned_kinds[i]),t->qualifiers);
    fail(e,at,"no integer type has this machine mode"); return NULL;
}

/* ---- declaration specifiers (§6.7.1–6.7.5) --------------------------------- */

typedef struct {
    unsigned chars, shorts, ints, longs, floats, doubles, signeds, unsigneds, complexes, int128s, float16s;
    unsigned qualifiers;
    CeType *unique;
    const CeTree *at;
} Words;

static bool record_specifier(Elab *e, const CeTree *t, const Attributes *outer, CeType **out);
static void convert(Elab *e, CeExpr *x, CeType *to);
static bool constant_value(Elab *e, const CeTree *t, long long *value);
/* §6.7.10: the condition must compare unequal to 0; Clang converts it to _Bool. */
static bool static_assertion(Elab *e, const CeTree *a) {
    long long v; if (!constant_value(e,a->children[0],&v)) return false;
    if (!v) return fail(e,a,"static assertion evaluates to zero here");
    convert(e,ce_pointer_get(&e->sema->exprs,a->children[0]),ce_type_basic(e->sema,CE_T_BOOL));
    return true;
}
static bool enum_specifier(Elab *e, const CeTree *t, CeType **out);
static bool type_name(Elab *e, const CeTree *t, CeType **out);

static bool qualifier(Elab *e, const CeTree *t, Words *w, Spec *s) {
    switch (kind_of(e,t)) {
    case CE_K_Const: case CE_K_GNUConst: case CE_K_GNUConstPrefix: w->qualifiers |= CE_Q_CONST; return true;
    case CE_K_Volatile: case CE_K_GNUVolatile: case CE_K_GNUVolatilePrefix: w->qualifiers |= CE_Q_VOLATILE; return true;
    case CE_K_Restrict: case CE_K_GNURestrict: case CE_K_GNURestrictPrefix: w->qualifiers |= CE_Q_RESTRICT; return true;
    case CE_K_AtomicQualifier: w->qualifiers |= CE_Q_ATOMIC; return true;
    case CE_K_GNUAttributeQualifier: return s ? attributes(e,t,&s->attributes) : true;
    default: return fail(e,t,"unknown type qualifier");
    }
}

static bool specifier_word(Elab *e, const CeTree *t, Words *w, Spec *s) {
    if (!t) return true;
    if (t->production == -2) {
        for (size_t i = 0; i < t->len; ++i) if (!specifier_word(e,t->children[i],w,s)) return false;
        return true;
    }
    switch (kind_of(e,t)) {
    case CE_K_GNUAttributeSpecifiers:
        return (!s || attributes(e,t->children[0],&s->attributes)) && specifier_word(e,t->children[1],w,s);
    case CE_K_UniqueDeclarationSpecifiers: case CE_K_UniqueSpecifierQualifiers:
    case CE_K_PrimitiveDeclarationSpecifiers: case CE_K_PrimitiveSpecifierQualifiers:
    case CE_K_PrefixedDeclarationSpecifiers: case CE_K_PrefixedSpecifierQualifiers:
    case CE_K_GNUAlignedMemberSpecifiers: case CE_K_PrimitiveSpecifierTail: case CE_K_NonTypeSpecifierTail:
    case CE_K_PrimitiveQualifierTail: case CE_K_QualifierTail: case CE_K_SomeTypeQualifiers:
    case CE_K_QualifierSpecifier:
        for (size_t i = 0; i < t->len; ++i) if (!specifier_word(e,t->children[i],w,s)) return false;
        return true;
    case CE_K_NoTypeQualifiers: return true;
    case CE_K_StorageSpecifier: {
        unsigned k = kind_of(e,t->children[0]);
        if (s) {
            if (k == CE_K_Typedef) s->is_typedef = true;
            else if (k == CE_K_Extern) s->external = true;
            else if (k == CE_K_Static) s->internal = true;
        }
        return true;
    }
    case CE_K_FunctionSpecifier: return true;
    case CE_K_AlignmentSpecifier: case CE_K_AlignasType: case CE_K_AlignasExpression: {
        const CeTree *a = kind_of(e,t) == CE_K_AlignmentSpecifier ? t->children[0] : t; unsigned align;
        if (kind_of(e,a) == CE_K_AlignasType) {
            CeType *type; if (!type_name(e,a->children[0],&type)) return false;
            align = ce_type_align(e->sema,type);
        } else {
            long long n; if (!constant_value(e,a->children[0],&n)) return false;
            align = (unsigned)n;
        }
        if (s && align > s->alignas_) s->alignas_ = align;
        return true;
    }
    case CE_K_Const: case CE_K_GNUConst: case CE_K_GNUConstPrefix: case CE_K_Volatile:
    case CE_K_GNUVolatile: case CE_K_GNUVolatilePrefix: case CE_K_Restrict: case CE_K_GNURestrict:
    case CE_K_GNURestrictPrefix: case CE_K_AtomicQualifier: case CE_K_GNUAttributeQualifier:
        return qualifier(e,t,w,s);
    case CE_K_Char: ++w->chars; return true;
    case CE_K_Short: ++w->shorts; return true;
    case CE_K_Int: ++w->ints; return true;
    case CE_K_Long: ++w->longs; return true;
    case CE_K_Float: ++w->floats; return true;
    case CE_K_Double: ++w->doubles; return true;
    case CE_K_Signed: case CE_K_GNUSigned: ++w->signeds; return true;
    case CE_K_Unsigned: ++w->unsigneds; return true;
    case CE_K_Complex: ++w->complexes; return true;
    case CE_K_GNUInt128:
        if (!e->sema->abi->size[CE_T_INT128]) return fail(e,t,"__int128 is not available on this target");
        ++w->int128s; return true;
    case CE_K_Void: w->unique = basic(e,CE_T_VOID); return true;
    case CE_K_Bool: w->unique = basic(e,CE_T_BOOL); return true;
    case CE_K_GNUFloat128: case CE_K_GNUQuad: w->unique = basic(e,CE_T_FLOAT128); return true;
    case CE_K_GNUFloat32: w->unique = basic(e,CE_T_FLOAT32); return true;
    case CE_K_GNUFloat64: w->unique = basic(e,CE_T_FLOAT64); return true;
    case CE_K_GNUFloat32x: w->unique = basic(e,CE_T_FLOAT32X); return true;
    case CE_K_GNUFloat64x: w->unique = basic(e,CE_T_FLOAT64X); return true;
    case CE_K_GNUFloat128x: return fail(e,t,"_Float128x is not available on this target");
    case CE_K_GNUFloat16:
        if (!e->sema->abi->size[CE_T_FLOAT16]) return fail(e,t,"_Float16 is not available on this target");
        ++w->float16s; return true;
    case CE_K_GNUBFloat16:
        if (!e->sema->abi->size[CE_T_BFLOAT16]) return fail(e,t,"__bf16 is not available on this target");
        w->unique = basic(e,CE_T_BFLOAT16); return true;
    case CE_K_AtomicType: {
        CeType *type; if (!type_name(e,t->children[0],&type)) return false;
        w->unique = ce_type_qualified(e->sema,type,CE_Q_ATOMIC); return true;
    }
    case CE_K_StructType: {
        const CeTree *spec = t->children[0];
        Attributes outer = {0};
        while (kind_of(e,spec) == CE_K_GNUAttributedStruct) {
            if (!attributes(e,spec->children[1],&outer)) return false;
            spec = spec->children[0];
        }
        return record_specifier(e,spec,&outer,&w->unique);
    }
    case CE_K_EnumType: return enum_specifier(e,t->children[0],&w->unique);
    case CE_K_TypedefType: {
        const CeUse *use = ce_sema_use(e->sema,t->children[0]);
        if (!use || use->entity->kind != CE_ENTITY_TYPEDEF) return fail(e,t,"typedef name has no binding");
        if (!use->entity->type) {
            /* target-predeclared names */
            CeText n = use->entity->name;
            if (ce_is(n,"__int128_t")) use->entity->type = basic(e,CE_T_INT128);
            else if (ce_is(n,"__uint128_t")) use->entity->type = basic(e,CE_T_UINT128);
            else if (ce_is(n,"__builtin_va_list") || ce_is(n,"__builtin_ms_va_list")) {
                /* x86-64: __va_list_tag[1] */
                use->entity->type = e->sema->va_list_type;
            } else return fail(e,t,"typedef %.*s has no type",(int)n.len,n.data);
        }
        CeType *type = ce_type_copy(e->sema,use->entity->type);
        type->alias = use->entity;
        w->unique = type; return true;
    }
    case CE_K_GNUTypeofType: return type_name(e,t->children[1],&w->unique);
    case CE_K_GNUTypeofExpression: {
        CeExpr *x; if (!expression(e,t->children[1],&x)) return false;
        w->unique = x->type; return true;
    }
    case CE_K_GNUAutoType: w->unique = NULL; w->at = t; return true;
    default: return fail(e,t,"unknown declaration specifier");
    }
}

static bool word_type(Elab *e, const Words *w, const CeTree *at, CeType **out) {
    if (w->unique) {
        if (w->chars || w->shorts || w->ints || w->longs || w->floats || w->doubles || w->signeds ||
            w->unsigneds || w->complexes || w->int128s || w->float16s) return fail(e,at,"invalid type specifier combination");
        *out = ce_type_qualified(e->sema,w->unique,w->qualifiers); return true;
    }
    unsigned kind;
    bool is_unsigned = w->unsigneds > 0, is_signed = w->signeds > 0;
    if (w->float16s) {
        /* _Float16 stands alone or with _Complex. */
        if (w->float16s > 1 || w->chars || w->shorts || w->ints || w->longs || w->floats || w->doubles ||
            w->signeds || w->unsigneds || w->int128s) return fail(e,at,"invalid type specifier combination");
        kind = CE_T_FLOAT16;
    } else if (w->floats || w->doubles) {
        kind = w->floats ? CE_T_FLOAT : w->longs ? CE_T_LDOUBLE : CE_T_DOUBLE;
    } else if (w->chars) kind = is_unsigned ? CE_T_UCHAR : is_signed ? CE_T_SCHAR : CE_T_CHAR;
    else if (w->shorts) kind = is_unsigned ? CE_T_USHORT : CE_T_SHORT;
    else if (w->int128s) kind = is_unsigned ? CE_T_UINT128 : CE_T_INT128;
    else if (w->longs >= 2) kind = is_unsigned ? CE_T_ULLONG : CE_T_LLONG;
    else if (w->longs == 1) kind = is_unsigned ? CE_T_ULONG : CE_T_LONG;
    else if (w->ints || is_signed || is_unsigned) kind = is_unsigned ? CE_T_UINT : CE_T_INT;
    else if (w->complexes) kind = CE_T_DOUBLE;
    else return fail(e,at,"declaration specifiers name no type");
    CeType *type = basic(e,kind);
    if (w->complexes) { CeType *c = ce_type_new(e->sema,CE_T_COMPLEX); c->base = type; type = c; }
    *out = ce_type_qualified(e->sema,type,w->qualifiers);
    return true;
}

static bool specifiers(Elab *e, const CeTree *t, Spec *s) {
    Words w = {0};
    *s = (Spec){0};
    if (!specifier_word(e,t,&w,s)) return false;
    if (w.at && !w.unique && !w.chars && !w.ints && !w.longs) { s->type = NULL; return true; } /* __auto_type */
    if (!word_type(e,&w,t,&s->type)) return false;
    if (s->attributes.has_mode && !(s->type = apply_mode(e,s->type,&s->attributes,t))) return false;
    return true;
}

/* ---- declarators (§6.7.6) -------------------------------------------------- */

static bool parameters(Elab *e, const CeTree *list, CeType *function, bool definition);

static CeType *pointer_chain(Elab *e, CeType *base, const CeTree *p, Attributes *a) {
    for (;;) {
        Words w = {0};
        Spec s = {0};
        if (!specifier_word(e,p->children[0],&w,&s)) return NULL;
        if (a) {
            if (s.attributes.aligned > a->aligned) a->aligned = s.attributes.aligned;
        }
        base = ce_type_qualified(e->sema,ce_type_pointer(e->sema,base),w.qualifiers);
        if (kind_of(e,p) == CE_K_NestedPointer) p = p->children[1];
        else return base;
    }
}

static bool array_length(Elab *e, const CeTree *size, long long *length) {
    if (!size || kind_of(e,size) == CE_K_NoExpression) { *length = -1; return true; }
    if (kind_of(e,size) == CE_K_SomeExpression) size = size->children[0];
    CeExpr *x; ++e->folded; bool ok = expression(e,size,&x); --e->folded;
    if (!ok) return false;
    *length = x->constant ? (long long)x->value : -2;
    if (x->constant && value_negative(e,x)) return fail(e,size,"array size is negative");
    return true;
}

/* derive(T, D): the type of the identifier declared by declarator D with
   base type T, and its name token. Attributes on the declarator go to `a`. */
static bool derive(Elab *e, CeType *t, const CeTree *d, CeType **out, const CeTree **name,
                   Attributes *a, const CeTree **function_node) {
    for (size_t depth = 0; d && depth < 100000; ++depth) {
        switch (kind_of(e,d)) {
        case CE_K_Declarator: case CE_K_SomeDeclarator: case CE_K_ParenthesizedDeclarator:
        case CE_K_AbstractDeclarator: case CE_K_SomeAbstractDeclarator: case CE_K_ParenthesizedAbstractDeclarator:
            d = d->children[0]; continue;
        case CE_K_NoDeclarator: case CE_K_NoAbstractDeclarator: *out = t; return true;
        case CE_K_PointerDeclarator: case CE_K_PointerAbstractDeclarator:
            if (!(t = pointer_chain(e,t,d->children[0],a))) return false;
            d = d->children[1]; continue;
        case CE_K_AbstractPointer:
            if (!(t = pointer_chain(e,t,d->children[0],a))) return false;
            *out = t; return true;
        case CE_K_NameDeclarator: if (name) *name = d->children[0]; *out = t; return true;
        case CE_K_ArrayDeclarator: case CE_K_NestedAbstractArray: {
            long long n; if (!array_length(e,d->children[2],&n)) return false;
            if (!(t = array_type(e,t,n,d))) return false;
            /* qualifiers in [] adjust a parameter's pointer */
            Words w = {0}; if (!specifier_word(e,d->children[1],&w,NULL)) return false;
            t->array_qualifiers = w.qualifiers;
            d = d->children[0]; continue;
        }
        case CE_K_AbstractArray: {
            long long n; if (!array_length(e,d->children[1],&n)) return false;
            if (!(t = array_type(e,t,n,d))) return false;
            Words w = {0}; if (!specifier_word(e,d->children[0],&w,NULL)) return false;
            t->array_qualifiers = w.qualifiers;
            *out = t; return true;
        }
        case CE_K_StaticArrayDeclarator: case CE_K_QualifiedStaticArrayDeclarator:
        case CE_K_NestedAbstractStaticArray: case CE_K_NestedAbstractQualifiedStaticArray: {
            long long n; if (!array_length(e,d->children[2],&n)) return false;
            if (!(t = array_type(e,t,n,d))) return false;
            Words w = {0}; if (!specifier_word(e,d->children[1],&w,NULL)) return false;
            t->array_qualifiers = w.qualifiers;
            d = d->children[0]; continue;
        }
        case CE_K_AbstractStaticArray: case CE_K_AbstractQualifiedStaticArray: {
            long long n; if (!array_length(e,d->children[1],&n)) return false;
            if (!(t = array_type(e,t,n,d))) return false;
            Words w = {0}; if (!specifier_word(e,d->children[0],&w,NULL)) return false;
            t->array_qualifiers = w.qualifiers;
            *out = t; return true;
        }
        case CE_K_VariableArrayDeclarator: case CE_K_NestedAbstractVariableArray:
            if (!(t = array_type(e,t,-2,d))) return false;
            d = d->children[0]; continue;
        case CE_K_AbstractVariableArray:
            *out = array_type(e,t,-2,d); return *out != NULL;
        case CE_K_FunctionDeclarator: case CE_K_OldFunctionDeclarator:
        case CE_K_NestedAbstractFunction: case CE_K_AbstractFunction: {
            if (t->kind == CE_T_ARRAY || t->kind == CE_T_FUNCTION)
                return fail(e,d,"a function cannot return an array or function type");
            CeType *f = ce_type_new(e->sema,CE_T_FUNCTION); f->base = t;
            unsigned k = kind_of(e,d);
            const CeTree *list = k == CE_K_AbstractFunction ? d->children[0] : d->children[1];
            if (k == CE_K_OldFunctionDeclarator) f->prototype = false;
            else if (!parameters(e,list,f,false)) return false;
            if (function_node && !*function_node && (k == CE_K_FunctionDeclarator || k == CE_K_OldFunctionDeclarator)) {
                /* remember the derivation nearest the name: the definition's parameters */
                const CeTree *inner = d->children[0];
                while (kind_of(e,inner) == CE_K_GNUAttributedDeclarator || kind_of(e,inner) == CE_K_GNUAsmDeclarator)
                    inner = inner->children[0];
                if (kind_of(e,inner) == CE_K_NameDeclarator) *function_node = d;
            }
            t = f;
            if (k == CE_K_AbstractFunction) { *out = t; return true; }
            d = d->children[0]; continue;
        }
        case CE_K_GNUAttributedDeclarator: case CE_K_GNUAttributedAbstractDeclarator:
            if (a && !attributes(e,d->children[1],a)) return false;
            d = d->children[0]; continue;
        case CE_K_GNUAsmDeclarator: d = d->children[0]; continue;
        default: return fail(e,d,"unknown declarator form");
        }
    }
    *out = t; return true;
}

/* §6.7.6.3p7–8: parameter type adjustment. */
static CeType *adjust_parameter(Elab *e, CeType *t) {
    if (t->kind == CE_T_ARRAY) {
        CeType *p = ce_type_pointer(e->sema,t->base);
        return ce_type_qualified(e->sema,p,t->array_qualifiers);
    }
    if (t->kind == CE_T_FUNCTION) return ce_type_pointer(e->sema,t);
    return t;
}

static bool parameters(Elab *e, const CeTree *list, CeType *function, bool definition) {
    (void)definition;
    unsigned k = kind_of(e,list);
    if (k == CE_K_NoParameterTypes) { function->prototype = false; return true; }
    if (k == CE_K_SomeParameterTypes) { list = list->children[0]; k = kind_of(e,list); }
    function->prototype = true;
    function->variadic = k == CE_K_VariadicParameterTypes;
    const CeTree *items = list->children[0];
    CeType **types = ce_alloc(&e->sema->arena,(items->len+1)*sizeof(*types)); size_t n = 0;
    for (size_t i = 0; i < items->len; ++i) {
        const CeTree *p = items->children[i];
        Spec s; if (!specifiers(e,p->children[0],&s)) return false;
        if (!s.type) return fail(e,p,"__auto_type is not a parameter type");
        CeType *t; const CeTree *name = NULL; Attributes a = s.attributes;
        if (!derive(e,s.type,p->children[1],&t,&name,&a,NULL)) return false;
        if (a.has_mode && !(t = apply_mode(e,t,&a,p))) return false;
        if (items->len == 1 && !name && ce_type_void(t) && !t->qualifiers && !function->variadic) break;
        if (ce_type_void(t)) return fail(e,p,"void is only allowed as the sole unnamed parameter");
        t = adjust_parameter(e,t);
        types[n++] = t;
        if (name) {
            CeEntity *entity = ce_pointer_get(&e->sema->entity_of,name);
            if (entity && entity->kind == CE_ENTITY_PARAMETER) entity->type = t;
        }
    }
    function->parameters = types; function->nparameters = n;
    return true;
}

/* ---- records and enumerations (§6.7.2.1–6.7.2.2) --------------------------- */

static unsigned long long round_up(unsigned long long n, unsigned long long a) { return a ? (n+a-1)/a*a : n; }

typedef struct { CeMember member; unsigned align; bool member_packed; } Pending;

static bool struct_members(Elab *e, const CeTree *list, Pending **items, size_t *n, size_t *capacity);

/* The record whose members are being declared, for naming anonymous member types. */
static CeType *enclosing_record;
/* Record how a member of the enclosing record reaches an object of an anonymous
   record type: `.m`, then `[0]` per array and `*` per pointer. */
static void witness(Elab *e, const CeType *t, CeText member) {
    if (!enclosing_record) return;
    char suffix[512]; size_t at = 0;
    at += (size_t)snprintf(suffix,sizeof(suffix),".%.*s",(int)member.len,member.data);
    for (size_t depth = 0; depth < 16; ++depth) {
        if (t->kind == CE_T_ARRAY) { at += (size_t)snprintf(suffix+at,sizeof(suffix)-at,"[0]"); t = t->base; }
        else if (t->kind == CE_T_POINTER) { at += (size_t)snprintf(suffix+at,sizeof(suffix)-at,"[0]"); t = t->base; }
        else break;
        if (at >= sizeof(suffix)-8) return;
    }
    if (!(ce_type_record(t) || t->kind == CE_T_ENUM) || !t->tag || t->tag->name.len || t->tag->naming_alias ||
        t->tag->witness_record) return;
    if (t->tag->witness_object) return;
    t->tag->witness_record = enclosing_record;
    t->tag->witness_path = ce_alloc(&e->sema->arena,at+1);
    memcpy(t->tag->witness_path,suffix,at+1);
}

/* An object can name an anonymous record its type reaches, wherever the object
   is visible: __typeof__((table)[0]). Only expressions inside the object's
   scope have such a type. */
static void witness_object(Elab *e, CeEntity *object, const CeType *t) {
    char suffix[512]; size_t at = 0; suffix[0] = 0;
    for (size_t depth = 0; depth < 16 && (t->kind == CE_T_ARRAY || t->kind == CE_T_POINTER); ++depth) {
        at += (size_t)snprintf(suffix+at,sizeof(suffix)-at,"[0]"); t = t->base;
    }
    if (!(ce_type_record(t) || t->kind == CE_T_ENUM) || !t->tag || t->tag->name.len || t->tag->naming_alias ||
        t->tag->witness_record || t->tag->witness_object) return;
    t->tag->witness_object = object;
    t->tag->witness_path = ce_alloc(&e->sema->arena,at+1);
    memcpy(t->tag->witness_path,suffix,at+1);
}

static bool struct_declaration(Elab *e, const CeTree *d, Pending **items, size_t *n, size_t *capacity) {
    switch (kind_of(e,d)) {
    case CE_K_StructStaticAssertion: return static_assertion(e,d->children[0]);
    case CE_K_GNUEmptyStructDeclaration: case CE_K_GNUStructPragma: return true;
    case CE_K_GNUStructDeclarationExtension: return struct_declaration(e,d->children[0],items,n,capacity);
    case CE_K_StructDeclaration: break;
    default: return fail(e,d,"unknown member declaration");
    }
    Spec s; if (!specifiers(e,d->children[0],&s)) return false;
    if (!s.type) return fail(e,d,"__auto_type is not a member type");
    const CeTree *declarators = d->children[1];
    if (kind_of(e,declarators) == CE_K_NoStructDeclarators) {
        /* §6.7.2.1p13: an anonymous structure or union member. */
        if (!ce_type_record(s.type)) return true;
        if (*n == *capacity) { *capacity = *capacity ? *capacity*2 : 8; *items = ce_resize(*items,*capacity,sizeof(**items)); }
        unsigned align = s.alignas_ > s.attributes.aligned ? s.alignas_ : s.attributes.aligned;
        (*items)[(*n)++] = (Pending){{.type=s.type,.anonymous=true,.declaration=d},align,s.attributes.packed};
        if (s.type->tag && enclosing_record) s.type->tag->anonymous_in = enclosing_record;
        return true;
    }
    const CeTree *list = declarators->children[0];
    for (size_t i = 0; i < list->len; ++i) {
        const CeTree *m = list->children[i];
        Attributes a = s.attributes;
        CeType *t; const CeTree *name = NULL;
        CeMember member = {.declaration=m};
        if (kind_of(e,m) == CE_K_BitField) {
            if (!derive(e,s.type,m->children[0],&t,&name,&a,NULL)) return false;
            long long width; if (!constant_value(e,m->children[1],&width)) return false;
            if (!ce_type_integer(t)) return fail(e,m,"a bit-field needs an integer type");
            unsigned limit = t->kind == CE_T_BOOL ? 1 : ce_kind_width(e->sema->abi,ce_type_integer_kind(t));
            if (width < 0 || (unsigned long long)width > limit || (!width && name))
                return fail(e,m,"invalid bit-field width");
            member.bitfield = true; member.bit_width = (unsigned)width;
        } else if (!derive(e,s.type,m->children[0],&t,&name,&a,NULL)) return false;
        if (a.has_mode && !(t = apply_mode(e,t,&a,m))) return false;
        member.type = t;
        if (name) member.name = ce_sema_name(e->sema,name);
        if (ce_type_void(t) || t->kind == CE_T_FUNCTION)
            return fail(e,m,"a member needs an object type");
        if (member.name.len)
            for (size_t j = 0; j < *n; ++j)
                if (ce_equal((*items)[j].member.name,member.name)) return fail(e,m,"duplicate member name");
        if (*n == *capacity) { *capacity = *capacity ? *capacity*2 : 8; *items = ce_resize(*items,*capacity,sizeof(**items)); }
        unsigned align = s.alignas_ > a.aligned ? s.alignas_ : a.aligned;
        (*items)[(*n)++] = (Pending){member,align,a.packed};
        if (name) witness(e,t,member.name);
    }
    return true;
}
static bool struct_members(Elab *e, const CeTree *list, Pending **items, size_t *n, size_t *capacity) {
    for (size_t i = 0; list && i < list->len; ++i)
        if (!struct_declaration(e,list->children[i],items,n,capacity)) return false;
    return true;
}

static bool members_overlap(const CeMember *a, const CeMember *b) {
    if (a->name.len && b->name.len) return ce_equal(a->name,b->name);
    if (a->anonymous && a->type->tag && a->type->tag->record) {
        CeRecord *r = a->type->tag->record;
        for (size_t i = 0; i < r->nmembers; ++i) if (members_overlap(&r->members[i],b)) return true;
    }
    if (b->anonymous && b->type->tag && b->type->tag->record) {
        CeRecord *r = b->type->tag->record;
        for (size_t i = 0; i < r->nmembers; ++i) if (members_overlap(a,&r->members[i])) return true;
    }
    return false;
}

/* x86-64 System V record layout, as GCC lays out bit-fields. */
static void layout(Elab *e, CeRecord *r, bool is_union, Pending *items, size_t n, const Attributes *a) {
    unsigned long long bits = 0, size_bits = 0;
    unsigned long long ms_start = 0, ms_unit = 0, ms_used = 0;
    bool ms_previous = false;
    unsigned align = 1;
    r->members = ce_alloc(&e->sema->arena,(n ? n : 1)*sizeof(*r->members));
    for (size_t i = 0; i < n; ++i) {
        CeMember m = items[i].member;
        bool packed = a->packed || items[i].member_packed;
        unsigned type_align = ce_type_align(e->sema,m.type);
        unsigned member_align = packed ? 1 : type_align;
        if (items[i].align > member_align) member_align = items[i].align;
        if (items[i].align || items[i].member_packed) r->attributed = true;
        unsigned long long start = is_union ? 0 : bits;
        if (m.bitfield) {
            unsigned long long unit = ce_type_size(e->sema,m.type)*8;
            if (e->sema->abi->record_layout == CE_RECORD_MS) {
                if (!m.bit_width) {
                    /* MS zero-width fields affect layout only after a bit-field. */
                    if (ms_previous) {
                        start = round_up(start,(unsigned long long)member_align*8);
                        if (!is_union && member_align > align) align = member_align;
                        if (!is_union) bits = start;
                        if (start > size_bits) size_bits = start;
                    }
                    ms_unit = ms_used = 0; ms_previous = false;
                } else {
                    if (is_union || !ms_previous || ms_unit != unit || ms_used+m.bit_width > unit || items[i].align) {
                        ms_start = round_up(start,(unsigned long long)member_align*8);
                        ms_unit = unit; ms_used = 0;
                    }
                    start = ms_start+ms_used;
                    ms_used += m.bit_width; ms_previous = true;
                    unsigned long long end = ms_start+unit;
                    if (!is_union) bits = end;
                    if (end > size_bits) size_bits = end;
                    if (!is_union && member_align > align) align = member_align;
                }
                m.offset = start/8; m.bit_offset = (unsigned)(start%8);
                r->members[r->nmembers++] = m;
                continue;
            }
            if (m.bit_width == 0) {
                start = round_up(start,(unsigned long long)type_align*8);
            } else {
                if (!packed && unit && (start % ((unsigned long long)type_align*8)) + m.bit_width > unit)
                    start = round_up(start,(unsigned long long)type_align*8);
                if (items[i].align) start = round_up(start,(unsigned long long)items[i].align*8);
                /* named bit-fields contribute their type's alignment (SysV) */
                if (m.name.len && member_align > align) align = member_align;
            }
            m.offset = start/8; m.bit_offset = (unsigned)(start%8);
            unsigned long long end = start + m.bit_width;
            if (!is_union) bits = end;
            if (end > size_bits) size_bits = end;
        } else {
            ms_previous = false; ms_unit = ms_used = 0;
            start = round_up(start,(unsigned long long)member_align*8);
            m.offset = start/8;
            bool flexible = m.type->kind == CE_T_ARRAY && m.type->length < 0 && i+1 == n && !is_union;
            if (flexible) r->flexible = true;
            unsigned long long end = start + (flexible ? 0 : ce_type_size(e->sema,m.type)*8);
            if (!is_union) bits = end;
            if (end > size_bits) size_bits = end;
            if (member_align > align) align = member_align;
        }
        r->members[r->nmembers++] = m;
    }
    if (a->aligned > align) align = a->aligned;
    if (a->aligned) r->attributed = true;
    r->align = align;
    r->size = round_up((size_bits+7)/8,align);
    r->packed = a->packed;
    r->complete = true;
}

static bool record_specifier(Elab *e, const CeTree *t, const Attributes *outer, CeType **out) {
    CeEntity *tag = ce_pointer_get(&e->sema->tag_of,t);
    if (!tag) return fail(e,t,"structure specifier has no binding");
    CeType *type = ce_type_new(e->sema,tag->kind == CE_ENTITY_UNION ? CE_T_UNION : CE_T_STRUCT);
    type->tag = tag;
    if (!tag->record) { tag->record = ce_alloc(&e->sema->arena,sizeof(*tag->record)); *tag->record = (CeRecord){0}; }
    unsigned k = kind_of(e,t);
    if (k == CE_K_StructDefinition || k == CE_K_GNUEmptyStruct) {
        Attributes a = *outer;
        if (!attributes(e,t->children[0],&a)) return false;
        Pending *items = NULL; size_t n = 0, capacity = 0;
        CeType *saved = enclosing_record; enclosing_record = type;
        bool ok = k != CE_K_StructDefinition || struct_members(e,t->children[2],&items,&n,&capacity);
        enclosing_record = saved;
        if (!ok) { free(items); return false; }
        bool prior_named = false;
        for (size_t i = 0; i < n; ++i) {
            CeType *member = items[i].member.type;
            bool flexible = member->kind == CE_T_ARRAY && member->length == -1 &&
                i+1 == n && tag->kind == CE_ENTITY_STRUCT && prior_named && complete_object(member->base);
            if (!complete_object(member) && !flexible) {
                fail(e,items[i].member.declaration,"a member needs a complete type or a final flexible array");
                free(items); return false;
            }
            prior_named |= items[i].member.name.len != 0 || items[i].member.anonymous;
            for (size_t j = 0; j < i; ++j)
                if (members_overlap(&items[i].member,&items[j].member)) {
                    fail(e,items[i].member.declaration,"duplicate member name through an anonymous member");
                    free(items); return false;
                }
        }
        layout(e,tag->record,tag->kind == CE_ENTITY_UNION,items,n,&a);
        free(items);
    }
    *out = type;
    return true;
}

static unsigned packed_enum_kind(Elab *e, long long low, long long high, bool wide) {
    static const unsigned signed_kinds[] = {CE_T_SCHAR,CE_T_SHORT,CE_T_INT,CE_T_LONG,CE_T_LLONG};
    static const unsigned unsigned_kinds[] = {CE_T_UCHAR,CE_T_USHORT,CE_T_UINT,CE_T_ULONG,CE_T_ULLONG};
    const unsigned *kinds = low < 0 && !wide ? signed_kinds : unsigned_kinds;
    for (size_t i = 0; i < sizeof(signed_kinds)/sizeof(*signed_kinds); ++i) {
        unsigned bits = ce_kind_width(e->sema->abi,kinds[i]);
        if (bits == 64) return kinds[i];
        if (wide) continue;
        if (kinds == signed_kinds) {
            long long bound = 1LL << (bits-1);
            if (low >= -bound && high < bound) return kinds[i];
        } else if ((unsigned long long)high < (1ULL << bits)) return kinds[i];
    }
    return CE_T_ULLONG;
}

static bool enum_specifier(Elab *e, const CeTree *t, CeType **out) {
    const CeTree *attributed = t;
    while (kind_of(e,t) == CE_K_GNUAttributedEnum) t = t->children[0];
    CeEntity *tag = ce_pointer_get(&e->sema->tag_of,t);
    if (!tag) return fail(e,t,"enumeration specifier has no binding");
    CeType *type = ce_type_new(e->sema,CE_T_ENUM); type->tag = tag;
    if (!tag->record) { tag->record = ce_alloc(&e->sema->arena,sizeof(*tag->record)); *tag->record = (CeRecord){0}; }
    unsigned k = kind_of(e,t);
    if (k == CE_K_EnumDefinition || k == CE_K_EnumDefinitionComma ||
        k == CE_K_GNUEnumDefinition || k == CE_K_GNUEnumDefinitionComma) {
        bool leading = k == CE_K_GNUEnumDefinition || k == CE_K_GNUEnumDefinitionComma;
        const CeTree *list = t->children[leading ? 2 : 1];
        long long next = 0, low = 0, high = 0; bool any = false, wide = false, unsigned_next = false;
        CeEntity **members = ce_alloc(&e->sema->arena,list->len*sizeof(*members));
        for (size_t i = 0; i < list->len; ++i) {
            const CeTree *item = list->children[i];
            CeEntity *entity = ce_pointer_get(&e->sema->entity_of,item->children[0]);
            if (!entity) return fail(e,item,"enumerator has no binding");
            if (kind_of(e,item) == CE_K_EnumeratorValue) {
                CeExpr *x; if (!expression(e,item->children[1],&x)) return false;
                if (!x->constant) return fail(e,item,"enumerator value is not an integer constant expression");
                next = (long long)x->value;
                unsigned_next = !signed_type(e,x->type) && x->value > LLONG_MAX;
            }
            wide |= unsigned_next;
            entity->value = next; entity->value_known = true;
            /* §6.7.2.2p3, GNU: an enumerator within int has type int */
            entity->type = !unsigned_next && next >= INT_MIN && next <= INT_MAX ? basic(e,CE_T_INT) : NULL;
            if (e->sema->abi->enum_policy == CE_ENUM_INT) {
                unsigned bits = ce_kind_width(e->sema->abi,CE_T_INT);
                unsigned long long value = (unsigned long long)next & ((1ULL << bits)-1);
                entity->value = value >= (1ULL << (bits-1))
                    ? (long long)value - (1LL << bits) : (long long)value;
                entity->type = basic(e,CE_T_INT);
            }
            if (entity->type && kind_of(e,item) == CE_K_EnumeratorValue)
                convert(e,ce_pointer_get(&e->sema->exprs,item->children[1]),entity->type);
            members[i] = entity;
            if (!unsigned_next && (!any || next < low)) low = next;
            if (!unsigned_next && (!any || next > high)) high = next;
            any = true;
            unsigned long long incremented = (unsigned long long)next + 1ULL;
            if (i+1 < list->len && kind_of(e,list->children[i+1]) == CE_K_Enumerator &&
                unsigned_next && !incremented) return fail(e,item,"enumerator successor exceeds the supported integer range");
            if (next == LLONG_MAX) unsigned_next = true;
            next = (long long)incremented;
        }
        Attributes a = {0};
        if (leading && !attributes(e,t->children[0],&a)) return false;
        size_t nattributes = 0;
        for (const CeTree *node = attributed; node != t; node = node->children[0]) ++nattributes;
        const CeTree **tail = ce_alloc(&e->sema->arena,nattributes*sizeof(*tail));
        size_t at = 0;
        for (const CeTree *node = attributed; node != t; node = node->children[0]) tail[at++] = node->children[1];
        while (at) if (!attributes(e,tail[--at],&a)) return false;
        if (!e->sema->clang && a.packed && a.aligned) {
            if (a.enum_layout == 2) a.packed = false;
            a.aligned = 0;
        }
        /* GCC: unsigned int unless a value is negative; wider when needed. */
        unsigned kind;
        if (low >= 0) kind = (unsigned long long)high <= UINT_MAX ? CE_T_UINT : CE_T_ULONG;
        else kind = low >= INT_MIN && high <= INT_MAX ? CE_T_INT : CE_T_LONG;
        if (wide) kind = CE_T_ULONG;
        /* GNU long or unsigned long follows the target's 64-bit type */
        if (kind == CE_T_LONG && e->sema->abi->size[CE_T_LONG] < 8) kind = CE_T_LLONG;
        if (kind == CE_T_ULONG && e->sema->abi->size[CE_T_ULONG] < 8) kind = CE_T_ULLONG;
        if (a.packed) kind = packed_enum_kind(e,low,high,wide);
        /* MSVC: every enumeration has type int */
        if (e->sema->abi->enum_policy == CE_ENUM_INT) kind = CE_T_INT;
        tag->record->enum_kind = kind;
        tag->record->complete = true;
        tag->record->size = e->sema->abi->size[kind];
        tag->record->align = e->sema->abi->align[kind];
        if (a.aligned > tag->record->align) tag->record->align = a.aligned;
        tag->record->packed = a.packed && e->sema->abi->enum_policy != CE_ENUM_INT;
        tag->record->attributed = a.packed || a.aligned || a.has_mode;
        /* In GNU's extended range, both compilers give every enumerator the
           completed enumeration's integer type, including small members.
           Earlier initializer references retain their point-of-use types. */
        bool extended = wide || low < INT_MIN || high > INT_MAX;
        for (size_t i = 0; i < list->len; ++i) {
            if (!members[i]->type || extended) members[i]->type = basic(e,kind);
            const CeTree *item = list->children[i];
            if (kind_of(e,item) == CE_K_EnumeratorValue)
                convert(e,ce_pointer_get(&e->sema->exprs,item->children[1]),members[i]->type);
        }
    }
    *out = type;
    return true;
}

/* ---- type names ------------------------------------------------------------ */

static bool type_name(Elab *e, const CeTree *t, CeType **out) {
    CeType *cached = ce_pointer_get(&e->sema->type_names,t);
    if (cached) { *out = cached; return true; }
    if (kind_of(e,t) != CE_K_TypeName) return fail(e,t,"expected a type name");
    Spec s; if (!specifiers(e,t->children[0],&s)) return false;
    if (!s.type) return fail(e,t,"__auto_type is not a type name");
    Attributes a = s.attributes;
    if (!derive(e,s.type,t->children[1],out,NULL,&a,NULL)) return false;
    ce_pointer_put(&e->sema->type_names,t,*out);
    return true;
}

/* ---- conversions (§6.3) ---------------------------------------------------- */

static void add_conversion(CeExpr *x, unsigned kind, CeType *to) {
    if (x->nconversions < sizeof(x->conversions)/sizeof(*x->conversions))
        x->conversions[x->nconversions++] = (CeConversion){kind,to};
}
/* The operand's type after the conversions its context applied so far. */
static CeType *current(const CeExpr *x) { return x->nconversions ? x->conversions[x->nconversions-1].to : x->type; }

static bool is_null_pointer_constant(const CeExpr *x) {
    /* §6.3.2.3p3: an integer constant 0, or such cast to void * */
    if (x->type->kind == CE_T_POINTER) return x->null_pointer;
    return ce_type_integer(x->type) && x->constant && x->value == 0;
}

static bool builtin_designator(const CeExpr *x) { return x->builtin; }

/* §6.3.2.1p2–4: lvalue conversion and array/function decay. */
static CeType *rvalue(Elab *e, CeExpr *x) {
    CeType *t = current(x);
    if (x->nconversions) return t;
    if (t->kind == CE_T_FUNCTION) {
        CeType *p = ce_type_pointer(e->sema,t);
        add_conversion(x,builtin_designator(x) ? CE_CONVERT_BuiltinFnToFnPtr : CE_CONVERT_FunctionToPointerDecay,p);
        return p;
    }
    if (t->kind == CE_T_ARRAY) {
        CeType *p = ce_type_pointer(e->sema,t->base);
        add_conversion(x,CE_CONVERT_ArrayToPointerDecay,p);
        return p;
    }
    if (x->category == CE_LVALUE) {
        /* an atomic object's value is read, then leaves the atomic type */
        if (t->qualifiers & CE_Q_ATOMIC) {
            CeType *atomic = ce_type_copy(e->sema,t); atomic->qualifiers = CE_Q_ATOMIC; atomic->alias = NULL;
            add_conversion(x,CE_CONVERT_LValueToRValue,atomic);
            CeType *u = ce_type_unqualified(e->sema,t);
            add_conversion(x,CE_CONVERT_AtomicToNonAtomic,u);
            return u;
        }
        CeType *u = ce_type_unqualified(e->sema,t);
        if (u->alias && t->qualifiers) u->alias = NULL;
        add_conversion(x,CE_CONVERT_LValueToRValue,u);
        return u;
    }
    return t;
}

/* The conversion from an rvalue of type `from` to type `to`, if any. */
static unsigned conversion_kind(Elab *e, const CeType *from, const CeType *to, bool null_pointer) {
    (void)e;
    if (to->kind == CE_T_VOID) return CE_CONVERT_ToVoid;
    if (to->kind == CE_T_BOOL) {
        if (from->kind == CE_T_BOOL) return 0;
        if (ce_type_integer(from)) return CE_CONVERT_IntegralToBoolean;
        if (ce_type_real_floating(from)) return CE_CONVERT_FloatingToBoolean;
        if (from->kind == CE_T_POINTER) return CE_CONVERT_PointerToBoolean;
        if (from->kind == CE_T_COMPLEX)
            return ce_type_real_floating(from->base) ? CE_CONVERT_FloatingComplexToBoolean : CE_CONVERT_IntegralComplexToBoolean;
        return 0;
    }
    if (ce_type_integer(to)) {
        if (ce_type_integer(from)) {
            if (from->kind == to->kind && (from->kind != CE_T_ENUM || from->tag == to->tag)) return 0;
            return CE_CONVERT_IntegralCast;
        }
        if (ce_type_real_floating(from)) return CE_CONVERT_FloatingToIntegral;
        if (from->kind == CE_T_POINTER) return CE_CONVERT_PointerToIntegral;
        if (from->kind == CE_T_COMPLEX)
            return ce_type_real_floating(from->base) ? CE_CONVERT_FloatingComplexToReal : CE_CONVERT_IntegralComplexToReal;
        return 0;
    }
    if (ce_type_real_floating(to)) {
        if (ce_type_integer(from)) return CE_CONVERT_IntegralToFloating;
        if (ce_type_real_floating(from)) return from->kind == to->kind ? 0 : CE_CONVERT_FloatingCast;
        if (from->kind == CE_T_COMPLEX)
            return ce_type_real_floating(from->base) ? CE_CONVERT_FloatingComplexToReal : CE_CONVERT_IntegralComplexToReal;
        return 0;
    }
    if (to->kind == CE_T_COMPLEX) {
        bool to_float = ce_type_real_floating(to->base);
        if (from->kind == CE_T_COMPLEX) {
            bool from_float = ce_type_real_floating(from->base);
            if (from->base->kind == to->base->kind) return 0;
            if (from_float && to_float) return CE_CONVERT_FloatingComplexCast;
            if (!from_float && !to_float) return CE_CONVERT_IntegralComplexCast;
            return from_float ? CE_CONVERT_FloatingComplexToIntegralComplex : CE_CONVERT_IntegralComplexToFloatingComplex;
        }
        if (ce_type_real_floating(from)) return CE_CONVERT_FloatingRealToComplex;
        return CE_CONVERT_IntegralRealToComplex;
    }
    if (to->kind == CE_T_POINTER) {
        if (null_pointer && !(from->kind == CE_T_POINTER && ce_type_compatible(e->sema,from,to)))
            return CE_CONVERT_NullToPointer;
        if (from->kind == CE_T_POINTER) {
            /* only qualifiers added to the same pointee type: NoOp */
            const CeType *a = from->base, *b = to->base;
            if (ce_type_similar(a,b))
                return ce_type_same(from,to) ? 0 : CE_CONVERT_NoOp;
            return CE_CONVERT_BitCast;
        }
        if (null_pointer) return CE_CONVERT_NullToPointer;
        if (ce_type_integer(from)) return CE_CONVERT_IntegralToPointer;
        return 0;
    }
    if (to->kind == CE_T_UNION && !ce_type_compatible(e->sema,ce_type_unqualified(e->sema,from),ce_type_unqualified(e->sema,to)))
        return CE_CONVERT_ToUnion;
    return 0;
}

/* Convert an operand to `to` as by assignment (§6.5.16.1) or an operator. */
/* §6.5.16.1p1 simple assignment, and every conversion "as if by assignment":
   arithmetic to arithmetic, compatible structures or unions, pointers (an
   incompatible pointer is diagnosed but converts), a null pointer constant to a
   pointer, and a pointer to _Bool. */
static bool assignable(Elab *e, const CeExpr *x, const CeType *from, const CeType *to) {
    from = ce_type_unqualified(e->sema,from); to = ce_type_unqualified(e->sema,to);
    if (ce_type_arithmetic(to) && ce_type_arithmetic(from)) return true;
    if (ce_type_record(to) || ce_type_record(from))
        return ce_type_record(to) && ce_type_record(from) && ce_type_compatible(e->sema,from,to);
    if (to->kind == CE_T_POINTER) return from->kind == CE_T_POINTER || is_null_pointer_constant(x);
    if (to->kind == CE_T_BOOL) return from->kind == CE_T_POINTER;
    return false;
}
static bool converted(Elab *e, const CeTree *at, CeExpr *x, CeType *to) {
    CeType *from = current(x);
    if (from->kind == CE_T_VOID) return fail(e,at,"a void expression has no value");
    if (!from->kind || !assignable(e,x,x->nconversions ? from :
        (from->kind == CE_T_ARRAY ? ce_type_pointer(e->sema,from->base) : from->kind == CE_T_FUNCTION ? ce_type_pointer(e->sema,from) : from),to))
        return fail(e,at,"incompatible types in assignment or initialization");
    convert(e,x,to);
    return true;
}
static void convert(Elab *e, CeExpr *x, CeType *to) {
    CeType *from = rvalue(e,x);
    CeType *target = ce_type_unqualified(e->sema,to);
    unsigned kind = conversion_kind(e,ce_type_unqualified(e->sema,from),target,is_null_pointer_constant(x));
    if (kind) add_conversion(x,kind,target);
    /* a null pointer constant converts directly to an atomic pointer */
    if ((to->qualifiers & CE_Q_ATOMIC) && kind == CE_CONVERT_NullToPointer) {
        CeType *atomic = ce_type_copy(e->sema,target); atomic->qualifiers = CE_Q_ATOMIC;
        x->conversions[x->nconversions-1].to = atomic;
    } else if (to->qualifiers & CE_Q_ATOMIC) {
        CeType *atomic = ce_type_copy(e->sema,target); atomic->qualifiers = CE_Q_ATOMIC;
        add_conversion(x,CE_CONVERT_NonAtomicToAtomic,atomic);
    }
}

/* ---- integer and floating constants (§6.4.4) ------------------------------- */

static bool integer_constant(Elab *e, const CeTree *node, CeExpr **out) {
    CeText s = node->children[0]->spelling;
    size_t i = 0; unsigned base = 10;
    if (s.len > 1 && s.data[0] == '0' && (s.data[1] == 'x' || s.data[1] == 'X')) { base = 16; i = 2; }
    else if (s.len > 1 && s.data[0] == '0' && (s.data[1] == 'b' || s.data[1] == 'B')) { base = 2; i = 2; }
    else if (s.data[0] == '0') base = 8;
    unsigned long long v = 0; bool overflow = false;
    for (; i < s.len; ++i) {
        unsigned c = s.data[i], d;
        if (c >= '0' && c <= '9') d = c-'0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c-'a'+10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c-'A'+10;
        else break;
        if (v > (ULLONG_MAX - d)/base) overflow = true;
        v = v*base + d;
    }
    bool u = false; unsigned l = 0;
    for (; i < s.len; ++i) {
        unsigned c = s.data[i];
        if (c == 'u' || c == 'U') u = true;
        else if (c == 'l' || c == 'L') ++l;
    }
    if (overflow) return fail(e,node,"integer constant is too large");
    /* §6.4.4.1p5 candidate lists */
    static const unsigned decimal[] = {CE_T_INT,CE_T_LONG,CE_T_LLONG};
    static const unsigned other[] = {CE_T_INT,CE_T_UINT,CE_T_LONG,CE_T_ULONG,CE_T_LLONG,CE_T_ULLONG};
    unsigned kinds[6]; size_t n = 0;
    if (u) {
        if (l == 0) kinds[n++] = CE_T_UINT;
        if (l <= 1) kinds[n++] = CE_T_ULONG;
        kinds[n++] = CE_T_ULLONG;
    } else if (base == 10) {
        for (size_t k = l ? (l == 1 ? 1 : 2) : 0; k < 3; ++k) kinds[n++] = decimal[k];
    } else {
        for (size_t k = l ? (l == 1 ? 2 : 4) : 0; k < 6; ++k) kinds[n++] = other[k];
    }
    const CeAbi *abi = e->sema->abi;
    unsigned kind = 0;
    for (size_t k = 0; k < n && !kind; ++k) {
        unsigned w = ce_kind_width(abi,kinds[k]);
        unsigned long long max = ce_kind_signed(abi,kinds[k]) ? (w >= 64 ? (unsigned long long)LLONG_MAX : (1ULL << (w-1))-1)
                                                              : (w >= 64 ? ULLONG_MAX : (1ULL << w)-1);
        if (v <= max) kind = kinds[k];
    }
    /* GCC: a decimal constant beyond long long is unsigned long long. */
    if (!kind) kind = CE_T_ULLONG;
    CeExpr *x = new_expr(e,node,basic(e,kind),CE_RVALUE);
    x->constant = true; x->value = v;
    x->null_pointer = v == 0;
    *out = x; return true;
}

static bool floating_constant(Elab *e, const CeTree *node, CeExpr **out) {
    CeText s = node->children[0]->spelling;
    bool hex = s.len > 1 && s.data[0] == '0' && (s.data[1] == 'x' || s.data[1] == 'X');
    unsigned kind = CE_T_DOUBLE;
    unsigned c = s.data[s.len-1];
    /* No unsuffixed constant ends in f16: a hexadecimal one ends in exponent digits. */
    if (s.len > 3 && (s.data[s.len-3] == 'f' || s.data[s.len-3] == 'F') && s.data[s.len-2] == '1' && c == '6')
        kind = CE_T_FLOAT16;
    else if (c == 'f' || c == 'F') kind = CE_T_FLOAT;
    else if (c == 'l' || c == 'L') kind = CE_T_LDOUBLE;
    (void)hex;
    *out = new_expr(e,node,basic(e,kind),CE_RVALUE);
    return true;
}

/* Decode one C character or escape; returns the code point and the
   number of bytes it occupies in a narrow (UTF-8) literal. */
static size_t escape(CeText s, size_t i, unsigned long *value, unsigned *bytes) {
    unsigned c = s.data[i];
    if (c != '\\') {
        /* a UTF-8 encoded source character */
        unsigned n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        unsigned long v = n == 1 ? c : c & (0xff >> (n+1));
        for (unsigned k = 1; k < n && i+k < s.len; ++k) v = (v << 6) | (s.data[i+k] & 0x3f);
        *value = v; *bytes = n; return n;
    }
    c = s.data[i+1];
    switch (c) {
    case 'n': *value = '\n'; break; case 't': *value = '\t'; break; case 'r': *value = '\r'; break;
    case 'a': *value = 7; break; case 'b': *value = 8; break; case 'f': *value = 12; break;
    case 'v': *value = 11; break; case 'e': case 'E': *value = 27; break;
    case 'x': {
        size_t j = i+2; unsigned long v = 0;
        while (j < s.len) {
            unsigned d = s.data[j];
            if (d >= '0' && d <= '9') v = v*16+d-'0';
            else if (d >= 'a' && d <= 'f') v = v*16+d-'a'+10;
            else if (d >= 'A' && d <= 'F') v = v*16+d-'A'+10;
            else break;
            ++j;
        }
        *value = v; *bytes = 1; return j-i;
    }
    case 'u': case 'U': {
        size_t digits = c == 'u' ? 4 : 8; unsigned long v = 0;
        for (size_t j = 0; j < digits && i+2+j < s.len; ++j) {
            unsigned d = s.data[i+2+j];
            v = v*16 + (d <= '9' ? d-'0' : (d|32)-'a'+10);
        }
        *value = v; *bytes = v < 0x80 ? 1 : v < 0x800 ? 2 : v < 0x10000 ? 3 : 4;
        return 2+digits;
    }
    default:
        if (c >= '0' && c <= '7') {
            size_t j = i+1; unsigned long v = 0;
            while (j < s.len && j < i+4 && s.data[j] >= '0' && s.data[j] <= '7') v = v*8+s.data[j++]-'0';
            *value = v; *bytes = 1; return j-i;
        }
        *value = c; break;
    }
    *bytes = 1; return 2;
}

static unsigned literal_prefix(CeText s, size_t *skip) {
    if (s.len > 2 && s.data[0] == 'u' && s.data[1] == '8') { *skip = 2; return 8; }
    if (s.data[0] == 'L') { *skip = 1; return 'L'; }
    if (s.data[0] == 'u') { *skip = 1; return 'u'; }
    if (s.data[0] == 'U') { *skip = 1; return 'U'; }
    *skip = 0; return 0;
}
static unsigned prefix_kind(Elab *e, unsigned prefix) {
    const CeAbi *abi = e->sema->abi;
    return prefix == 'L' ? abi->wchar_type : prefix == 'u' ? abi->char16_type : prefix == 'U' ? abi->char32_type : CE_T_CHAR;
}

static bool character_constant(Elab *e, const CeTree *node, CeExpr **out) {
    CeText s = node->children[0]->spelling; size_t skip;
    unsigned prefix = literal_prefix(s,&skip);
    unsigned long long value = 0; size_t count = 0;
    for (size_t i = skip+1; i+1 < s.len;) {
        unsigned long v; unsigned bytes; i += escape(s,i,&v,&bytes);
        if (!prefix && bytes > 1 && s.data[i-bytes] != '\\') {
            /* a multibyte source character contributes its bytes */
            value = (value << (8*bytes)) | v; count += bytes; continue;
        }
        value = prefix ? v : (value << 8) | (v & 0xff); ++count;
    }
    CeType *type = prefix ? basic(e,prefix_kind(e,prefix)) : basic(e,CE_T_INT);
    CeExpr *x = new_expr(e,node,type,CE_RVALUE);
    x->constant = true;
    /* a single plain char constant has the value of a char (§6.4.4.4p10) */
    if (!prefix && count == 1) value = (unsigned long long)(long long)(signed char)value;
    x->value = truncate_value(e,type,value);
    *out = x; return true;
}

static bool string_literal(Elab *e, const CeTree *node, CeExpr **out) {
    const CeTree *list = node->children[0];
    unsigned prefix = 0;
    for (size_t k = 0; k < list->len; ++k) {
        size_t skip; unsigned p = literal_prefix(list->children[k]->spelling,&skip);
        if (p && p != 8) prefix = p;
    }
    unsigned long long length = 1;
    for (size_t k = 0; k < list->len; ++k) {
        CeText s = list->children[k]->spelling; size_t skip;
        literal_prefix(s,&skip);
        for (size_t i = skip+1; i+1 < s.len;) {
            unsigned long v; unsigned bytes; i += escape(s,i,&v,&bytes);
            if (!prefix) length += bytes;
            else if (prefix == 'u' && v >= 0x10000) length += 2;
            else ++length;
        }
    }
    CeType *element = basic(e,prefix_kind(e,prefix));
    *out = new_expr(e,node,ce_type_array(e->sema,element,(long long)length),CE_LVALUE);
    return true;
}

/* ---- builtins -------------------------------------------------------------- */

/* Signature codes: v void, b _Bool, i int, u unsigned, l long, L unsigned long,
   x long long, X unsigned long long, z size_t, p void *, P const void *,
   c char *, s const char *, f float, d double, V va_list, h unsigned short. */
typedef struct { const char *name, *signature; bool variadic; } Builtin;
static const Builtin builtins[] = {
    {"__builtin_expect","lll",false}, {"__builtin_dynamic_object_size","zPi",false},
    {"__builtin_frame_address","pu",false}, {"__builtin_return_address","pu",false},
    {"__builtin_prefetch","vP",true}, {"__builtin_stack_save","p",false}, {"__builtin_stack_restore","vp",false},
    {"__builtin_object_size","zPi",false}, {"__builtin_va_arg_pack_len","i",false},
    {"__builtin___memset_chk","ppizz",false}, {"__builtin___memmove_chk","ppPzz",false},
    {"__builtin___memcpy_chk","ppPzz",false}, {"__builtin___mempcpy_chk","ppPzz",false},
    {"__builtin___vsprintf_chk","icizsV",false}, {"__builtin___vsnprintf_chk","iczizsV",false},
    {"__builtin___sprintf_chk","icizs",true}, {"__builtin___snprintf_chk","iczizs",true},
    {"__builtin___strncpy_chk","ccszz",false}, {"__builtin___strncat_chk","ccszz",false},
    {"__builtin___strcpy_chk","ccsz",false}, {"__builtin___strcat_chk","ccsz",false},
    {"__builtin___stpncpy_chk","ccszz",false}, {"__builtin___stpcpy_chk","ccsz",false},
    {"__builtin_free","vp",false}, {"__builtin_memset","ppiz",false}, {"__builtin_memcpy","ppPz",false},
    {"__builtin_alloca","pz",false}, {"__builtin_va_end","vV",false}, {"__builtin_va_copy","vVV",false},
    {"__builtin_va_start","vV",true}, {"__builtin_bswap16","hh",false}, {"__builtin_bswap32","uu",false},
    {"__builtin_bswap64","LL",false}, {"__builtin_popcount","iu",false}, {"__builtin_popcountl","iL",false},
    {"__builtin_popcountll","iX",false}, {"__builtin_clz","iu",false}, {"__builtin_clzl","iL",false},
    {"__builtin_clzll","iX",false}, {"__builtin_ctz","iu",false}, {"__builtin_ctzl","iL",false},
    {"__builtin_ctzll","iX",false}, {"__builtin_inff","f",false}, {"__builtin_inf","d",false},
    {"__builtin_huge_val","d",false}, {"__builtin_huge_valf","f",false}, {"__builtin_nanf","fs",false},
    {"__builtin_nan","ds",false}, {"__builtin_saddll_overflow","bxxx*",false},
    {"__builtin_trap","v",false}, {"__builtin_unreachable","v",false}, {"__builtin_abort","v",false},
    {"__atomic_thread_fence","vi",false}, {"__atomic_signal_fence","vi",false},
    {"__builtin_strlen","zs",false}, {"__builtin_flt_rounds","i",false}, {"__builtin_strcmp","iss",false}, {"__builtin_memcmp","iPPz",false},
};
static CeType *signature_type(Elab *e, char code) {
    CeSema *s = e->sema;
    switch (code) {
    case 'v': return basic(e,CE_T_VOID);
    case 'b': return basic(e,CE_T_BOOL);
    case 'i': return basic(e,CE_T_INT);
    case 'u': return basic(e,CE_T_UINT);
    case 'l': return basic(e,CE_T_LONG);
    case 'L': return basic(e,CE_T_ULONG);
    case 'x': return basic(e,CE_T_LLONG);
    case 'X': return basic(e,CE_T_ULLONG);
    case 'h': return basic(e,CE_T_USHORT);
    case 'z': return basic(e,s->abi->size_type);
    case 'p': return ce_type_pointer(s,basic(e,CE_T_VOID));
    case 'P': return ce_type_pointer(s,ce_type_qualified(s,basic(e,CE_T_VOID),CE_Q_CONST));
    case 'c': return ce_type_pointer(s,basic(e,CE_T_CHAR));
    case 's': return ce_type_pointer(s,ce_type_qualified(s,basic(e,CE_T_CHAR),CE_Q_CONST));
    case 'f': return basic(e,CE_T_FLOAT);
    case 'd': return basic(e,CE_T_DOUBLE);
    case 'V': return ce_type_pointer(s,s->va_list_type->base);
    case '*': return ce_type_pointer(s,basic(e,CE_T_LLONG));
    default: return basic(e,CE_T_INT);
    }
}
static CeType *builtin_function(Elab *e, CeText name) {
    for (size_t i = 0; i < sizeof(builtins)/sizeof(*builtins); ++i) if (ce_is(name,builtins[i].name)) {
        const char *sig = builtins[i].signature;
        CeType *f = ce_type_new(e->sema,CE_T_FUNCTION);
        f->base = signature_type(e,sig[0]);
        size_t n = strlen(sig)-1;
        /* "x*" names a pointer to the previous code's type */
        f->parameters = ce_alloc(&e->sema->arena,(n ? n : 1)*sizeof(*f->parameters));
        for (size_t k = 1; sig[k]; ++k) {
            if (sig[k] == '*') { f->parameters[f->nparameters-1] = ce_type_pointer(e->sema,f->parameters[f->nparameters-1]); continue; }
            f->parameters[f->nparameters++] = signature_type(e,sig[k]);
        }
        f->prototype = true; f->variadic = builtins[i].variadic;
        return f;
    }
    return NULL;
}

/* ---- expressions (§6.5) ---------------------------------------------------- */


static CeExpr *operand(Elab *e, const CeTree *t, bool *ok) {
    CeExpr *x; *ok = *ok && expression(e,t,&x);
    return *ok ? x : NULL;
}

static unsigned long long fold_binary(Elab *e, unsigned k, CeType *type, const CeExpr *a, const CeExpr *b, bool *valid) {
    bool is_signed = signed_type(e,type);
    unsigned long long x = truncate_value(e,type,a->value), y = truncate_value(e,type,b->value);
    long long sx = (long long)x, sy = (long long)y;
    unsigned long long r = 0;
    *valid = true;
    switch (k) {
    case CE_K_Multiply: r = x*y; break;
    case CE_K_Divide: if (!y) { *valid = false; return 0; } r = is_signed ? (unsigned long long)(sx/sy) : x/y; break;
    case CE_K_Remainder: if (!y) { *valid = false; return 0; } r = is_signed ? (unsigned long long)(sx%sy) : x%y; break;
    case CE_K_Add: r = x+y; break;
    case CE_K_Subtract: r = x-y; break;
    case CE_K_BitAnd: r = x&y; break;
    case CE_K_BitXor: r = x^y; break;
    case CE_K_BitOr: r = x|y; break;
    default: *valid = false; return 0;
    }
    return truncate_value(e,type,r);
}
static bool compare_values(Elab *e, unsigned k, CeType *type, const CeExpr *a, const CeExpr *b) {
    bool is_signed = signed_type(e,type);
    unsigned long long x = truncate_value(e,type,a->value), y = truncate_value(e,type,b->value);
    long long sx = (long long)x, sy = (long long)y;
    switch (k) {
    case CE_K_Less: return is_signed ? sx < sy : x < y;
    case CE_K_Greater: return is_signed ? sx > sy : x > y;
    case CE_K_LessEqual: return is_signed ? sx <= sy : x <= y;
    case CE_K_GreaterEqual: return is_signed ? sx >= sy : x >= y;
    case CE_K_Equal: return x == y;
    default: return x != y;
    }
}

static bool member_lookup(Elab *e, const CeType *record, CeText name, CeMember *out, unsigned long long base) {
    if (!record->tag || !record->tag->record) return false;
    CeRecord *r = record->tag->record;
    for (size_t i = 0; i < r->nmembers; ++i) {
        CeMember *m = &r->members[i];
        if (m->anonymous) {
            if (member_lookup(e,m->type,name,out,base+m->offset)) return true;
        } else if (m->name.len && ce_equal(m->name,name)) {
            *out = *m; out->offset += base; return true;
        }
    }
    return false;
}

static bool statement(Elab *e, const CeTree *t);
static bool initializer(Elab *e, CeType *type, const CeTree *init, CeType **completed);
static bool block(Elab *e, const CeTree *t, CeExpr **last);

/* §6.5.15 the conditional operator's result type. */
static CeType *conditional_type(Elab *e, const CeTree *at, CeExpr *a, CeExpr *b) {
    CeType *ta = rvalue(e,a), *tb = rvalue(e,b);
    if (ce_type_arithmetic(ta) && ce_type_arithmetic(tb)) {
        CeType *c = ce_type_common(e->sema,ta,tb,a->bits,b->bits);
        CeExpr *operands[] = {a,b};
        for (size_t i = 0; i < 2; ++i) {
            CeExpr *x = operands[i];
            CeType *from = current(x);
            unsigned count = x->nconversions;
            convert(e,x,c);
            /* A narrow unsigned bit-field is promoted even when the other
               operand restores its declared unsigned type. Clang represents
               this as one IntegralCast to the common result type. */
            if (e->sema->clang && x->bits && ce_type_integer(from) &&
                ce_type_integer(c) && x->nconversions == count &&
                ce_promoted_kind(e->sema->abi,ce_type_integer_kind(from),x->bits) != ce_type_integer_kind(from))
                add_conversion(x,CE_CONVERT_IntegralCast,c);
        }
        return c;
    }
    if (ce_type_void(ta) || ce_type_void(tb)) return basic(e,CE_T_VOID);
    if (ce_type_record(ta) && ce_type_record(tb)) return ce_type_unqualified(e->sema,ta);
    if (ta->kind == CE_T_POINTER && is_null_pointer_constant(b) && tb->kind != CE_T_POINTER) { convert(e,b,ta); return ta; }
    if (tb->kind == CE_T_POINTER && is_null_pointer_constant(a) && ta->kind != CE_T_POINTER) { convert(e,a,tb); return tb; }
    if (ta->kind == CE_T_POINTER && tb->kind == CE_T_POINTER) {
        unsigned q = ta->base->qualifiers | tb->base->qualifiers;
        CeType *result;
        if (is_null_pointer_constant(b) && b->type->kind == CE_T_POINTER && !is_null_pointer_constant(a)) result = ta;
        else if (is_null_pointer_constant(a) && a->type->kind == CE_T_POINTER && !is_null_pointer_constant(b)) result = tb;
        else if (ce_type_void(ta->base) || ce_type_void(tb->base))
            result = ce_type_pointer(e->sema,ce_type_qualified(e->sema,basic(e,CE_T_VOID),q));
        else {
            CeType *ua = ce_type_unqualified(e->sema,ta->base), *ub = ce_type_unqualified(e->sema,tb->base);
            CeType *composite = ce_type_compatible(e->sema,ua,ub) ? ce_type_composite(e->sema,ua,ub) : ua;
            result = ce_type_pointer(e->sema,ce_type_qualified(e->sema,composite,q));
        }
        /* Every pointer conversion here keeps the representation. Clang names
           a null pointer constant's NullToPointer; with a void pointer, the
           void side's qualification NoOp and the other side's BitCast; and
           BitCast for both sides of compatible pointees. */
        CeExpr *operands[] = {a,b}; CeType *types[] = {ta,tb};
        bool void_case = !is_null_pointer_constant(a) && !is_null_pointer_constant(b) &&
            (ce_type_void(ta->base) || ce_type_void(tb->base));
        for (size_t i = 0; i < 2; ++i) {
            if (types[i] == result || (ce_type_compatible(e->sema,types[i],result) &&
                types[i]->base->qualifiers == result->base->qualifiers)) continue;
            unsigned kind = is_null_pointer_constant(operands[i]) ? CE_CONVERT_NullToPointer :
                void_case && ce_type_void(types[i]->base) ? CE_CONVERT_NoOp : CE_CONVERT_BitCast;
            add_conversion(operands[i],kind,result);
        }
        return result;
    }
    fail(e,at,"incompatible operand types in a conditional expression");
    return NULL;
}

static bool call(Elab *e, const CeTree *t, CeExpr **out);
/* §6.3.2.1p1: a modifiable lvalue has no array, incomplete or const type,
   and no const member at any depth. */
static bool const_member(const CeType *t, size_t depth) {
    if (depth > 64 || !ce_type_record(t) || !t->tag || !t->tag->record) return false;
    for (size_t i = 0; i < t->tag->record->nmembers; ++i) {
        const CeType *m = t->tag->record->members[i].type;
        while (m->kind == CE_T_ARRAY) m = m->base;
        if ((m->qualifiers & CE_Q_CONST) || const_member(m,depth+1)) return true;
    }
    return false;
}
static bool modifiable(Elab *e, const CeTree *t, const CeExpr *x) {
    if (x->category != CE_LVALUE) return fail(e,t,"the operand is not an lvalue");
    const CeType *type = x->type;
    if (type->kind == CE_T_ARRAY) return fail(e,t,"an array is not a modifiable lvalue");
    if (type->qualifiers & CE_Q_CONST) return fail(e,t,"the operand has const-qualified type");
    if (!ce_type_complete(type)) return fail(e,t,"the operand has incomplete type");
    if (const_member(type,0)) return fail(e,t,"the structure has a const member");
    return true;
}
/* "void f(T *, int)": a function's signature with its declared type names. */
/* Clang writes the definition's own parameter types, not an earlier declaration's. */
static char *pretty_function(Elab *e, const CeEntity *f, const CeType *t) {
    char *ret = ce_type_text(e->sema,t->base,CE_PRINT_SUGAR|CE_PRINT_ANYWAY|CE_PRINT_DECLARED);
    size_t cap = strlen(ret)+f->name.len+64, len = 0;
    char *out = malloc(cap);
    if (!out) { free(ret); return NULL; }
    /* the name sits in the declarator: "zipper *f(", "int f(" */
    size_t r = strlen(ret);
    len += (size_t)snprintf(out,cap,"%s%s%.*s(",ret,r && ret[r-1] == '*' ? "" : " ",(int)f->name.len,f->name.data);
    free(ret);
    for (size_t i = 0; i < t->nparameters; ++i) {
        char *p = ce_type_text(e->sema,t->parameters[i],CE_PRINT_SUGAR|CE_PRINT_ANYWAY|CE_PRINT_DECLARED);
        size_t need = len+strlen(p)+8;
        if (need > cap) { cap = need*2; out = ce_resize(out,cap,1); }
        len += (size_t)snprintf(out+len,cap-len,"%s%s",i ? ", " : "",p);
        free(p);
    }
    if (len+16 > cap) { cap = len+32; out = ce_resize(out,cap,1); }
    if (t->variadic) len += (size_t)snprintf(out+len,cap-len,"%s...",t->nparameters ? ", " : "");
    else if (t->prototype && !t->nparameters) len += (size_t)snprintf(out+len,cap-len,"void");
    snprintf(out+len,cap-len,")");
    return out;
}
/* Pointer types that are the same type, qualifiers included. */
static bool same_pointer(Elab *e, const CeType *a, const CeType *b) {
    return a->base->qualifiers == b->base->qualifiers && ce_type_compatible(e->sema,a,b);
}
static bool offsetof_designator(Elab *e, CeType *record, const CeTree *d, CeType **type,
                                unsigned long long *offset, bool *constant);

static bool expression(Elab *e, const CeTree *t, CeExpr **out) {
    CeExpr *known = ce_pointer_get(&e->sema->exprs,t);
    if (known) { *out = known; return true; }
    bool ok = true;
    unsigned k = kind_of(e,t);
    CeSema *s = e->sema;
    switch (k) {
    case CE_K_Var: {
        const CeUse *use = ce_sema_use(s,t->children[0]);
        if (!use) return fail(e,t,"identifier has no binding");
        CeEntity *entity = use->entity;
        if (entity->kind == CE_ENTITY_ENUMERATOR) {
            CeExpr *x = new_expr(e,t,entity->type ? entity->type : basic(e,CE_T_INT),CE_RVALUE);
            x->constant = entity->value_known; x->value = (unsigned long long)entity->value;
            *out = x; return true;
        }
        if (entity->kind == CE_ENTITY_BUILTIN) {
            CeText n = entity->name;
            if (ce_is(n,"__func__") || ce_is(n,"__FUNCTION__") || ce_is(n,"__PRETTY_FUNCTION__")) {
                size_t len = e->function ? e->function->name.len+1 : 1;
                /* Clang's C __PRETTY_FUNCTION__ is the signature, GCC's the name */
                if (s->clang && ce_is(n,"__PRETTY_FUNCTION__") && e->function && e->function_type) {
                    char *signature = pretty_function(e,e->function,e->function_type);
                    if (signature) { len = strlen(signature)+1; free(signature); }
                }
                CeType *c = ce_type_qualified(s,basic(e,CE_T_CHAR),CE_Q_CONST);
                *out = new_expr(e,t,ce_type_array(s,c,(long long)len),CE_LVALUE); return true;
            }
            CeType *f = builtin_function(e,n);
            CeExpr *x = new_expr(e,t,f ? f : ce_type_new(s,CE_T_FUNCTION),CE_DESIGNATOR);
            if (!f) { x->type->base = basic(e,CE_T_INT); x->type->prototype = false; x->generic = true; }
            x->builtin = true;
            *out = x; return true;
        }
        if (!entity->type) return fail(e,t,"%.*s has no type here",(int)entity->name.len,entity->name.data);
        bool function = entity->type->kind == CE_T_FUNCTION;
        *out = new_expr(e,t,entity->type,function ? CE_DESIGNATOR : CE_LVALUE);
        return true;
    }
    case CE_K_IntConst: return integer_constant(e,t,out);
    case CE_K_FloatConst: return floating_constant(e,t,out);
    case CE_K_CharConst: return character_constant(e,t,out);
    case CE_K_StringLiterals: return string_literal(e,t,out);
    case CE_K_Generic: {
        CeExpr *control = operand(e,t->children[0],&ok);
        if (!ok) return false;
        /* the controlling expression undergoes lvalue conversion (C17 DR 481) */
        CeType *ct = ce_type_unqualified(s,rvalue(e,control));
        const CeTree *list = t->children[1], *chosen = NULL, *fallback = NULL;
        CeType **associations = ce_alloc(&s->arena,list->len*sizeof(*associations)); size_t nassociations = 0;
        for (size_t i = 0; i < list->len; ++i) {
            const CeTree *a = list->children[i];
            if (kind_of(e,a) == CE_K_GenericDefault) {
                if (fallback) return fail(e,a,"duplicate default generic association");
                fallback = a->children[0]; continue;
            }
            CeType *at; if (!type_name(e,a->children[0],&at)) return false;
            if (!ce_type_complete(at)) return fail(e,a,"a generic association needs a complete object type");
            for (size_t j = 0; j < nassociations; ++j)
                if (ce_type_compatible(s,at,associations[j])) return fail(e,a,"compatible types in two generic associations");
            associations[nassociations++] = at;
            if (!chosen && ce_type_compatible(s,at,ct)) chosen = a->children[1];
        }
        if (!chosen) chosen = fallback;
        if (!chosen) return fail(e,t,"no generic association matches the controlling type");
        for (size_t i = 0; i < list->len; ++i) {
            const CeTree *a = list->children[i];
            const CeTree *x = kind_of(e,a) == CE_K_GenericDefault ? a->children[0] : a->children[1];
            CeExpr *ignored; if (!expression(e,x,&ignored)) return false;
        }
        CeExpr *r = ce_pointer_get(&s->exprs,chosen);
        CeExpr *x = new_expr(e,t,r->type,r->category);
        x->constant = r->constant; x->value = r->value; x->bits = r->bits; x->null_pointer = r->null_pointer;
        x->selected = chosen;
        *out = x; return true;
    }
    case CE_K_Subscript: {
        CeExpr *a = operand(e,t->children[0],&ok), *b = operand(e,t->children[1],&ok);
        if (!ok) return false;
        CeType *ta = rvalue(e,a), *tb = rvalue(e,b);
        CeType *pointer = ta->kind == CE_T_POINTER ? ta : tb->kind == CE_T_POINTER ? tb : NULL;
        if (!pointer) return fail(e,t,"subscripted value is neither array nor pointer");
        *out = new_expr(e,t,pointer->base,pointer->base->kind == CE_T_FUNCTION ? CE_DESIGNATOR : CE_LVALUE);
        return true;
    }
    case CE_K_Call: return call(e,t,out);
    case CE_K_Member: case CE_K_PointerMember: {
        CeExpr *base = operand(e,t->children[0],&ok);
        if (!ok) return false;
        CeType *record; bool lvalue;
        if (k == CE_K_PointerMember) {
            CeType *p = rvalue(e,base);
            if (p->kind != CE_T_POINTER || !ce_type_record(p->base)) return fail(e,t,"-> applies to a pointer to a structure or union");
            record = p->base; lvalue = true;
        } else {
            record = base->type;
            if (!ce_type_record(record)) return fail(e,t,". applies to a structure or union");
            lvalue = base->category == CE_LVALUE;
        }
        CeText name = ce_sema_name(s,t->children[1]);
        CeMember m;
        if (!member_lookup(e,record,name,&m,0))
            return fail(e,t,"no member named %.*s",(int)name.len,name.data);
        unsigned q = record->qualifiers;
        CeType *type = ce_type_qualified(s,m.type,q);
        CeExpr *x = new_expr(e,t,type,lvalue ? CE_LVALUE : CE_RVALUE);
        if (m.bitfield) x->bits = m.bit_width;
        x->member = m.declaration;
        *out = x; return true;
    }
    case CE_K_PostIncrement: case CE_K_PostDecrement: case CE_K_PreIncrement: case CE_K_PreDecrement: {
        CeExpr *a = operand(e,t->children[0],&ok);
        if (!ok) return false;
        if (!modifiable(e,t,a)) return false;
        if (!ce_type_scalar(a->type)) return fail(e,t,"increment and decrement need a scalar operand");
        *out = new_expr(e,t,ce_type_unqualified(s,a->type),CE_RVALUE);
        /* GCC's incremented bit-field value keeps the field's width; Clang's
           has the declared type */
        (*out)->bits = s->clang ? 0 : a->bits;
        return true;
    }
    case CE_K_CompoundLiteral: case CE_K_CompoundLiteralComma: case CE_K_GNUEmptyCompoundLiteral: {
        CeType *type; if (!type_name(e,t->children[0],&type)) return false;
        CeExpr *x = new_expr(e,t,type,CE_LVALUE);
        CeType *completed = type;
        if (k != CE_K_GNUEmptyCompoundLiteral) {
            /* the brace list itself is the initializer */
            if (!initializer(e,type,t,&completed)) return false;
        }
        x->type = completed;
        *out = x; return true;
    }
    case CE_K_AddressOf: {
        CeExpr *a = operand(e,t->children[0],&ok);
        if (!ok) return false;
        if (a->category == CE_RVALUE && a->type->kind != CE_T_FUNCTION)
            return fail(e,t,"& needs an lvalue or function designator");
        if (a->bits) return fail(e,t,"cannot take the address of a bit-field");
        CeExpr *x = new_expr(e,t,ce_type_pointer(s,a->type),CE_RVALUE);
        *out = x; return true;
    }
    case CE_K_Indirection: {
        CeExpr *a = operand(e,t->children[0],&ok);
        if (!ok) return false;
        CeType *p = rvalue(e,a);
        if (p->kind != CE_T_POINTER) return fail(e,t,"* applies to a pointer");
        *out = new_expr(e,t,p->base,p->base->kind == CE_T_FUNCTION ? CE_DESIGNATOR : CE_LVALUE);
        return true;
    }
    case CE_K_UnaryPlus: case CE_K_Negate: case CE_K_BitNot: {
        CeExpr *a = operand(e,t->children[0],&ok);
        if (!ok) return false;
        CeType *ta = rvalue(e,a);
        if (!ce_type_arithmetic(ta)) return fail(e,t,"unary arithmetic needs an arithmetic operand");
        CeType *p = ce_type_promoted(s,ta,a->bits);
        convert(e,a,p);
        CeExpr *x = new_expr(e,t,p,CE_RVALUE);
        if (a->constant && ce_type_integer(p)) {
            x->constant = true;
            unsigned long long v = truncate_value(e,p,a->value);
            x->value = truncate_value(e,p,k == CE_K_Negate ? (unsigned long long)0-v : k == CE_K_BitNot ? ~v : v);
        }
        *out = x; return true;
    }
    case CE_K_Not: {
        CeExpr *a = operand(e,t->children[0],&ok);
        if (!ok) return false;
        if (!ce_type_scalar(rvalue(e,a))) return fail(e,t,"logical negation needs a scalar operand");
        CeExpr *x = new_expr(e,t,basic(e,CE_T_INT),CE_RVALUE);
        if (a->constant) { x->constant = true; x->value = a->value == 0; }
        *out = x; return true;
    }
    case CE_K_SizeofExpression: case CE_K_SizeofType: case CE_K_Alignof:
    case CE_K_GNUAlignofType: case CE_K_GNUAlignofExpression: {
        CeType *type;
        if (k == CE_K_SizeofType || k == CE_K_Alignof || k == CE_K_GNUAlignofType) {
            if (!type_name(e,t->children[k == CE_K_GNUAlignofType ? 1 : 0],&type)) return false;
        } else {
            CeExpr *a = operand(e,t->children[k == CE_K_GNUAlignofExpression ? 1 : 0],&ok);
            if (!ok) return false;
            type = a->type;
        }
        CeExpr *x = new_expr(e,t,basic(e,s->abi->size_type),CE_RVALUE);
        bool variable = type->kind == CE_T_ARRAY && type->length == -2;
        if (k == CE_K_SizeofExpression || k == CE_K_SizeofType) {
            if (!variable && ce_type_complete(type)) { x->constant = true; x->value = ce_type_size(s,type); }
            else if (type->kind == CE_T_VOID || type->kind == CE_T_FUNCTION) { x->constant = true; x->value = 1; }
            else if (!variable) return fail(e,t,"sizeof applied to an incomplete type");
        } else { x->constant = true; x->value = ce_type_align(s,type); }
        *out = x; return true;
    }
    case CE_K_Cast: {
        CeType *type; if (!type_name(e,t->children[0],&type)) return false;
        CeExpr *a = operand(e,t->children[1],&ok);
        if (!ok) return false;
        CeType *target = ce_type_unqualified(s,type);
        CeExpr *x = new_expr(e,t,target,CE_RVALUE);
        CeType *from = rvalue(e,a);
        if (target->kind == CE_T_VOID) { x->cast = CE_CONVERT_ToVoid; *out = x; return true; }
        /* an explicit cast from a pointer is a bit cast even for (void *)0,
           and a no-op when the types differ only in qualifiers at any depth */
        x->cast = conversion_kind(e,ce_type_unqualified(s,from),target,is_null_pointer_constant(a) && ce_type_integer(from));
        if (!x->cast || (x->cast == CE_CONVERT_BitCast && ce_type_similar(ce_type_unqualified(s,from),target)))
            x->cast = CE_CONVERT_NoOp;
        if (a->constant && ce_type_integer(target) && ce_type_integer(from)) {
            x->constant = true; x->value = truncate_value(e,target,truncate_value(e,from,a->value));
        }
        if (target->kind == CE_T_POINTER && ce_type_void(target->base) && !target->base->qualifiers &&
            is_null_pointer_constant(a)) x->null_pointer = true;
        *out = x; return true;
    }
    case CE_K_Multiply: case CE_K_Divide: case CE_K_Remainder: case CE_K_Add: case CE_K_Subtract:
    case CE_K_BitAnd: case CE_K_BitXor: case CE_K_BitOr: {
        CeExpr *a = operand(e,t->children[0],&ok), *b = operand(e,t->children[1],&ok);
        if (!ok) return false;
        CeType *ta = rvalue(e,a), *tb = rvalue(e,b);
        if ((k == CE_K_Add || k == CE_K_Subtract) && (ta->kind == CE_T_POINTER || tb->kind == CE_T_POINTER)) {
            if (k == CE_K_Subtract && ta->kind == CE_T_POINTER && tb->kind == CE_T_POINTER) {
                *out = new_expr(e,t,basic(e,s->abi->ptrdiff_type),CE_RVALUE); return true;
            }
            /* Clang promotes the integer operand */
            CeExpr *integer = ta->kind == CE_T_POINTER ? b : a;
            CeType *ti = current(integer);
            if (ce_type_integer(ti)) convert(e,integer,ce_type_promoted(s,ti,integer->bits));
            *out = new_expr(e,t,ta->kind == CE_T_POINTER ? ta : tb,CE_RVALUE); return true;
        }
        if (!ce_type_arithmetic(ta) || !ce_type_arithmetic(tb)) return fail(e,t,"arithmetic needs arithmetic operands");
        CeType *c = ce_type_common(s,ta,tb,a->bits,b->bits);
        convert(e,a,c); convert(e,b,c);
        CeExpr *x = new_expr(e,t,c,CE_RVALUE);
        if (a->constant && b->constant && ce_type_integer(c)) {
            bool valid; x->value = fold_binary(e,k,c,a,b,&valid); x->constant = valid;
        }
        *out = x; return true;
    }
    case CE_K_ShiftLeft: case CE_K_ShiftRight: {
        CeExpr *a = operand(e,t->children[0],&ok), *b = operand(e,t->children[1],&ok);
        if (!ok) return false;
        if (!ce_type_integer(rvalue(e,a)) || !ce_type_integer(rvalue(e,b)))
            return fail(e,t,"shift operands need integer types");
        CeType *pa = ce_type_promoted(s,rvalue(e,a),a->bits), *pb = ce_type_promoted(s,rvalue(e,b),b->bits);
        convert(e,a,pa); convert(e,b,pb);
        CeExpr *x = new_expr(e,t,pa,CE_RVALUE);
        if (a->constant && b->constant) {
            unsigned long long v = truncate_value(e,pa,a->value), n = b->value;
            if (n < 64) {
                x->constant = true;
                x->value = truncate_value(e,pa,k == CE_K_ShiftLeft ? v << n :
                    signed_type(e,pa) ? (unsigned long long)((long long)v >> n) : v >> n);
            }
        }
        *out = x; return true;
    }
    case CE_K_Less: case CE_K_Greater: case CE_K_LessEqual: case CE_K_GreaterEqual:
    case CE_K_Equal: case CE_K_NotEqual: {
        CeExpr *a = operand(e,t->children[0],&ok), *b = operand(e,t->children[1],&ok);
        if (!ok) return false;
        CeType *ta = rvalue(e,a), *tb = rvalue(e,b);
        CeExpr *x = new_expr(e,t,basic(e,CE_T_INT),CE_RVALUE);
        if (ce_type_arithmetic(ta) && ce_type_arithmetic(tb)) {
            CeType *c = ce_type_common(s,ta,tb,a->bits,b->bits);
            convert(e,a,c); convert(e,b,c);
            if (a->constant && b->constant && ce_type_integer(c)) { x->constant = true; x->value = compare_values(e,k,c,a,b); }
        } else if (ta->kind == CE_T_POINTER && tb->kind == CE_T_POINTER) {
            /* §6.5.9p5: a null pointer constant takes the other operand's type;
               otherwise Clang brings the right operand to the left's type, a
               representation-preserving bit cast whenever the types differ */
            if (!same_pointer(e,ta,tb)) add_conversion(b,CE_CONVERT_BitCast,ta);
        } else if (ta->kind == CE_T_POINTER) convert(e,b,ta);
        else if (tb->kind == CE_T_POINTER) convert(e,a,tb);
        *out = x; return true;
    }
    case CE_K_And: case CE_K_Or: {
        CeExpr *a = operand(e,t->children[0],&ok), *b = operand(e,t->children[1],&ok);
        if (!ok) return false;
        /* Clang applies the integer promotions to arithmetic operands */
        CeExpr *both[] = {a,b};
        for (size_t i = 0; i < 2; ++i) {
            CeType *ti = rvalue(e,both[i]);
            if (ce_type_integer(ti)) convert(e,both[i],ce_type_promoted(s,ti,both[i]->bits));
        }
        CeExpr *x = new_expr(e,t,basic(e,CE_T_INT),CE_RVALUE);
        if (a->constant && (k == CE_K_And ? !a->value : a->value)) { x->constant = true; x->value = k == CE_K_Or; }
        else if (a->constant && b->constant) { x->constant = true; x->value = b->value != 0; }
        *out = x; return true;
    }
    case CE_K_Conditional: case CE_K_GNUConditional: {
        bool gnu = k == CE_K_GNUConditional;
        CeExpr *c = operand(e,t->children[0],&ok);
        CeExpr *a = gnu ? c : operand(e,t->children[1],&ok);
        CeExpr *b = operand(e,t->children[gnu ? 1 : 2],&ok);
        if (!ok) return false;
        if (!gnu) {
            /* Clang applies the integer promotions to the condition */
            CeType *tc = rvalue(e,c);
            if (ce_type_integer(tc)) convert(e,c,ce_type_promoted(s,tc,c->bits));
        }
        CeType *type = conditional_type(e,t,a,b);
        if (!type) return false;
        CeExpr *x = new_expr(e,t,type,CE_RVALUE);
        if (c->constant && a->constant && b->constant && ce_type_integer(type)) {
            x->constant = true; x->value = truncate_value(e,type,c->value ? a->value : b->value);
        }
        if (c->constant && !c->value && b->null_pointer) x->null_pointer = true;
        *out = x; return true;
    }
    case CE_K_Assign: case CE_K_MultiplyAssign: case CE_K_DivideAssign: case CE_K_RemainderAssign:
    case CE_K_AddAssign: case CE_K_SubtractAssign: case CE_K_ShiftLeftAssign: case CE_K_ShiftRightAssign:
    case CE_K_BitAndAssign: case CE_K_BitXorAssign: case CE_K_BitOrAssign: {
        CeExpr *a = operand(e,t->children[0],&ok), *b = operand(e,t->children[1],&ok);
        if (!ok) return false;
        if (!modifiable(e,t,a)) return false;
        CeType *lhs = ce_type_unqualified(s,a->type);
        CeExpr *x = new_expr(e,t,lhs,CE_RVALUE);
        /* GCC: the value stored to a bit-field keeps the bit-field's width */
        x->bits = a->bits;
        if (k == CE_K_Assign) { if (!converted(e,t,b,a->type)) return false; *out = x; return true; }
        CeType *tb = rvalue(e,b);
        if ((k == CE_K_AddAssign || k == CE_K_SubtractAssign) && lhs->kind == CE_T_POINTER) {
            if (ce_type_integer(tb)) convert(e,b,ce_type_promoted(s,tb,b->bits));
            *out = x; return true;
        }
        if (k == CE_K_ShiftLeftAssign || k == CE_K_ShiftRightAssign) {
            convert(e,b,ce_type_promoted(s,tb,b->bits)); *out = x; return true;
        }
        /* the computation type of a compound assignment */
        CeType *c = ce_type_common(s,lhs,tb,a->bits,b->bits);
        convert(e,b,c);
        x->computation = c;
        *out = x; return true;
    }
    case CE_K_Comma: {
        CeExpr *a = operand(e,t->children[0],&ok), *b = operand(e,t->children[1],&ok);
        if (!ok) return false;
        (void)a;
        CeType *tb = b->type;
        if (tb->kind == CE_T_ARRAY || tb->kind == CE_T_FUNCTION || b->category == CE_LVALUE) tb = rvalue(e,b);
        *out = new_expr(e,t,tb,CE_RVALUE);
        return true;
    }
    case CE_K_GNUStatementExpression: {
        CeExpr *last = NULL;
        if (!block(e,t->children[0],&last)) return false;
        CeType *type = basic(e,CE_T_VOID);
        if (last) type = rvalue(e,last);
        *out = new_expr(e,t,type,CE_RVALUE);
        return true;
    }
    case CE_K_GNUExpressionExtension: {
        CeExpr *a = operand(e,t->children[0],&ok);
        if (!ok) return false;
        CeExpr *x = new_expr(e,t,a->type,a->category);
        x->constant = a->constant; x->value = a->value; x->bits = a->bits; x->null_pointer = a->null_pointer;
        x->selected = t->children[0];
        *out = x; return true;
    }
    case CE_K_GNUOffsetof: {
        CeType *type; if (!type_name(e,t->children[0],&type)) return false;
        unsigned long long offset; bool constant = true;
        if (!offsetof_designator(e,type,t->children[1],&type,&offset,&constant)) return false;
        CeExpr *x = new_expr(e,t,basic(e,s->abi->size_type),CE_RVALUE);
        x->constant = constant; x->value = offset;
        *out = x; return true;
    }
    case CE_K_GNUVaArg: {
        CeExpr *a = operand(e,t->children[0],&ok);
        if (!ok) return false;
        rvalue(e,a);
        CeType *type; if (!type_name(e,t->children[1],&type)) return false;
        *out = new_expr(e,t,type,CE_RVALUE);
        return true;
    }
    case CE_K_GNUCompatibleTypes: {
        CeType *a, *b;
        if (!type_name(e,t->children[0],&a) || !type_name(e,t->children[1],&b)) return false;
        CeExpr *x = new_expr(e,t,basic(e,CE_T_INT),CE_RVALUE);
        x->constant = true;
        x->value = ce_type_compatible(s,ce_type_unqualified(s,a),ce_type_unqualified(s,b));
        *out = x; return true;
    }
    case CE_K_GNULabelAddress:
        *out = new_expr(e,t,ce_type_pointer(s,basic(e,CE_T_VOID)),CE_RVALUE); return true;
    case CE_K_GNUConvertVector: return fail(e,t,"GNU vector types are outside the elaborated C type language");
    default: return fail(e,t,"unknown expression form");
    }
}

static bool offsetof_designator(Elab *e, CeType *record, const CeTree *d, CeType **type,
                                unsigned long long *offset, bool *constant) {
    switch (kind_of(e,d)) {
    case CE_K_GNUOffsetMember: {
        CeMember m;
        CeText name = ce_sema_name(e->sema,d->children[0]);
        if (!member_lookup(e,record,name,&m,0)) return fail(e,d,"no member named %.*s",(int)name.len,name.data);
        *type = m.type; *offset = m.offset; return true;
    }
    case CE_K_GNUOffsetNestedMember: {
        CeType *inner; unsigned long long base;
        if (!offsetof_designator(e,record,d->children[0],&inner,&base,constant)) return false;
        CeMember m; CeText name = ce_sema_name(e->sema,d->children[1]);
        if (!member_lookup(e,inner,name,&m,0)) return fail(e,d,"no member named %.*s",(int)name.len,name.data);
        *type = m.type; *offset = base + m.offset; return true;
    }
    case CE_K_GNUOffsetSubscript: {
        CeType *inner; unsigned long long base;
        if (!offsetof_designator(e,record,d->children[0],&inner,&base,constant)) return false;
        CeExpr *index; if (!expression(e,d->children[1],&index)) return false;
        rvalue(e,index);
        if (inner->kind != CE_T_ARRAY) return fail(e,d,"offsetof subscript needs an array member");
        *type = inner->base;
        if (index->constant) *offset = base + index->value*ce_type_size(e->sema,inner->base);
        else *constant = false;
        return true;
    }
    default: return fail(e,d,"unknown offsetof designator");
    }
}

/* Generic builtins type their result from their arguments. */
static bool generic_builtin(Elab *e, const CeTree *t, CeText name, CeExpr **args, size_t n, CeExpr *x) {
    CeSema *s = e->sema;
    for (size_t i = 0; i < n; ++i) rvalue(e,args[i]);
    if (!strncmp((const char *)name.data,"__c11_atomic_",13)) {
        /* Clang's C11 atomic builtins operate on a pointer to an _Atomic object. */
        CeText op = {name.data+13,name.len-13};
        CeType *p = n ? current(args[0]) : NULL;
        CeType *object = p && p->kind == CE_T_POINTER ? ce_type_unqualified(s,p->base) : NULL;
        if (ce_is(op,"thread_fence") || ce_is(op,"signal_fence") || ce_is(op,"init") || ce_is(op,"store"))
            x->type = basic(e,CE_T_VOID);
        else if (ce_is(op,"is_lock_free") || !strncmp((const char *)op.data,"compare_exchange",16))
            x->type = basic(e,CE_T_BOOL);
        else if (object) x->type = object;
        else return fail(e,t,"atomic builtin needs a pointer");
        /* value operands take the object's value type; orders are int */
        bool exchange = !strncmp((const char *)op.data,"compare_exchange",16);
        for (size_t i = 1; i < n; ++i) {
            CeType *ti = current(args[i]);
            bool order = exchange ? i >= 3 : (ce_is(op,"init") ? false : i+1 == n);
            if (exchange && i == 1) continue;
            if (order) { if (ce_type_integer(ti)) convert(e,args[i],basic(e,CE_T_INT)); }
            else if (object && (ce_type_arithmetic(ti) || (object->kind == CE_T_POINTER &&
                     (ti->kind == CE_T_POINTER || is_null_pointer_constant(args[i]))))) convert(e,args[i],object);
        }
        return true;
    }
    if (!strncmp((const char *)name.data,"__atomic_",9) || !strncmp((const char *)name.data,"__sync_",7)) {
        if (!n) return fail(e,t,"atomic builtin needs a pointer");
        CeType *p = current(args[0]);
        if (p->kind != CE_T_POINTER) return fail(e,t,"atomic builtin needs a pointer");
        CeType *object = ce_type_unqualified(s,p->base);
        CeText op = {name.data + (name.data[2] == 'a' ? 9 : 7), name.len - (name.data[2] == 'a' ? 9 : 7)};
        /* value operands take the object's type; a weak flag is _Bool */
        if (name.data[2] == 'a') {
            bool value_at_1 = ce_is(op,"store_n") || ce_is(op,"exchange_n") || !strncmp((const char *)op.data,"fetch_",6) ||
                (op.len > 6 && !memcmp(op.data+op.len-6,"_fetch",6));
            if (value_at_1 && n > 1 && ce_type_arithmetic(current(args[1]))) convert(e,args[1],object);
            if (ce_is(op,"compare_exchange_n")) {
                if (n > 2 && ce_type_arithmetic(current(args[2]))) convert(e,args[2],object);
                if (n > 3) convert(e,args[3],basic(e,CE_T_BOOL));
            }
            if (ce_is(op,"compare_exchange") && n > 3) convert(e,args[3],basic(e,CE_T_BOOL));
        }
        if (ce_is(op,"load") || ce_is(op,"store") || ce_is(op,"thread_fence") || ce_is(op,"signal_fence") ||
            ce_is(op,"store_n") || ce_is(op,"exchange") || ce_is(op,"clear") || ce_is(op,"synchronize"))
            x->type = basic(e,CE_T_VOID);
        else if (ce_is(op,"compare_exchange") || ce_is(op,"compare_exchange_n") || ce_is(op,"test_and_set") ||
                 ce_is(op,"always_lock_free") || ce_is(op,"is_lock_free") || ce_is(op,"bool_compare_and_swap"))
            x->type = basic(e,CE_T_BOOL);
        else x->type = object;
        return true;
    }
    if (ce_is(name,"__builtin_constant_p") || ce_is(name,"__builtin_isnan") || ce_is(name,"__builtin_isfinite") ||
        ce_is(name,"__builtin_isinf") || ce_is(name,"__builtin_isinf_sign") || ce_is(name,"__builtin_signbit") ||
        ce_is(name,"__builtin_fpclassify") || ce_is(name,"__builtin_isnormal") || ce_is(name,"__builtin_va_arg_pack")) {
        x->type = basic(e,CE_T_INT);
        if (ce_is(name,"__builtin_constant_p") && n == 1) {
            x->constant = true; x->value = args[0]->constant;
        }
        return true;
    }
    if (ce_is(name,"__builtin_add_overflow") || ce_is(name,"__builtin_sub_overflow") ||
        ce_is(name,"__builtin_mul_overflow")) { x->type = basic(e,CE_T_BOOL); return true; }
    return fail(e,t,"unknown builtin %.*s",(int)name.len,name.data);
}

static bool call(Elab *e, const CeTree *t, CeExpr **out) {
    CeSema *s = e->sema;
    CeExpr *callee; if (!expression(e,t->children[0],&callee)) return false;
    const CeTree *arguments = t->children[1];
    const CeTree *list = kind_of(e,arguments) == CE_K_Arguments ? arguments->children[0] : NULL;
    size_t n = list ? list->len : 0;
    CeExpr **args = ce_alloc(&s->arena,(n ? n : 1)*sizeof(*args));
    for (size_t i = 0; i < n; ++i) if (!expression(e,list->children[i],&args[i])) return false;
    CeExpr *x = new_expr(e,t,basic(e,CE_T_INT),CE_RVALUE);
    if (callee->generic) {
        rvalue(e,callee);
        CeText name = ce_sema_use(s,t->children[0]->children[0])->entity->name;
        if (!generic_builtin(e,t,name,args,n,x)) return false;
        *out = x; return true;
    }
    CeType *p = rvalue(e,callee);
    if (p->kind != CE_T_POINTER || p->base->kind != CE_T_FUNCTION) return fail(e,t,"called object is not a function");
    CeType *f = p->base;
    x->type = ce_type_unqualified(s,f->base);
    CeText builtin_name = callee->builtin ? ce_sema_use(s,t->children[0]->children[0])->entity->name : (CeText){NULL,0};
    if (ce_is(builtin_name,"__builtin_frame_address") || ce_is(builtin_name,"__builtin_return_address")) {
        if (n != 1 || !args[0]->constant)
            return fail(e,t,"frame and return address builtins need one constant level");
        if (s->clang && (!ce_type_integer(args[0]->type) || (unsigned)args[0]->value > 65535))
            return fail(e,t,"address builtin level must be an integer in [0, 65535]");
    }
    if (ce_is(builtin_name,"__builtin_prefetch")) {
        if (s->clang && n > 3) return fail(e,t,"prefetch takes at most three arguments");
        for (size_t i = 1; i < n && i < 3; ++i) {
            if (!args[i]->constant || !ce_type_integer(args[i]->type))
                return fail(e,list->children[i],"prefetch options must be constant integers");
            if (s->clang && args[i]->value > (i == 1 ? 1u : 3u))
                return fail(e,list->children[i],"prefetch option is outside its permitted range");
        }
    }
    bool va_start = callee->builtin && ce_is(ce_sema_use(s,t->children[0]->children[0])->entity->name,"__builtin_va_start");
    for (size_t i = 0; i < n; ++i) {
        if (va_start && i > 0) continue;   /* names the last parameter; not a value */
        if (f->prototype && i < f->nparameters) { if (!converted(e,list->children[i],args[i],f->parameters[i])) return false; }
        else if (f->prototype && !f->variadic) return fail(e,t,"too many arguments in a call");
        else {
            CeType *a = rvalue(e,args[i]);
            if (a->kind == CE_T_VOID) return fail(e,list->children[i],"a void expression has no value");
            convert(e,args[i],ce_type_default_promoted(s,a));
        }
    }
    if (f->prototype && n < f->nparameters) return fail(e,t,"too few arguments in a call");
    *out = x; return true;
}

static bool constant_value(Elab *e, const CeTree *t, long long *value) {
    CeExpr *x; if (!expression(e,t,&x)) return false;
    if (!x->constant) return fail(e,t,"expected an integer constant expression");
    *value = signed_type(e,x->type) ? (long long)x->value : (long long)x->value;
    return true;
}

/* ---- initializers (§6.7.9) ------------------------------------------------- */

typedef struct { const CeTree **items; size_t n, i; } Items;

static bool initialize_list(Elab *e, CeType *type, const CeTree *list, CeType **completed);
static bool fill(Elab *e, CeType *type, Items *it, bool braced, long long *count);
static bool initialize_designated(Elab *e, CeType *type, const CeTree *item, size_t from, Items *it);

/* §6.7.9p14: a string literal initializing an array supplies the array's
   elements; in that position it denotes a value of the initialized type. */
static void string_initializes(Elab *e, CeExpr *x, CeType *type) {
    (void)e;
    x->no_probe = true;
    x->category = CE_RVALUE;
    if (type->length >= 0) x->type = type;
}
static bool character_array(const CeType *t) {
    if (t->kind != CE_T_ARRAY) return false;
    unsigned k = t->base->kind;
    return k == CE_T_CHAR || k == CE_T_SCHAR || k == CE_T_UCHAR || k == CE_T_INT || k == CE_T_UINT ||
        k == CE_T_USHORT || k == CE_T_SHORT || k == CE_T_LONG || k == CE_T_ULONG;
}
static bool string_element_compatible(Elab *e, const CeType *array, const CeExpr *string) {
    unsigned target = array->base->kind, source = string->type->base->kind;
    if (source == CE_T_CHAR) return target == CE_T_CHAR || target == CE_T_SCHAR || target == CE_T_UCHAR;
    return ce_type_compatible(e->sema,ce_type_unqualified(e->sema,array->base),string->type->base);
}
static const CeTree *strip_parentheses(Elab *e, const CeTree *t) {
    while (kind_of(e,t) == CE_K_GNUExpressionExtension) t = t->children[0];
    return t;
}
static const CeTree *element_initializer(Elab *e, const CeTree *item) {
    switch (kind_of(e,item)) {
    case CE_K_InitializerElement: return item->children[0];
    case CE_K_DesignatedInitializerElement: return item->children[1];
    case CE_K_GNUOldArrayInitializer: return item->children[1];
    default: return item;
    }
}
static size_t member_count(const CeType *t) {
    return t->tag && t->tag->record ? t->tag->record->nmembers : 0;
}
/* The next member that takes part in initialization (unnamed bit-fields do not). */
static size_t next_member(const CeType *t, size_t from) {
    CeRecord *r = t->tag->record;
    while (from < r->nmembers && !r->members[from].name.len && !r->members[from].anonymous) ++from;
    return from;
}

static void bitfield_initializer(Elab *e, const CeTree *init, CeType *type, unsigned bits) {
    if (!bits) return;
    for (;;) {
        unsigned k = kind_of(e,init);
        if (k == CE_K_InitializerList || k == CE_K_InitializerListComma) {
            const CeTree *items = init->children[0];
            if (items->len != 1) return;
            init = element_initializer(e,items->children[0]);
        } else {
            if (k == CE_K_InitializerExpression) init = init->children[0];
            CeExpr *x = ce_pointer_get(&e->sema->exprs,init);
            if (x) { x->store_type = type; x->store_bits = bits; }
            return;
        }
    }
}

/* Initialize one subobject of type `type` from the current item. */
static bool initialize_one(Elab *e, CeType *type, Items *it, long long *count) {
    (void)count;
    const CeTree *init = element_initializer(e,it->items[it->i]);
    unsigned k = kind_of(e,init);
    if (k == CE_K_InitializerList || k == CE_K_InitializerListComma || k == CE_K_GNUEmptyInitializer) {
        ++it->i;
        CeType *ignored; return initialize_list(e,type,init,&ignored);
    }
    const CeTree *expr_node = k == CE_K_InitializerExpression ? init->children[0] : init;
    CeExpr *x; if (!expression(e,expr_node,&x)) return false;
    if (ce_type_scalar(type)) { ++it->i; return converted(e,expr_node,x,type); }
    if (character_array(type) && kind_of(e,strip_parentheses(e,expr_node)) == CE_K_StringLiterals) {
        if (!string_element_compatible(e,type,x)) return fail(e,expr_node,"string encoding does not match the array element type");
        string_initializes(e,x,type); ++it->i; return true;
    }
    if ((ce_type_record(type) || type->kind == CE_T_COMPLEX) &&
        ce_type_compatible(e->sema,ce_type_unqualified(e->sema,x->type),ce_type_unqualified(e->sema,type))) {
        ++it->i; rvalue(e,x); return true;
    }
    /* §6.7.9p20 brace elision: the expression initializes the first scalar */
    long long ignored = 0;
    return fill(e,type,it,false,&ignored);
}

static bool fill(Elab *e, CeType *type, Items *it, bool braced, long long *count) {
    if (!ce_type_record(type) && type->kind != CE_T_ARRAY) {
        if (it->i < it->n) return initialize_one(e,type,it,count);
        return true;
    }
    bool is_union = type->kind == CE_T_UNION, is_array = type->kind == CE_T_ARRAY;
    size_t position = is_array ? 0 : next_member(type,0);
    long long high = 0;
    bool first = true;
    while (it->i < it->n) {
        const CeTree *item = it->items[it->i];
        unsigned k = kind_of(e,item);
        if (k == CE_K_DesignatedInitializerElement || k == CE_K_GNUOldArrayInitializer) {
            if (!braced) break;
            const CeTree *designator = k == CE_K_GNUOldArrayInitializer ? item : item->children[0]->children[0];
            unsigned dk = kind_of(e,designator);
            if (dk == CE_K_ArrayDesignator || dk == CE_K_GNUArrayRangeDesignator || dk == CE_K_GNUOldArrayInitializer) {
                if (!is_array) return fail(e,item,"array designator in a non-array initializer");
                long long index, low = 0; ++e->folded;
                bool ok = constant_value(e,designator->children[dk == CE_K_GNUArrayRangeDesignator ? 1 : 0],&index) &&
                    (dk != CE_K_GNUArrayRangeDesignator || constant_value(e,designator->children[0],&low));
                --e->folded;
                if (!ok) return false;
                position = (size_t)index;
            } else {
                if (is_array) return fail(e,item,"member designator in an array initializer");
                CeText name = ce_sema_name(e->sema,designator->children[0]);
                CeRecord *r = type->tag->record; size_t found = r->nmembers;
                for (size_t m = 0; m < r->nmembers && found == r->nmembers; ++m) {
                    if (r->members[m].name.len && ce_equal(r->members[m].name,name)) found = m;
                    else if (r->members[m].anonymous) {
                        CeMember inner;
                        if (member_lookup(e,r->members[m].type,name,&inner,0)) found = m;
                    }
                }
                if (found == r->nmembers) return fail(e,item,"no member named %.*s",(int)name.len,name.data);
                position = found;
                if (r->members[found].anonymous && !(r->members[found].name.len)) {
                    /* a designator into an anonymous member continues inside it */
                    CeType *sub = r->members[found].type;
                    if (!initialize_designated(e,sub,item,0,it)) return false;
                    position = next_member(type,position+1); first = false;
                    if (is_union) break;
                    continue;
                }
            }
            CeType *sub = is_array ? type->base : type->tag->record->members[position].type;
            const CeTree *list = k == CE_K_GNUOldArrayInitializer ? NULL : item->children[0];
            if (list && list->len > 1) {
                if (!initialize_designated(e,sub,item,1,it)) return false;
            } else {
                if (!initialize_one(e,sub,it,count)) return false;
                if (!is_array) {
                    CeMember *member = &type->tag->record->members[position];
                    bitfield_initializer(e,element_initializer(e,item),sub,member->bitfield ? member->bit_width : 0);
                }
            }
        } else {
            if (!first && is_union) break;
            if (is_array ? (type->length >= 0 && (long long)position >= type->length) : position >= member_count(type)) break;
            CeType *sub = is_array ? type->base : type->tag->record->members[position].type;
            if (!initialize_one(e,sub,it,count)) return false;
            if (!is_array) {
                CeMember *member = &type->tag->record->members[position];
                bitfield_initializer(e,element_initializer(e,item),sub,member->bitfield ? member->bit_width : 0);
            }
        }
        first = false;
        ++position;
        if (!is_array) position = next_member(type,position);
        if ((long long)position > high) high = (long long)position;
        if (is_union && !braced) break;
    }
    *count = high;
    return true;
}

/* Apply designators from index `from` of an element's designator list. */
static bool initialize_designated(Elab *e, CeType *type, const CeTree *item, size_t from, Items *it) {
    const CeTree *list = item->children[0];
    unsigned bits = 0;
    for (size_t d = from; d < list->len; ++d) {
        const CeTree *designator = list->children[d];
        unsigned dk = kind_of(e,designator);
        if (dk == CE_K_MemberDesignator) {
            CeMember m; CeText name = ce_sema_name(e->sema,designator->children[0]);
            if (!ce_type_record(type) || !member_lookup(e,type,name,&m,0))
                return fail(e,designator,"no member named %.*s",(int)name.len,name.data);
            type = m.type;
            bits = m.bitfield ? m.bit_width : 0;
        } else {
            if (type->kind != CE_T_ARRAY) return fail(e,designator,"array designator in a non-array initializer");
            long long index, low = 0; ++e->folded;
            bool ok = constant_value(e,designator->children[dk == CE_K_GNUArrayRangeDesignator ? 1 : 0],&index) &&
                (dk != CE_K_GNUArrayRangeDesignator || constant_value(e,designator->children[0],&low));
            --e->folded;
            if (!ok) return false;
            type = type->base;
            bits = 0;
        }
    }
    long long ignored = 0;
    if (!initialize_one(e,type,it,&ignored)) return false;
    bitfield_initializer(e,element_initializer(e,item),type,bits);
    return true;
}

static bool initialize_list(Elab *e, CeType *type, const CeTree *list, CeType **completed) {
    *completed = type;
    unsigned k = kind_of(e,list);
    if (k == CE_K_GNUEmptyInitializer || k == CE_K_GNUEmptyCompoundLiteral) return true;
    const CeTree *elements = k == CE_K_CompoundLiteral || k == CE_K_CompoundLiteralComma ? list->children[1] : list->children[0];
    Items it = {(const CeTree **)elements->children,elements->len,0};
    if (ce_type_scalar(type)) {
        /* §6.7.9p11: a scalar may be enclosed in braces */
        if (it.n) { long long ignored; if (!initialize_one(e,type,&it,&ignored)) return false; }
        return true;
    }
    long long count = 0;
    if (!fill(e,type,&it,true,&count)) return false;
    if (it.i < it.n) return fail(e,elements->children[it.i],"excess elements in initializer");
    if (type->kind == CE_T_ARRAY && type->length < 0 && type->length != -2) {
        CeType *c = ce_type_copy(e->sema,type); c->length = count; c->alias = NULL; *completed = c;
    }
    return true;
}

static bool initializer(Elab *e, CeType *type, const CeTree *init, CeType **completed) {
    *completed = type;
    unsigned k = kind_of(e,init);
    if (k == CE_K_InitializerList || k == CE_K_InitializerListComma || k == CE_K_GNUEmptyInitializer ||
        k == CE_K_CompoundLiteral || k == CE_K_CompoundLiteralComma)
        return initialize_list(e,type,init,completed);
    const CeTree *expr_node = init->children[0];
    CeExpr *x; if (!expression(e,expr_node,&x)) return false;
    if (character_array(type) && kind_of(e,strip_parentheses(e,expr_node)) == CE_K_StringLiterals) {
        if (!string_element_compatible(e,type,x)) return fail(e,expr_node,"string encoding does not match the array element type");
        if (type->length < 0) {
            CeType *c = ce_type_copy(e->sema,type); c->length = x->type->length; c->alias = NULL; *completed = c;
        }
        string_initializes(e,x,*completed);
        return true;
    }
    if (ce_type_record(type) || type->kind == CE_T_ARRAY) {
        if (type->kind == CE_T_ARRAY) return fail(e,expr_node,"an array initializer needs braces or a compatible string literal");
        rvalue(e,x);
        if (ce_type_record(type) && !ce_type_compatible(e->sema,ce_type_unqualified(e->sema,x->type),ce_type_unqualified(e->sema,type)))
            return fail(e,expr_node,"incompatible types in initialization");
        return true;
    }
    return converted(e,expr_node,x,type);
}

/* ---- declarations (§6.7) and function definitions (§6.9.1) ---------------- */

static bool declaration(Elab *e, const CeTree *d) {
    switch (kind_of(e,d)) {
    case CE_K_GNUDeclarationExtension: return declaration(e,d->children[0]);
    case CE_K_StaticAssertionDeclaration: return static_assertion(e,d->children[0]);
    case CE_K_Declaration: break;
    default: return fail(e,d,"unknown declaration form");
    }
    Spec s; if (!specifiers(e,d->children[0],&s)) return false;
    const CeTree *declarators = d->children[1];
    if (kind_of(e,declarators) == CE_K_NoInitDeclarators) return true;
    const CeTree *list = declarators->children[0];
    for (size_t i = 0; i < list->len; ++i) {
        const CeTree *item = list->children[i];
        Attributes a = s.attributes;
        bool had_mode = a.has_mode;
        CeType *type; const CeTree *name = NULL;
        CeType *base = s.type;
        if (!base) {
            /* GNU __auto_type: the type of the initializer after conversion */
            if (kind_of(e,item) != CE_K_InitDeclaratorValue)
                return fail(e,item,"__auto_type needs an initializer");
            CeExpr *x; if (!expression(e,item->children[1]->children[0],&x)) return false;
            base = rvalue(e,x);
        }
        if (!derive(e,base,item->children[0],&type,&name,&a,NULL)) return false;
        if (a.has_mode && !had_mode && !(type = apply_mode(e,type,&a,item))) return false;
        CeEntity *entity = name ? ce_pointer_get(&e->sema->entity_of,name) : NULL;
        if (!entity) return fail(e,item,"declarator has no binding");
        if (s.is_typedef) {
            CeType *t = ce_type_copy(e->sema,type);
            if (a.aligned) t->alignment = a.aligned;
            t->alias = NULL;
            /* §6.7p3: a typedef name may be redefined only as the same type */
            if (entity->type && !ce_type_same(entity->type,t))
                return fail(e,item,"typedef %.*s redefined as a different type",(int)entity->name.len,entity->name.data);
            entity->type = t;
            if ((ce_type_record(t) || t->kind == CE_T_ENUM) && t->tag && !t->tag->name.len && !t->tag->naming_alias &&
                !t->qualifiers) t->tag->naming_alias = entity;
            continue;
        }
        if (entity->kind == CE_ENTITY_PARAMETER) type = adjust_parameter(e,type);
        if (type->kind == CE_T_FUNCTION) entity->kind = CE_ENTITY_FUNCTION;
        if (entity->kind == CE_ENTITY_OBJECT && ce_type_void(type))
            return fail(e,item,"an object cannot have void type");
        if (entity->kind == CE_ENTITY_OBJECT && !s.external) {
            entity->needs_complete = true;
        }
        /* §6.7p4, §6.2.7p2: every declaration of one entity has compatible type */
        if (entity->type && entity->kind != CE_ENTITY_PARAMETER && !ce_type_compatible(e->sema,entity->type,type))
            return fail(e,item,"conflicting types for %.*s",(int)entity->name.len,entity->name.data);
        entity->type = entity->type && ce_type_compatible(e->sema,entity->type,type) ?
            ce_type_composite(e->sema,entity->type,type) : type;
        if (a.aligned || s.alignas_) entity->alignment = a.aligned > s.alignas_ ? a.aligned : s.alignas_;
        if (entity->kind == CE_ENTITY_OBJECT) witness_object(e,entity,entity->type);
        if (kind_of(e,item) == CE_K_InitDeclaratorValue) {
            if (!s.type) {
                CeExpr *x = ce_pointer_get(&e->sema->exprs,item->children[1]->children[0]);
                (void)x; continue;
            }
            CeType *completed;
            if (!initializer(e,entity->type,item->children[1],&completed)) return false;
            entity->type = completed;
        }
        if (entity->kind == CE_ENTITY_OBJECT && !s.external && entity->scope != CE_SCOPE_FILE && !complete_object(entity->type))
            return fail(e,item,"an object definition needs a complete type");
    }
    return true;
}

static bool function_definition(Elab *e, const CeTree *t) {
    if (kind_of(e,t) == CE_K_GNUFunctionExtension) return function_definition(e,t->children[0]);
    Spec s; if (!specifiers(e,t->children[0],&s)) return false;
    if (!s.type) return fail(e,t,"__auto_type cannot declare a function");
    CeType *type; const CeTree *name = NULL; Attributes a = s.attributes; const CeTree *function_node = NULL;
    if (!derive(e,s.type,t->children[1],&type,&name,&a,&function_node)) return false;
    CeEntity *entity = name ? ce_pointer_get(&e->sema->entity_of,name) : NULL;
    if (!entity || type->kind != CE_T_FUNCTION) return fail(e,t,"function definition has no function declarator");
    if (entity->defined) return fail(e,t,"duplicate function definition");
    if (!ce_type_void(type->base) && !complete_object(type->base))
        return fail(e,t,"a function definition needs a complete return type");
    /* §6.9.1p6: identifier-list parameters take types from the declarations */
    const CeTree *old = t->children[2];
    for (size_t i = 0; i < old->len; ++i) if (!declaration(e,old->children[i])) return false;
    if (kind_of(e,function_node) == CE_K_OldFunctionDeclarator) {
        const CeTree *ids = function_node->children[1];
        if (kind_of(e,ids) == CE_K_Identifiers) {
            const CeTree *names = ids->children[0];
            type->parameters = ce_alloc(&e->sema->arena,(names->len ? names->len : 1)*sizeof(*type->parameters));
            for (size_t i = 0; i < names->len; ++i) {
                CeEntity *p = ce_pointer_get(&e->sema->entity_of,names->children[i]);
                if (!p) return fail(e,names->children[i],"parameter has no binding");
                if (!p->type) p->type = basic(e,CE_T_INT);
                type->parameters[type->nparameters++] = p->type;
            }
        }
    }
    for (size_t i = 0; i < type->nparameters; ++i)
        if (!complete_object(type->parameters[i])) return fail(e,t,"a parameter definition needs a complete type");
    if (entity->type && !ce_type_compatible(e->sema,entity->type,type))
        return fail(e,t,"conflicting types for %.*s",(int)entity->name.len,entity->name.data);
    entity->type = entity->type ? ce_type_composite(e->sema,entity->type,type) : type;
    entity->definition = t; entity->defined = true;
    CeType *saved_return = e->return_type, *saved_type = e->function_type; CeEntity *saved_function = e->function;
    e->return_type = type->base; e->function = entity; e->function_type = type;
    bool ok = statement(e,t->children[3]);
    e->return_type = saved_return; e->function = saved_function; e->function_type = saved_type;
    return ok;
}

/* ---- statements (§6.8) ----------------------------------------------------- */

static bool full_expression(Elab *e, const CeTree *t, CeExpr **out) {
    *out = NULL;
    if (kind_of(e,t) == CE_K_NoFullExpression) return true;
    if (kind_of(e,t) == CE_K_SomeFullExpression) t = t->children[0];
    return expression(e,t,out);
}
static bool condition(Elab *e, const CeTree *t) {
    CeExpr *x; if (!full_expression(e,t,&x)) return false;
    if (x) {
        CeType *type = rvalue(e,x);
        if (!ce_type_scalar(type)) return fail(e,t,"a controlling expression needs scalar type");
    }
    return true;
}
static bool asm_operands(Elab *e, const CeTree *list, bool output) {
    for (size_t i = 0; list && i < list->len; ++i) {
        const CeTree *op = list->children[i];
        const CeTree *x = op->children[kind_of(e,op) == CE_K_GNUAsmNamedOperand ? 2 : 1];
        CeExpr *value; if (!expression(e,x,&value)) return false;
        value->no_probe = true;
        if (!output) rvalue(e,value);
    }
    return true;
}

static bool block(Elab *e, const CeTree *t, CeExpr **last) {
    const CeTree *items = t->children[0];
    if (last) *last = NULL;
    for (size_t i = 0; i < items->len; ++i) {
        const CeTree *item = items->children[i];
        switch (kind_of(e,item)) {
        case CE_K_BlockDeclaration: if (!declaration(e,item->children[0])) return false; break;
        case CE_K_BlockStatement: {
            const CeTree *st = item->children[0];
            if (!statement(e,st)) return false;
            if (last && i+1 == items->len && kind_of(e,st) == CE_K_ExpressionStatement &&
                kind_of(e,st->children[0]) == CE_K_SomeFullExpression)
                *last = ce_pointer_get(&e->sema->exprs,st->children[0]->children[0]);
            break;
        }
        case CE_K_GNUBlockPragma: break;
        default: return fail(e,item,"unknown block item");
        }
    }
    return true;
}

static bool statement(Elab *e, const CeTree *t) {
    CeExpr *x;
    switch (kind_of(e,t)) {
    case CE_K_LabeledStatement: case CE_K_SelectionStatement: case CE_K_IterationStatement:
    case CE_K_JumpStatement:
        return statement(e,t->children[0]);
    case CE_K_CompoundStatement: return block(e,t->children[0],NULL);
    case CE_K_Block: return block(e,t,NULL);
    case CE_K_ExpressionStatement: return full_expression(e,t->children[0],&x);
    case CE_K_Label: return statement(e,t->children[1]);
    case CE_K_GNUAttributedLabel: return statement(e,t->children[2]);
    case CE_K_GNULabelDeclaration: return declaration(e,t->children[1]);
    case CE_K_GNUAttributedLabelDeclaration: return declaration(e,t->children[2]);
    case CE_K_GNUAttributeStatement: { Attributes a = {0}; return attributes(e,t->children[0],&a); }
    case CE_K_Case: case CE_K_GNUCaseRange: case CE_K_GNUCaseDeclaration: case CE_K_GNUCaseRangeDeclaration: {
        if (!e->switch_labels) return fail(e,t,"case label outside a switch");
        unsigned kind = kind_of(e,t);
        size_t values = kind == CE_K_Case || kind == CE_K_GNUCaseDeclaration ? 1 : 2;
        unsigned long long endpoints[2];
        for (size_t i = 0; i < values; ++i) {
            if (!expression(e,t->children[i],&x)) return false;
            if (!x->constant || !ce_type_integer(x->type)) return fail(e,t,"case label is not an integer constant expression");
            convert(e,x,e->switch_type);
            endpoints[i] = truncate_value(e,e->switch_type,x->value);
        }
        unsigned long long low = endpoints[0], high = endpoints[values-1];
        bool signed_ = signed_type(e,e->switch_type);
        bool empty = signed_ ? (long long)high < (long long)low : high < low;
        for (CaseRange *r = e->switch_labels->ranges; !empty && r; r = r->next) {
            bool separate = signed_ ? (long long)high < (long long)r->low || (long long)r->high < (long long)low :
                high < r->low || r->high < low;
            if (!separate) return fail(e,t,"duplicate or overlapping case labels after conversion");
        }
        if (!empty) {
            CaseRange *r = ce_alloc(&e->sema->arena,sizeof(*r));
            *r = (CaseRange){low,high,e->switch_labels->ranges}; e->switch_labels->ranges = r;
        }
        return kind == CE_K_GNUCaseDeclaration || kind == CE_K_GNUCaseRangeDeclaration
            ? declaration(e,t->children[values]) : statement(e,t->children[values]);
    }
    case CE_K_Default: case CE_K_GNUDefaultDeclaration:
        if (!e->switch_labels) return fail(e,t,"default label outside a switch");
        if (e->switch_labels->has_default) return fail(e,t,"duplicate default label");
        e->switch_labels->has_default = true;
        return kind_of(e,t) == CE_K_GNUDefaultDeclaration ? declaration(e,t->children[0]) : statement(e,t->children[0]);
    case CE_K_If: return condition(e,t->children[0]) && statement(e,t->children[1]);
    case CE_K_IfElse: return condition(e,t->children[0]) && statement(e,t->children[1]) && statement(e,t->children[2]);
    case CE_K_Switch: {
        if (!full_expression(e,t->children[0],&x)) return false;
        CeType *type = rvalue(e,x);
        if (!ce_type_integer(type)) return fail(e,t,"switch needs an integer controlling expression");
        CeType *promoted = ce_type_promoted(e->sema,type,x->bits);
        convert(e,x,promoted);
        CeType *saved = e->switch_type; e->switch_type = promoted;
        SwitchLabels labels = {0}, *saved_labels = e->switch_labels; e->switch_labels = &labels;
        bool ok = statement(e,t->children[1]);
        e->switch_type = saved; e->switch_labels = saved_labels; return ok;
    }
    case CE_K_While: case CE_K_DoWhile: case CE_K_For: case CE_K_ForDeclaration: {
        unsigned kind = kind_of(e,t); bool ok;
        if (kind == CE_K_While && !condition(e,t->children[0])) return false;
        if (kind == CE_K_For || kind == CE_K_ForDeclaration) {
            if (!(kind == CE_K_For ? full_expression(e,t->children[0],&x) : declaration(e,t->children[0])) ||
                !condition(e,t->children[1]) || !full_expression(e,t->children[2],&x)) return false;
        }
        ++e->loop_depth;
        ok = statement(e,t->children[kind == CE_K_DoWhile ? 0 : kind == CE_K_While ? 1 : 3]);
        --e->loop_depth;
        return ok && (kind != CE_K_DoWhile || condition(e,t->children[1]));
    }
    case CE_K_Goto: return true;
    case CE_K_Continue: return e->loop_depth || fail(e,t,"continue outside a loop");
    case CE_K_Break: return e->loop_depth || e->switch_labels || fail(e,t,"break outside a loop or switch");
    case CE_K_GNUIndirectGoto:
        if (!expression(e,t->children[0],&x)) return false;
        rvalue(e,x); return true;
    case CE_K_Return:
        if (!full_expression(e,t->children[0],&x)) return false;
        if (x && e->return_type && !ce_type_void(e->return_type)) { if (!converted(e,t,x,e->return_type)) return false; }
        else if (x) rvalue(e,x);
        return true;
    case CE_K_GNUAsmStatement: return true;
    case CE_K_GNUAsmOutput: return asm_operands(e,t->children[3],true);
    case CE_K_GNUAsmInput: case CE_K_GNUAsmWithClobbers: case CE_K_GNUAsmLabels:
        return asm_operands(e,t->children[3],true) && asm_operands(e,t->children[4],false);
    default: return fail(e,t,"unknown statement form");
    }
}

/* ---- translation units ----------------------------------------------------- */

static void builtin_types(Elab *e) {
    CeSema *s = e->sema;
    if (s->abi != &ce_abi_x86_64_sysv) {
        s->va_list_type = ce_type_pointer(s,basic(e,CE_T_CHAR)); return;
    }
    /* x86-64 System V: typedef struct __va_list_tag __builtin_va_list[1] */
    CeEntity *tag = ce_sema_entity(s,CE_ENTITY_STRUCT,CE_SPACE_TAG,ce_cstr(&s->arena,"__va_list_tag"),NULL);
    tag->record = ce_alloc(&s->arena,sizeof(*tag->record));
    *tag->record = (CeRecord){0};
    CeType *uint = basic(e,CE_T_UINT), *pointer = ce_type_pointer(s,basic(e,CE_T_VOID));
    tag->record->members = ce_alloc(&s->arena,4*sizeof(CeMember));
    const char *names[] = {"gp_offset","fp_offset","overflow_arg_area","reg_save_area"};
    CeType *types[] = {uint,uint,pointer,pointer};
    unsigned long long offsets[] = {0,4,8,16};
    for (size_t i = 0; i < 4; ++i)
        tag->record->members[tag->record->nmembers++] = (CeMember){.name=ce_cstr(&s->arena,names[i]),.type=types[i],.offset=offsets[i]};
    tag->record->size = 24; tag->record->align = 8; tag->record->complete = true;
    tag->builtin_tag = true;
    CeType *record = ce_type_new(s,CE_T_STRUCT); record->tag = tag;
    s->va_list_type = ce_type_array(s,record,1);
}

bool ce_elaborate(CeSema *sema, const CeTree *unit, CeError *error) {
    Elab e = {.sema=sema,.g=sema->grammar,.error=error};
    builtin_types(&e);
    if (kind_of(&e,unit) == CE_K_GNUEmptyTranslationUnit) return true;
    const CeTree *list = unit->children[0];
    for (size_t i = 0; i < list->len; ++i) {
        const CeTree *x = list->children[i];
        switch (kind_of(&e,x)) {
        case CE_K_ExternalFunction: if (!function_definition(&e,x->children[0])) return false; break;
        case CE_K_ExternalDeclaration: if (!declaration(&e,x->children[0])) return false; break;
        case CE_K_GNUExternalPragma: case CE_K_GNUEmptyExternalDeclaration: case CE_K_GNUFileAsm: case CE_K_GNUIdent: break;
        default: return fail(&e,x,"unknown external declaration");
        }
    }
    for (size_t i = 0; i < sema->nentities; ++i) {
        CeEntity *entity = sema->entities[i];
        if (!entity->needs_complete) continue;
        CeType *type = entity->type;
        if (type->kind == CE_T_ARRAY && type->length == -1) {
            type = ce_type_copy(sema,type); type->length = 1; entity->type = type;
        }
        if (!complete_object(type)) return fail(&e,entity->declaration,"an object definition has incomplete type");
    }
    return true;
}

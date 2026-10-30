/* The C type language (N1570 §6.2.5–6.2.7, §6.3.1) over a declared target
   ABI. Types are values; typedef names are kept as sugar on a type node and
   never change its identity. */

#include "sema.h"

#include <stdlib.h>
#include <string.h>

/* x86-64 System V, LP64, as GCC and Clang implement it. Every entry is
   checked by compiler probes over the corpora. */
const CeAbi ce_abi_x86_64_sysv = {
    .name = "x86_64-sysv-lp64",
    .size = {
        [CE_T_BOOL]=1, [CE_T_CHAR]=1, [CE_T_SCHAR]=1, [CE_T_UCHAR]=1, [CE_T_SHORT]=2, [CE_T_USHORT]=2,
        [CE_T_INT]=4, [CE_T_UINT]=4, [CE_T_LONG]=8, [CE_T_ULONG]=8, [CE_T_LLONG]=8, [CE_T_ULLONG]=8,
        [CE_T_INT128]=16, [CE_T_UINT128]=16, [CE_T_FLOAT]=4, [CE_T_DOUBLE]=8, [CE_T_LDOUBLE]=16,
        [CE_T_FLOAT32]=4, [CE_T_FLOAT64]=8, [CE_T_FLOAT128]=16, [CE_T_FLOAT32X]=8, [CE_T_FLOAT64X]=16,
        [CE_T_FLOAT16]=2, [CE_T_BFLOAT16]=2, [CE_T_POINTER]=8
    },
    .align = {
        [CE_T_BOOL]=1, [CE_T_CHAR]=1, [CE_T_SCHAR]=1, [CE_T_UCHAR]=1, [CE_T_SHORT]=2, [CE_T_USHORT]=2,
        [CE_T_INT]=4, [CE_T_UINT]=4, [CE_T_LONG]=8, [CE_T_ULONG]=8, [CE_T_LLONG]=8, [CE_T_ULLONG]=8,
        [CE_T_INT128]=16, [CE_T_UINT128]=16, [CE_T_FLOAT]=4, [CE_T_DOUBLE]=8, [CE_T_LDOUBLE]=16,
        [CE_T_FLOAT32]=4, [CE_T_FLOAT64]=8, [CE_T_FLOAT128]=16, [CE_T_FLOAT32X]=8, [CE_T_FLOAT64X]=16,
        [CE_T_FLOAT16]=2, [CE_T_BFLOAT16]=2, [CE_T_POINTER]=8
    },
    /* significand precision and exponent range identify a value set */
    .precision = {
        [CE_T_FLOAT]=24, [CE_T_DOUBLE]=53, [CE_T_LDOUBLE]=64, [CE_T_FLOAT32]=24, [CE_T_FLOAT64]=53,
        [CE_T_FLOAT128]=113, [CE_T_FLOAT32X]=53, [CE_T_FLOAT64X]=64, [CE_T_FLOAT16]=11, [CE_T_BFLOAT16]=8
    },
    .char_signed = true,
    .size_type = CE_T_ULONG, .ptrdiff_type = CE_T_LONG, .wchar_type = CE_T_INT,
    .char16_type = CE_T_USHORT, .char32_type = CE_T_UINT, .word_type = CE_T_LONG,
    .max_align = 16, .enum_policy = CE_ENUM_GNU
};

/* i386 System V, ILP32, as Clang targets i386-linux-gnu: 4-byte alignment for
   8-byte scalars and a 12-byte x87 long double; no __int128. */
const CeAbi ce_abi_i386_sysv = {
    .name = "i386-sysv-ilp32",
    .size = {
        [CE_T_BOOL]=1, [CE_T_CHAR]=1, [CE_T_SCHAR]=1, [CE_T_UCHAR]=1, [CE_T_SHORT]=2, [CE_T_USHORT]=2,
        [CE_T_INT]=4, [CE_T_UINT]=4, [CE_T_LONG]=4, [CE_T_ULONG]=4, [CE_T_LLONG]=8, [CE_T_ULLONG]=8,
        [CE_T_FLOAT]=4, [CE_T_DOUBLE]=8, [CE_T_LDOUBLE]=12, [CE_T_FLOAT32]=4, [CE_T_FLOAT64]=8,
        [CE_T_FLOAT128]=16, [CE_T_FLOAT32X]=8, [CE_T_FLOAT64X]=12, [CE_T_FLOAT16]=2, [CE_T_BFLOAT16]=2,
        [CE_T_POINTER]=4
    },
    .align = {
        [CE_T_BOOL]=1, [CE_T_CHAR]=1, [CE_T_SCHAR]=1, [CE_T_UCHAR]=1, [CE_T_SHORT]=2, [CE_T_USHORT]=2,
        [CE_T_INT]=4, [CE_T_UINT]=4, [CE_T_LONG]=4, [CE_T_ULONG]=4, [CE_T_LLONG]=4, [CE_T_ULLONG]=4,
        [CE_T_FLOAT]=4, [CE_T_DOUBLE]=4, [CE_T_LDOUBLE]=4, [CE_T_FLOAT32]=4, [CE_T_FLOAT64]=4,
        [CE_T_FLOAT128]=16, [CE_T_FLOAT32X]=4, [CE_T_FLOAT64X]=4, [CE_T_FLOAT16]=2, [CE_T_BFLOAT16]=2,
        [CE_T_POINTER]=4
    },
    .precision = {
        [CE_T_FLOAT]=24, [CE_T_DOUBLE]=53, [CE_T_LDOUBLE]=64, [CE_T_FLOAT32]=24, [CE_T_FLOAT64]=53,
        [CE_T_FLOAT128]=113, [CE_T_FLOAT32X]=53, [CE_T_FLOAT64X]=64, [CE_T_FLOAT16]=11, [CE_T_BFLOAT16]=8
    },
    .char_signed = true,
    .size_type = CE_T_UINT, .ptrdiff_type = CE_T_INT, .wchar_type = CE_T_INT,
    .char16_type = CE_T_USHORT, .char32_type = CE_T_UINT, .word_type = CE_T_INT,
    .max_align = 16, .enum_policy = CE_ENUM_GNU
};

/* x86-64 Windows, LLP64, as Clang targets x86_64-pc-windows-msvc: 4-byte long,
   long double with double's format, 16-bit wchar_t, enumerations of type int. */
const CeAbi ce_abi_x86_64_windows = {
    .name = "x86_64-windows-llp64",
    .size = {
        [CE_T_BOOL]=1, [CE_T_CHAR]=1, [CE_T_SCHAR]=1, [CE_T_UCHAR]=1, [CE_T_SHORT]=2, [CE_T_USHORT]=2,
        [CE_T_INT]=4, [CE_T_UINT]=4, [CE_T_LONG]=4, [CE_T_ULONG]=4, [CE_T_LLONG]=8, [CE_T_ULLONG]=8,
        [CE_T_INT128]=16, [CE_T_UINT128]=16, [CE_T_FLOAT]=4, [CE_T_DOUBLE]=8, [CE_T_LDOUBLE]=8,
        [CE_T_POINTER]=8
    },
    .align = {
        [CE_T_BOOL]=1, [CE_T_CHAR]=1, [CE_T_SCHAR]=1, [CE_T_UCHAR]=1, [CE_T_SHORT]=2, [CE_T_USHORT]=2,
        [CE_T_INT]=4, [CE_T_UINT]=4, [CE_T_LONG]=4, [CE_T_ULONG]=4, [CE_T_LLONG]=8, [CE_T_ULLONG]=8,
        [CE_T_INT128]=16, [CE_T_UINT128]=16, [CE_T_FLOAT]=4, [CE_T_DOUBLE]=8, [CE_T_LDOUBLE]=8,
        [CE_T_POINTER]=8
    },
    .precision = { [CE_T_FLOAT]=24, [CE_T_DOUBLE]=53, [CE_T_LDOUBLE]=53 },
    .char_signed = true,
    .size_type = CE_T_ULLONG, .ptrdiff_type = CE_T_LLONG, .wchar_type = CE_T_USHORT,
    .char16_type = CE_T_USHORT, .char32_type = CE_T_UINT, .word_type = CE_T_LLONG,
    .max_align = 16, .enum_policy = CE_ENUM_INT, .record_layout = CE_RECORD_MS
};

const CeAbi *ce_abi_named(const char *name) {
    const CeAbi *abis[] = {&ce_abi_x86_64_sysv,&ce_abi_i386_sysv,&ce_abi_x86_64_windows};
    for (size_t i = 0; i < 3; ++i) if (!strcmp(abis[i]->name,name)) return abis[i];
    return NULL;
}

static const char *kind_spelling[CE_T_KINDS] = {
    [CE_T_VOID]="void", [CE_T_BOOL]="_Bool", [CE_T_CHAR]="char", [CE_T_SCHAR]="signed char",
    [CE_T_UCHAR]="unsigned char", [CE_T_SHORT]="short", [CE_T_USHORT]="unsigned short",
    [CE_T_INT]="int", [CE_T_UINT]="unsigned int", [CE_T_LONG]="long", [CE_T_ULONG]="unsigned long",
    [CE_T_LLONG]="long long", [CE_T_ULLONG]="unsigned long long", [CE_T_INT128]="__int128",
    [CE_T_UINT128]="unsigned __int128", [CE_T_FLOAT]="float", [CE_T_DOUBLE]="double",
    [CE_T_LDOUBLE]="long double", [CE_T_FLOAT32]="_Float32", [CE_T_FLOAT64]="_Float64",
    [CE_T_FLOAT128]="_Float128", [CE_T_FLOAT32X]="_Float32x", [CE_T_FLOAT64X]="_Float64x",
    [CE_T_FLOAT16]="_Float16", [CE_T_BFLOAT16]="__bf16"
};

/* ---- construction --------------------------------------------------------- */

CeType *ce_type_new(CeSema *sema, unsigned kind) {
    CeType *t = ce_alloc(&sema->arena,sizeof(*t));
    *t = (CeType){.kind=kind};
    return t;
}
CeType *ce_type_basic(CeSema *sema, unsigned kind) {
    if (kind < CE_T_KINDS && sema->basic[kind]) return sema->basic[kind];
    CeType *t = ce_type_new(sema,kind);
    if (kind < CE_T_KINDS) sema->basic[kind] = t;
    return t;
}
CeType *ce_type_copy(CeSema *sema, const CeType *t) {
    CeType *c = ce_alloc(&sema->arena,sizeof(*c)); *c = *t; return c;
}
CeType *ce_type_qualified(CeSema *sema, const CeType *t, unsigned qualifiers) {
    if (!qualifiers) return (CeType *)t;
    if (t->kind == CE_T_ARRAY) {
        /* §6.7.3p9: qualifying an array type qualifies its element type. */
        CeType *c = ce_type_copy(sema,t); c->alias = NULL;
        c->base = ce_type_qualified(sema,t->base,qualifiers); return c;
    }
    if ((t->qualifiers | qualifiers) == t->qualifiers) return (CeType *)t;
    CeType *c = ce_type_copy(sema,t); c->qualifiers |= qualifiers; return c;
}
CeType *ce_type_unqualified(CeSema *sema, const CeType *t) {
    if (!t->qualifiers) return (CeType *)t;
    CeType *c = ce_type_copy(sema,t); c->qualifiers = 0; c->alias = NULL; return c;
}
CeType *ce_type_pointer(CeSema *sema, CeType *to) {
    CeType *t = ce_type_new(sema,CE_T_POINTER); t->base = to; return t;
}
CeType *ce_type_array(CeSema *sema, CeType *of, long long length) {
    CeType *t = ce_type_new(sema,CE_T_ARRAY); t->base = of; t->length = length; return t;
}

/* ---- classification ------------------------------------------------------- */

bool ce_type_integer(const CeType *t) {
    return (t->kind >= CE_T_BOOL && t->kind <= CE_T_UINT128) || t->kind == CE_T_ENUM;
}
bool ce_type_real_floating(const CeType *t) { return t->kind >= CE_T_FLOAT && t->kind <= CE_T_BFLOAT16; }
bool ce_type_floating(const CeType *t) { return ce_type_real_floating(t) || t->kind == CE_T_COMPLEX; }
bool ce_type_arithmetic(const CeType *t) { return ce_type_integer(t) || ce_type_floating(t); }
bool ce_type_scalar(const CeType *t) { return ce_type_arithmetic(t) || t->kind == CE_T_POINTER; }
bool ce_type_record(const CeType *t) { return t->kind == CE_T_STRUCT || t->kind == CE_T_UNION; }
bool ce_type_void(const CeType *t) { return t->kind == CE_T_VOID; }

/* An enumeration's compatible integer type (§6.7.2.2p4). */
unsigned ce_type_integer_kind(const CeType *t) {
    if (t->kind == CE_T_ENUM) return t->tag && t->tag->record && t->tag->record->enum_kind ?
        t->tag->record->enum_kind : CE_T_UINT;
    return t->kind;
}
bool ce_kind_signed(const CeAbi *abi, unsigned kind) {
    switch (kind) {
    case CE_T_CHAR: return abi->char_signed;
    case CE_T_SCHAR: case CE_T_SHORT: case CE_T_INT: case CE_T_LONG: case CE_T_LLONG: case CE_T_INT128: return true;
    default: return false;
    }
}
unsigned ce_kind_width(const CeAbi *abi, unsigned kind) {
    return kind == CE_T_BOOL ? 1 : abi->size[kind]*8;
}
/* §6.3.1.1p1 integer conversion rank. */
static unsigned rank(unsigned kind) {
    switch (kind) {
    case CE_T_BOOL: return 1;
    case CE_T_CHAR: case CE_T_SCHAR: case CE_T_UCHAR: return 2;
    case CE_T_SHORT: case CE_T_USHORT: return 3;
    case CE_T_INT: case CE_T_UINT: return 4;
    case CE_T_LONG: case CE_T_ULONG: return 5;
    case CE_T_LLONG: case CE_T_ULLONG: return 6;
    case CE_T_INT128: case CE_T_UINT128: return 7;
    default: return 0;
    }
}
static unsigned unsigned_of(unsigned kind) {
    switch (kind) {
    case CE_T_CHAR: case CE_T_SCHAR: return CE_T_UCHAR;
    case CE_T_SHORT: return CE_T_USHORT;
    case CE_T_INT: return CE_T_UINT;
    case CE_T_LONG: return CE_T_ULONG;
    case CE_T_LLONG: return CE_T_ULLONG;
    case CE_T_INT128: return CE_T_UINT128;
    default: return kind;
    }
}

/* §6.3.1.1p2: the integer promotions. A bit-field of width `bits` (nonzero)
   promotes by the values its width can represent. */
unsigned ce_promoted_kind(const CeAbi *abi, unsigned kind, unsigned bits) {
    unsigned width = bits ? bits : ce_kind_width(abi,kind);
    bool is_signed = ce_kind_signed(abi,kind);
    unsigned int_width = ce_kind_width(abi,CE_T_INT);
    if (bits) {
        /* GCC: bit-fields narrower than int promote to int or unsigned int by
           value range; wider ones keep their declared type. */
        if (width < int_width || (width == int_width && is_signed)) return CE_T_INT;
        if (width == int_width) return CE_T_UINT;
        return kind;
    }
    if (rank(kind) > rank(CE_T_INT)) return kind;
    if (kind == CE_T_INT || kind == CE_T_UINT) return kind;
    if (width < int_width || (width == int_width && is_signed)) return CE_T_INT;
    return CE_T_UINT;
}
CeType *ce_type_promoted(CeSema *sema, const CeType *t, unsigned bits) {
    if (!ce_type_integer(t)) return ce_type_unqualified(sema,t);
    unsigned kind = ce_type_integer_kind(t);
    unsigned promoted = ce_promoted_kind(sema->abi,kind,bits);
    if (promoted == kind && !bits && t->kind != CE_T_ENUM) return ce_type_unqualified(sema,t);
    if (promoted == kind && t->kind == CE_T_ENUM && rank(kind) > rank(CE_T_INT)) return ce_type_unqualified(sema,t);
    return ce_type_basic(sema,promoted);
}

/* Floating ranks (§6.3.1.8 with ISO/IEC TS 18661-3): by precision; at equal
   precision an interchange type outranks the standard type, which outranks
   an extended type. Precision orders the value sets except for _Float16 and
   __bf16, which GCC and Clang also convert to _Float16. */
static unsigned floating_rank(const CeAbi *abi, unsigned kind) {
    unsigned tier = kind == CE_T_FLOAT16 || kind == CE_T_FLOAT32 || kind == CE_T_FLOAT64 || kind == CE_T_FLOAT128 ? 2 :
        kind == CE_T_FLOAT32X || kind == CE_T_FLOAT64X || kind == CE_T_BFLOAT16 ? 0 : 1;
    /* float < double < long double even where formats coincide (§6.3.1.8) */
    unsigned standard = kind == CE_T_LDOUBLE ? 2 : kind == CE_T_DOUBLE ? 1 : 0;
    return abi->precision[kind]*16 + tier*4 + standard;
}

/* An arithmetic type as a small code: integer and real kinds, complex = 64 + real. */
static unsigned arithmetic_code(const CeType *t, unsigned bits, const CeAbi *abi) {
    if (t->kind == CE_T_COMPLEX) return 64 + t->base->kind;
    unsigned kind = ce_type_integer(t) ? ce_type_integer_kind(t) : t->kind;
    return bits ? ce_promoted_kind(abi,kind,bits) : kind;
}
/* §6.3.1.8: the common real type of the usual arithmetic conversions. */
static CeType *common_type(CeSema *sema, const CeType *a, const CeType *b, unsigned abits, unsigned bbits);
CeType *ce_type_common(CeSema *sema, const CeType *a, const CeType *b, unsigned abits, unsigned bbits) {
    CeType *c = common_type(sema,a,b,abits,bbits);
    /* record each distinct step the program used, for kernel admission */
    unsigned x = arithmetic_code(a,abits,sema->abi), y = arithmetic_code(b,bbits,sema->abi);
    if (x < 128 && y < 128) sema->arithmetic_used[x][y] = true;
    return c;
}
static CeType *common_type(CeSema *sema, const CeType *a, const CeType *b, unsigned abits, unsigned bbits) {
    const CeAbi *abi = sema->abi;
    bool complex = a->kind == CE_T_COMPLEX || b->kind == CE_T_COMPLEX;
    const CeType *ra = a->kind == CE_T_COMPLEX ? a->base : a, *rb = b->kind == CE_T_COMPLEX ? b->base : b;
    CeType *real;
    if (ce_type_real_floating(ra) || ce_type_real_floating(rb)) {
        unsigned ka = ce_type_real_floating(ra) ? ra->kind : 0, kb = ce_type_real_floating(rb) ? rb->kind : 0;
        unsigned kind = !ka ? kb : !kb ? ka : floating_rank(abi,ka) >= floating_rank(abi,kb) ? ka : kb;
        real = ce_type_basic(sema,kind);
    } else {
        unsigned ka = ce_type_promoted(sema,ra,abits)->kind, kb = ce_type_promoted(sema,rb,bbits)->kind;
        if (ka == CE_T_ENUM) ka = ce_type_integer_kind(ra);
        if (kb == CE_T_ENUM) kb = ce_type_integer_kind(rb);
        unsigned kind;
        if (ka == kb) kind = ka;
        else if (ce_kind_signed(abi,ka) == ce_kind_signed(abi,kb)) kind = rank(ka) >= rank(kb) ? ka : kb;
        else {
            unsigned s = ce_kind_signed(abi,ka) ? ka : kb, u = ce_kind_signed(abi,ka) ? kb : ka;
            if (rank(u) >= rank(s)) kind = u;
            else if (ce_kind_width(abi,s) > ce_kind_width(abi,u)) kind = s;
            else kind = unsigned_of(s);
        }
        real = ce_type_basic(sema,kind);
    }
    if (!complex) return real;
    CeType *c = ce_type_new(sema,CE_T_COMPLEX); c->base = real; return c;
}

/* ---- size, alignment ------------------------------------------------------ */

bool ce_type_complete(const CeType *t) {
    switch (t->kind) {
    case CE_T_VOID: case CE_T_FUNCTION: return false;
    case CE_T_ARRAY: return t->length >= 0 && ce_type_complete(t->base);
    case CE_T_STRUCT: case CE_T_UNION: case CE_T_ENUM: return t->tag && t->tag->record && t->tag->record->complete;
    default: return true;
    }
}
unsigned long long ce_type_size(const CeSema *sema, const CeType *t) {
    const CeAbi *abi = sema->abi;
    switch (t->kind) {
    case CE_T_VOID: case CE_T_FUNCTION: return 1;   /* GNU: sizeof(void) == 1 */
    case CE_T_ENUM: return abi->size[ce_type_integer_kind(t)];
    case CE_T_COMPLEX: return 2*ce_type_size(sema,t->base);
    case CE_T_ARRAY: return t->length > 0 ? (unsigned long long)t->length*ce_type_size(sema,t->base) : 0;
    case CE_T_STRUCT: case CE_T_UNION: return t->tag && t->tag->record ? t->tag->record->size : 0;
    default: return abi->size[t->kind];
    }
}
unsigned ce_type_align(const CeSema *sema, const CeType *t) {
    const CeAbi *abi = sema->abi;
    unsigned natural;
    switch (t->kind) {
    case CE_T_VOID: case CE_T_FUNCTION: natural = 1; break;
    case CE_T_ENUM:
        natural = t->tag && t->tag->record && t->tag->record->complete
            ? t->tag->record->align : abi->align[ce_type_integer_kind(t)];
        break;
    case CE_T_COMPLEX: natural = ce_type_align(sema,t->base); break;
    case CE_T_ARRAY: natural = ce_type_align(sema,t->base); break;
    case CE_T_STRUCT: case CE_T_UNION: natural = t->tag && t->tag->record ? t->tag->record->align : 1; break;
    default: natural = abi->align[t->kind]; break;
    }
    /* An alignment attribute on a typedef may raise or (GNU) lower it. */
    return t->alignment ? t->alignment : natural;
}

/* ---- identity ---------------------------------------------------------------- */

/* The same type, not merely compatible: typedef names are sugar, and an
   unprototyped function type or an incomplete array differs from its
   compatible completions. */
bool ce_type_same(const CeType *a, const CeType *b) {
    if (a == b) return true;
    if (a->kind != b->kind || a->qualifiers != b->qualifiers) return false;
    switch (a->kind) {
    case CE_T_POINTER: case CE_T_COMPLEX: return ce_type_same(a->base,b->base);
    case CE_T_ARRAY: return a->length == b->length && ce_type_same(a->base,b->base);
    case CE_T_STRUCT: case CE_T_UNION: case CE_T_ENUM: return a->tag == b->tag;
    case CE_T_FUNCTION:
        if (!ce_type_same(a->base,b->base) || a->prototype != b->prototype || a->variadic != b->variadic ||
            a->nparameters != b->nparameters) return false;
        for (size_t i = 0; i < a->nparameters; ++i) if (!ce_type_same(a->parameters[i],b->parameters[i])) return false;
        return true;
    default: return true;
    }
}
/* Types that differ at most in qualifiers, through pointers and arrays. */
bool ce_type_similar(const CeType *a, const CeType *b) {
    if (a->kind != b->kind) return false;
    if (a->kind == CE_T_POINTER) return ce_type_similar(a->base,b->base);
    if (a->kind == CE_T_ARRAY) return a->length == b->length && ce_type_similar(a->base,b->base);
    CeType ua = *a, ub = *b; ua.qualifiers = ub.qualifiers = 0;
    return ce_type_same(&ua,&ub);
}

/* ---- compatibility (§6.2.7) ------------------------------------------------ */

static bool parameters_compatible(CeSema *sema, const CeType *a, const CeType *b);

bool ce_type_compatible(CeSema *sema, const CeType *a, const CeType *b) {
    if (a == b) return true;
    if (a->qualifiers != b->qualifiers) return false;
    unsigned ka = a->kind, kb = b->kind;
    /* §6.7.2.2p4: an enumeration is compatible with its integer type. */
    if (ka == CE_T_ENUM && kb != CE_T_ENUM) return ce_type_integer_kind(a) == kb;
    if (kb == CE_T_ENUM && ka != CE_T_ENUM) return ce_type_integer_kind(b) == ka;
    if (ka != kb) return false;
    switch (ka) {
    case CE_T_POINTER: return ce_type_compatible(sema,a->base,b->base);
    case CE_T_COMPLEX: return ce_type_compatible(sema,a->base,b->base);
    case CE_T_ARRAY:
        return ce_type_compatible(sema,a->base,b->base) &&
            (a->length < 0 || b->length < 0 || a->length == b->length);
    case CE_T_STRUCT: case CE_T_UNION: case CE_T_ENUM: return a->tag == b->tag;
    case CE_T_FUNCTION:
        return ce_type_compatible(sema,ce_type_unqualified(sema,a->base),ce_type_unqualified(sema,b->base)) &&
            parameters_compatible(sema,a,b);
    default: return true;
    }
}
/* §6.7.6.3p15. */
static bool parameters_compatible(CeSema *sema, const CeType *a, const CeType *b) {
    if (a->prototype && b->prototype) {
        if (a->nparameters != b->nparameters || a->variadic != b->variadic) return false;
        for (size_t i = 0; i < a->nparameters; ++i)
            if (!ce_type_compatible(sema,ce_type_unqualified(sema,a->parameters[i]),
                                    ce_type_unqualified(sema,b->parameters[i]))) return false;
        return true;
    }
    if (!a->prototype && !b->prototype) return true;
    const CeType *p = a->prototype ? a : b, *old = a->prototype ? b : a;
    if (p->variadic) return false;
    if (old->nparameters && p->nparameters != old->nparameters) return false;
    for (size_t i = 0; i < p->nparameters; ++i) {
        CeType *q = ce_type_unqualified(sema,p->parameters[i]);
        CeType *promoted = ce_type_default_promoted(sema,old->nparameters ? old->parameters[i] : q);
        if (!ce_type_compatible(sema,q,promoted)) return false;
    }
    return true;
}
/* §6.5.2.2p6 default argument promotions. */
CeType *ce_type_default_promoted(CeSema *sema, const CeType *t) {
    if (t->kind == CE_T_FLOAT) return ce_type_basic(sema,CE_T_DOUBLE);
    return ce_type_promoted(sema,t,0);
}

/* §6.2.7p3 composite type. */
CeType *ce_type_composite(CeSema *sema, CeType *a, CeType *b) {
    if (a == b) return a;
    if (a->kind == CE_T_ARRAY && b->kind == CE_T_ARRAY) {
        CeType *c = ce_type_copy(sema,a->length >= 0 ? a : b);
        c->base = ce_type_composite(sema,a->base,b->base); return c;
    }
    if (a->kind == CE_T_POINTER && b->kind == CE_T_POINTER) {
        CeType *c = ce_type_copy(sema,a); c->base = ce_type_composite(sema,a->base,b->base); return c;
    }
    if (a->kind == CE_T_FUNCTION && b->kind == CE_T_FUNCTION) {
        if (!a->prototype && b->prototype) { CeType *t = a; a = b; b = t; }
        CeType *c = ce_type_copy(sema,a);
        c->base = ce_type_composite(sema,a->base,b->base);
        if (a->prototype && b->prototype && a->nparameters == b->nparameters) {
            c->parameters = ce_alloc(&sema->arena,a->nparameters*sizeof(*c->parameters));
            for (size_t i = 0; i < a->nparameters; ++i)
                c->parameters[i] = ce_type_composite(sema,a->parameters[i],b->parameters[i]);
        }
        return c;
    }
    return a;
}

/* ---- printing as C type names ---------------------------------------------- */

typedef struct { char *data; size_t len, capacity; } Text;
static void put(Text *t, const char *s, size_t n) {
    if (t->len+n+1 > t->capacity) {
        t->capacity = (t->len+n+1)*2;
        t->data = ce_resize(t->data,t->capacity,1);
    }
    memcpy(t->data+t->len,s,n); t->len += n; t->data[t->len] = 0;
}
static void puts_(Text *t, const char *s) { put(t,s,strlen(s)); }
static void prepend(Text *t, const char *s) {
    size_t n = strlen(s);
    if (t->len+n+1 > t->capacity) { t->capacity = (t->len+n+1)*2; t->data = ce_resize(t->data,t->capacity,1); }
    memmove(t->data+n,t->data,t->len+1); memcpy(t->data,s,n); t->len += n;
}
static void qualifier_words(Text *t, unsigned q, bool leading_space) {
    static const struct { unsigned bit; const char *word; } words[] = {
        {CE_Q_CONST,"const"},{CE_Q_VOLATILE,"volatile"},{CE_Q_RESTRICT,"restrict"},{CE_Q_ATOMIC,"_Atomic"}};
    for (size_t i = 0; i < 4; ++i) if (q & words[i].bit) {
        if (leading_space || t->len) puts_(t," ");
        puts_(t,words[i].word);
    }
}

/* Whether a type, as printed, needs a name that is unavailable. */
static bool print_declarator(CeSema *sema, const CeType *t, Text *specifiers, Text *declarator, unsigned flags);

static bool print_base(CeSema *sema, const CeType *t, Text *out, unsigned flags) {
    Text q = {0}; qualifier_words(&q,t->qualifiers,false);
    if (q.len) { put(out,q.data,q.len); puts_(out," "); }
    free(q.data);
    if ((flags & CE_PRINT_SUGAR) && t->alias) { put(out,(const char *)t->alias->name.data,t->alias->name.len); return true; }
    /* GCC and Clang compare a qualified enumeration only with itself, so
       only an unqualified one is written as its integer type. */
    if ((flags & CE_PRINT_COMPATIBLE) && t->kind == CE_T_ENUM && !t->qualifiers && t->tag && t->tag->record &&
        t->tag->record->complete) {
        puts_(out,kind_spelling[ce_type_integer_kind(t)]); return true;
    }
    switch (t->kind) {
    case CE_T_STRUCT: case CE_T_UNION: case CE_T_ENUM: {
        const char *word = t->kind == CE_T_STRUCT ? "struct " : t->kind == CE_T_UNION ? "union " : "enum ";
        /* A target-provided record has no name a program can write; GCC's
           va_list element is reachable through the predeclared typedef. */
        if (t->tag && t->tag->builtin_tag) {
            if (flags & CE_PRINT_DECLARED) { puts_(out,word); put(out,(const char *)t->tag->name.data,t->tag->name.len); }
            else puts_(out,"__typeof__((*(__builtin_va_list *)0)[0])");
            return true;
        }
        if (t->tag && t->tag->name.len) { puts_(out,word); put(out,(const char *)t->tag->name.data,t->tag->name.len); return true; }
        if (t->tag && t->tag->naming_alias) {
            put(out,(const char *)t->tag->naming_alias->name.data,t->tag->naming_alias->name.len); return true;
        }
        /* An anonymous record is named by a member that has it:
           __typeof__((*(struct outer *)0).inner[0]) */
        if (t->tag && t->tag->witness_record && t->tag->witness_record->tag != t->tag) {
            /* members of an anonymous member are members of its container */
            CeType *record = t->tag->witness_record;
            for (size_t hops = 0; hops < 64 && record->tag && !record->tag->name.len && !record->tag->naming_alias &&
                 record->tag->anonymous_in; ++hops) record = record->tag->anonymous_in;
            record = ce_type_unqualified(sema,record);
            record->alias = NULL;
            char *outer = ce_type_text(sema,ce_type_pointer(sema,record),flags & ~CE_PRINT_ANYWAY);
            if (outer) {
                puts_(out,"__typeof__((*("); puts_(out,outer); puts_(out,")0)");
                puts_(out,t->tag->witness_path); puts_(out,")");
                free(outer); return true;
            }
        }
        if (t->tag && t->tag->witness_object) {
            puts_(out,"__typeof__(("); put(out,(const char *)t->tag->witness_object->name.data,t->tag->witness_object->name.len);
            puts_(out,")"); puts_(out,t->tag->witness_path); puts_(out,")");
            return true;
        }
        puts_(out,word); puts_(out,"<anonymous>"); return false;
    }
    case CE_T_COMPLEX: puts_(out,"_Complex "); return print_base(sema,t->base,out,flags);
    default: puts_(out,t->kind < CE_T_KINDS && kind_spelling[t->kind] ? kind_spelling[t->kind] : "<type>"); return true;
    }
}
static bool print_declarator(CeSema *sema, const CeType *t, Text *specifiers, Text *declarator, unsigned flags) {
    if ((flags & CE_PRINT_SUGAR) && t->alias) return print_base(sema,t,specifiers,flags);
    switch (t->kind) {
    case CE_T_POINTER: {
        Text q = {0}; qualifier_words(&q,t->qualifiers,false);
        if (q.len) { prepend(declarator," "); prepend(declarator,q.data); }
        free(q.data);
        prepend(declarator,"*");
        if (t->base->kind == CE_T_ARRAY || (t->base->kind == CE_T_FUNCTION && !((flags & CE_PRINT_SUGAR) && t->base->alias))) {
            prepend(declarator,"("); puts_(declarator,")");
        }
        return print_declarator(sema,t->base,specifiers,declarator,flags);
    }
    case CE_T_ARRAY: {
        char size[48];
        if (t->length >= 0) snprintf(size,sizeof(size),"[%lld]",t->length); else strcpy(size,"[]");
        puts_(declarator,size);
        return print_declarator(sema,t->base,specifiers,declarator,flags);
    }
    case CE_T_FUNCTION: {
        puts_(declarator,"(");
        bool ok = true;
        if (t->prototype) {
            if (!t->nparameters && !t->variadic) puts_(declarator,"void");
            for (size_t i = 0; i < t->nparameters; ++i) {
                if (i) puts_(declarator,", ");
                char *p = ce_type_text(sema,t->parameters[i],flags);
                if (!p) { ok = false; puts_(declarator,"<?>"); continue; }
                puts_(declarator,p); free(p);
            }
            if (t->variadic) puts_(declarator,", ...");
        }
        puts_(declarator,")");
        return print_declarator(sema,t->base,specifiers,declarator,flags) && ok;
    }
    default: return print_base(sema,t,specifiers,flags);
    }
}
/* A C type name, e.g. "unsigned long", "char *", "int (*)(int, char *)".
   Returns NULL when the type contains a record that cannot be named. */
char *ce_type_text(CeSema *sema, const CeType *t, unsigned flags) {
    Text specifiers = {0}, declarator = {0};
    bool ok = print_declarator(sema,t,&specifiers,&declarator,flags);
    if (declarator.len && declarator.data[0] != '[' && declarator.data[0] != '(') puts_(&specifiers," ");
    else if (declarator.len && declarator.data[0] == '(' && specifiers.len) puts_(&specifiers," ");
    if (declarator.len) put(&specifiers,declarator.data,declarator.len);
    free(declarator.data);
    if (!specifiers.data) puts_(&specifiers,"");
    if (!ok && !(flags & CE_PRINT_ANYWAY)) { free(specifiers.data); return NULL; }
    return specifiers.data;
}

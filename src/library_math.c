#include "library_math.h"
#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(double)==8 && FLT_RADIX==2 && DBL_MANT_DIG==53 &&
               DBL_MIN_EXP==-1021 && DBL_MAX_EXP==1024,
               "binary64 decomposition requires IEEE754 binary64");

static Atom *integer_refused(Arena *a,const char *reason) {
    return atom_expr2(a,atom_symbol(a,"IntegerDivmodRefusedV1"),atom_symbol(a,reason));
}
#if CETTA_BUILD_WITH_GMP
static bool exact_integer(const Atom *value,mpz_t out) {
    if (!value || value->kind!=ATOM_GROUNDED) return false;
    if (value->ground.gkind==GV_BIGINT) return atom_bigint_get_mpz(value,out);
    if (value->ground.gkind!=GV_INT) return false;
    int64_t input=value->ground.ival;
    uint64_t magnitude=input<0 ? (uint64_t)(-(input+1))+1u : (uint64_t)input;
    mpz_import(out,1,-1,sizeof(magnitude),0,0,&magnitude);
    if (input<0) mpz_neg(out,out);
    return true;
}
#endif
static Atom *integer_divmod(Arena *a,Atom **args,uint32_t n) {
    if (n!=2) return integer_refused(a,"expected-integers");
#if CETTA_BUILD_WITH_GMP
    mpz_t left,right,quotient,remainder;mpz_inits(left,right,quotient,remainder,NULL);
    Atom *result;
    if (!exact_integer(args[0],left) || !exact_integer(args[1],right))
        result=integer_refused(a,"expected-integers");
    else if (!mpz_sgn(right)) result=integer_refused(a,"division-by-zero");
    else {
        mpz_fdiv_qr(quotient,remainder,left,right);
        result=atom_expr3(a,atom_symbol(a,"IntegerDivmodV1"),
            atom_bigint_from_mpz(a,quotient),atom_bigint_from_mpz(a,remainder));
    }
    mpz_clears(left,right,quotient,remainder,NULL);return result;
#else
    if (!args[0] || !args[1] || args[0]->kind!=ATOM_GROUNDED || args[1]->kind!=ATOM_GROUNDED ||
        args[0]->ground.gkind!=GV_INT || args[1]->ground.gkind!=GV_INT)
        return integer_refused(a,"expected-integers");
    int64_t left=args[0]->ground.ival,right=args[1]->ground.ival;
    if (!right) return integer_refused(a,"division-by-zero");
    if (left==INT64_MIN && right==-1) return integer_refused(a,"requires-gmp");
    int64_t quotient=left/right,remainder=left%right;
    if (remainder && ((remainder<0)!=(right<0))) { --quotient;remainder+=right; }
    return atom_expr3(a,atom_symbol(a,"IntegerDivmodV1"),atom_int(a,quotient),atom_int(a,remainder));
#endif
}

Atom *cetta_math_dispatch(Arena *a, Atom *head, Atom **args, uint32_t n) {
    if (atom_is_symbol(head,"__cetta_lib_math_integer_divmod")) return integer_divmod(a,args,n);
    if (!atom_is_symbol(head,"__cetta_lib_math_binary64_parts")) return NULL;
    if (n!=1 || !args[0] || args[0]->kind!=ATOM_GROUNDED || args[0]->ground.gkind!=GV_FLOAT)
        return atom_expr2(a,atom_symbol(a,"Binary64RefusedV1"),atom_symbol(a,"expected-float"));
    uint64_t bits;
    memcpy(&bits,&args[0]->ground.fval,sizeof(bits));
    int sign=(bits>>63) ? -1 : 1;
    uint64_t fraction=bits&UINT64_C(0x000fffffffffffff);
    unsigned exponent=(unsigned)((bits>>52)&2047u);
    if (exponent==2047u) {
        if (!fraction) return atom_expr2(a,atom_symbol(a,"Binary64InfinityV1"),atom_int(a,sign));
        return atom_expr3(a,atom_symbol(a,"Binary64NaNV1"),atom_int(a,sign),atom_int(a,(int64_t)fraction));
    }
    int power=exponent ? (int)exponent-1075 : -1074;
    uint64_t mantissa=exponent ? fraction+(UINT64_C(1)<<52) : fraction;
    Atom *fields[]={atom_symbol(a,"Binary64PartsV1"),atom_int(a,sign),
                   atom_int(a,(int64_t)mantissa),atom_int(a,power)};
    return atom_expr(a,fields,4);
}

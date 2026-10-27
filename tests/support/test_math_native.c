#include "library_math.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define check(test) do { ++checks; if (!(test)) { \
    fprintf(stderr,"math boundary failed at line %d: %s\n",__LINE__,#test);exit(1); \
} } while (0)

static bool shape(const Atom *value,const char *tag,unsigned length) {
    return value && value->kind==ATOM_EXPR && value->expr.len==length &&
        atom_is_symbol(value->expr.elems[0],tag);
}
static Atom *call(Arena *arena,const char *name,Atom **arguments,unsigned count) {
    Atom *result=cetta_math_dispatch(arena,atom_symbol(arena,name),arguments,count);
    check(result);return result;
}
static bool small_integer(const Atom *value,int64_t wanted) {
    return value && value->kind==ATOM_GROUNDED && value->ground.gkind==GV_INT &&
        value->ground.ival==wanted;
}
int main(void) {
    SymbolTable symbols;symbol_table_init(&symbols);g_symbols=&symbols;
    symbol_table_init_builtins(&symbols,&g_builtin_syms);
    Arena arena;arena_init_detached(&arena);
    const char *parts="__cetta_lib_math_binary64_parts";
    const char *divide="__cetta_lib_math_integer_divmod";
    check(!cetta_math_dispatch(&arena,atom_symbol(&arena,"unrelated"),NULL,0));
    Atom *bad=call(&arena,parts,NULL,0);
    check(shape(bad,"Binary64RefusedV1",2));
    check(shape(call(&arena,divide,NULL,0),"IntegerDivmodRefusedV1",2));
    Atom *missing[]={NULL,NULL};
    check(shape(call(&arena,parts,missing,1),"Binary64RefusedV1",2));
    check(shape(call(&arena,divide,missing,2),"IntegerDivmodRefusedV1",2));
    const int64_t values[]={INT64_MIN,INT64_MIN+1,-17,-1,0,1,17,INT64_MAX};
    for (unsigned i=0;i<sizeof(values)/sizeof(*values);++i)
        for (unsigned j=0;j<sizeof(values)/sizeof(*values);++j) {
            int64_t left=values[i],right=values[j];
            Atom *arguments[]={atom_int(&arena,left),atom_int(&arena,right)};
            Atom *result=call(&arena,divide,arguments,2);
            if (!right) {
                check(shape(result,"IntegerDivmodRefusedV1",2));
                check(atom_is_symbol(result->expr.elems[1],"division-by-zero"));
            } else if (left==INT64_MIN && right==-1) {
#if CETTA_BUILD_WITH_GMP
                check(shape(result,"IntegerDivmodV1",3));
                check(!strcmp(atom_bigint_cstr(result->expr.elems[1]),"9223372036854775808"));
                check(small_integer(result->expr.elems[2],0));
#else
                check(shape(result,"IntegerDivmodRefusedV1",2));
                check(atom_is_symbol(result->expr.elems[1],"requires-gmp"));
#endif
            } else {
                int64_t quotient=left/right,remainder=left%right;
                if (remainder && ((remainder<0)!=(right<0))) { --quotient;remainder+=right; }
                check(shape(result,"IntegerDivmodV1",3));
                check(small_integer(result->expr.elems[1],quotient));
                check(small_integer(result->expr.elems[2],remainder));
            }
        }
    for (unsigned exponent=0;exponent<2048;++exponent)
        for (unsigned negative=0;negative<2;++negative) {
            uint64_t word=((uint64_t)negative<<63)|((uint64_t)exponent<<52)|1;
            double value;memcpy(&value,&word,sizeof(value));
            Atom *arguments[]={atom_float(&arena,value)};
            Atom *result=call(&arena,parts,arguments,1);
            if (exponent==2047) {
                check(shape(result,"Binary64NaNV1",3));
                check(small_integer(result->expr.elems[2],1));
            } else {
                check(shape(result,"Binary64PartsV1",4));
                check(small_integer(result->expr.elems[2],exponent ? INT64_C(4503599627370497) : 1));
                check(small_integer(result->expr.elems[3],exponent ? (int)exponent-1075 : -1074));
            }
            check(small_integer(result->expr.elems[1],negative ? -1 : 1));
        }
    arena_free(&arena);symbol_table_free(&symbols);g_symbols=NULL;
    printf("native math boundary: %u checks passed\n",checks);return 0;
}

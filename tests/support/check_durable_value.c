#include "durable_value.h"
#include "symbol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void roundtrip(Arena *a, Atom *value) {
    unsigned char *bytes=NULL,*again=NULL; size_t size=0,n=0; Atom *copy=NULL;
    assert(cetta_durable_value_encode(value,&bytes,&size)==DURABLE_OK);
    assert(cetta_durable_value_decode(a,bytes,size,&copy)==DURABLE_OK);
    assert(cetta_durable_value_encode(copy,&again,&n)==DURABLE_OK);
    assert(size==n && !memcmp(bytes,again,n));
    for (size_t i=0;i<size;++i) {
        Atom *bad=NULL;
        assert(cetta_durable_value_decode(a,bytes,i,&bad)!=DURABLE_OK && !bad);
    }
    bytes=realloc(bytes,size+1); assert(bytes); bytes[size]=0;
    assert(cetta_durable_value_decode(a,bytes,size+1,&copy)==DURABLE_CORRUPT);
    free(bytes); free(again);
}

int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    Arena a; arena_init(&a);
    Atom *values[]={atom_symbol(&a,"symbol \\\""),atom_int(&a,INT64_MIN),atom_int(&a,INT64_MAX),
        atom_float(&a,-0.0),atom_float(&a,NAN),atom_float(&a,INFINITY),atom_float(&a,1e-310),
        atom_bool(&a,true),atom_bool(&a,false),atom_string(&a,"\nquoted \" 雪\\"),atom_expr(&a,NULL,0)};
    roundtrip(&a,atom_expr(&a,values,sizeof(values)/sizeof(*values)));
    roundtrip(&a,atom_string_n(&a,"a\0b",3));
    roundtrip(&a,atom_string_n(&a,"\0",1));
#if CETTA_BUILD_WITH_GMP
    roundtrip(&a,atom_bigint(&a,"184467440737095516160"));
    roundtrip(&a,atom_rational(&a,"-184467440737095516160/3"));
#endif
    unsigned char *bytes=NULL; size_t size=0;
    /* Native I/O envelopes have the exact same wire form without any symbol
     * table access. Re-encode via Atoms to cross-check the independent path. */
    CettaDurableField fields[]={
        {.kind=DURABLE_FIELD_SYMBOL,.text={"host:fact",9}},
        {.kind=DURABLE_FIELD_TEXT,.text={"external-id",11}},
        {.kind=DURABLE_FIELD_INT,.integer=INT64_MIN},
        {.kind=DURABLE_FIELD_BOOL,.boolean=true},
        {.kind=DURABLE_FIELD_EXPR,.expression={NULL,0}}
    };
    CettaDurableField native={.kind=DURABLE_FIELD_EXPR,.expression={fields,5}};
    g_symbols=NULL;
    assert(cetta_durable_fields_encode(&native,&bytes,&size)==DURABLE_OK);
    g_symbols=&symbols;
    Atom *decoded=NULL; unsigned char *again=NULL; size_t n=0;
    assert(cetta_durable_value_decode(&a,bytes,size,&decoded)==DURABLE_OK);
    assert(cetta_durable_value_encode(decoded,&again,&n)==DURABLE_OK && n==size && !memcmp(bytes,again,n));
    free(bytes); free(again); bytes=NULL;
    fields[1].text.data="a\0b"; fields[1].text.size=3;
    assert(cetta_durable_fields_encode(&native,&bytes,&size)==DURABLE_INVALID && !bytes);
    fields[1].text.data=NULL;
    assert(cetta_durable_fields_encode(&native,&bytes,&size)==DURABLE_INVALID && !bytes);
    native.expression.items=&native; native.expression.count=1;
    assert(cetta_durable_fields_encode(&native,&bytes,&size)==DURABLE_LIMIT && !bytes);
    fields[0].text.data="NativeHandle"; fields[0].text.size=12;
    native.expression.items=fields;
    assert(cetta_durable_fields_encode(&native,&bytes,&size)==DURABLE_INVALID && !bytes);
    assert(cetta_durable_value_encode(atom_var(&a,"x"),&bytes,&size)==DURABLE_INVALID);
    SymbolId binary_name=symbol_intern_bytes(g_symbols,(const uint8_t *)"x\0y",3);
    assert(cetta_durable_value_encode(atom_symbol_id(&a,binary_name),&bytes,&size)==DURABLE_INVALID);
    Atom *handle=atom_expr3(&a,atom_symbol(&a,"NativeHandle"),atom_string(&a,"x"),atom_int(&a,1));
    assert(cetta_durable_value_encode(handle,&bytes,&size)==DURABLE_INVALID);
    Atom *deep=atom_int(&a,0);
    for (int i=0;i<140;++i) deep=atom_expr(&a,&deep,1);
    assert(cetta_durable_value_encode(deep,&bytes,&size)==DURABLE_LIMIT);
    Atom *dag=atom_int(&a,0);
    for (int i=0;i<40;++i) dag=atom_expr2(&a,dag,dag);
    assert(cetta_durable_value_encode(dag,&bytes,&size)==DURABLE_LIMIT);
    const unsigned char zero_denominator[]={'C','D','V','1','Q',4,0,0,0,'1','/','-','0'};
    Atom *bad=NULL;
    assert(cetta_durable_value_decode(&a,zero_denominator,sizeof(zero_denominator),&bad)==DURABLE_CORRUPT);
    const unsigned char huge_expr[]={'C','D','V','1','E',255,255,255,255};
    assert(cetta_durable_value_decode(&a,huge_expr,sizeof(huge_expr),&bad)==DURABLE_CORRUPT);
    const unsigned char unknown_tag[]="CDV1?", unknown_version[]="CDV2Y", bad_header[]="bad1Y";
    assert(cetta_durable_value_decode(&a,unknown_tag,sizeof(unknown_tag)-1,&bad)==DURABLE_CORRUPT);
    assert(cetta_durable_value_decode(&a,unknown_version,sizeof(unknown_version)-1,&bad)==DURABLE_VERSION);
    assert(cetta_durable_value_decode(&a,bad_header,sizeof(bad_header)-1,&bad)==DURABLE_CORRUPT);
    arena_free(&a); symbol_table_free(&symbols); g_symbols=NULL;
    puts("durable values: bit-exact roundtrips, truncation, trailing data, unsafe values, depth and expansion limits passed");
}

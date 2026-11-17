#define _POSIX_C_SOURCE 200809L
#include "atom.h"
#include "match.h"
#include "native/kernel/kernel.h"
#include "tests/test_runtime_stats_stubs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool reference_walk;
bool cetta_kernel_dag_test_reference(void) { return reference_walk; }
static Atom *shared(Arena *a,Atom *head,Atom *leaf,unsigned depth) {
    while (depth--) leaf=atom_expr3(a,head,leaf,leaf);
    return leaf;
}
static double seconds(void) {
    struct timespec t;assert(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
int main(int argc,char **argv) {
    bool benchmark=argc==2 && !strcmp(argv[1],"--bench");
    SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;g_hashcons=NULL;
    VarInternTable variables;var_intern_init(&variables);g_var_intern=&variables;
    Arena a;arena_init(&a);char error[256];
    Atom *head=atom_symbol(&a,"dag-f"),*tag=atom_symbol(&a,"dag-p");
    Atom *x=atom_var_with_id(&a,"dag-x",var_id_from_base(UINT64_C(940001)));
    Atom *y=atom_var_with_id(&a,"dag-y",var_id_from_base(UINT64_C(940002)));
    Atom *u=atom_symbol(&a,"dag-u"),*v=atom_symbol(&a,"dag-v"),*w=atom_symbol(&a,"dag-w");
    assert(cetta_kernel_select(CETTA_KERNEL_X86,error,sizeof error));
    if (benchmark) {
        Atom *left=shared(&a,head,atom_expr3(&a,tag,x,y),18),*right=shared(&a,head,atom_expr3(&a,tag,u,v),18);
        double elapsed[4]={0};
        for (unsigned which=0;which<4;++which) {
            unsigned mode=which ? which-1 : 0;
            reference_walk=which==0;
            unsigned runs=which ? 1000 : 4;
            assert(cetta_kernel_select((CettaKernelMode)mode,error,sizeof error));
            double begin=seconds();
            for (unsigned n=0;n<runs;++n) {
                BindingsBuilder builder;assert(bindings_builder_init(&builder,NULL));
                assert(match_atoms_builder(left,right,&builder,&a));
                assert(bindings_lookup_value_id(&builder.current,x->var_id).skeleton==u);
                assert(bindings_lookup_value_id(&builder.current,y->var_id).skeleton==v);
                bindings_builder_free(&builder);
            }
            elapsed[which]=(seconds()-begin)/(double)runs;
        }
        printf("{\"family\":\"plain shared binary DAG depth 18\",\"seconds_per_match\":{\"c_reference\":%.9f,\"c\":%.9f,\"x86\":%.9f,\"hybrid\":%.9f}}\n",elapsed[0],elapsed[1],elapsed[2],elapsed[3]);
    } else {
        cetta_kernel_enable_stats();unsigned checks=0;
        for (unsigned specimen=0;specimen<9;++specimen) {
            Atom *left_leaf=atom_expr3(&a,tag,x,specimen==1 ? u : y);
            Atom *right_leaf=atom_expr3(&a,tag,u,specimen==1 ? w : v);
            if (specimen==2) right_leaf=atom_expr3(&a,tag,atom_int(&a,1),atom_int(&a,2));
            if (specimen==3) { left_leaf=atom_list(&a,&x,1u);right_leaf=atom_list(&a,&u,1u); }
            if (specimen==4) right_leaf=atom_expr3(&a,tag,y,x);
            if (specimen==5) right_leaf=atom_expr3(&a,tag,y,y);
            Atom *left=shared(&a,head,left_leaf,13),*right=shared(&a,head,right_leaf,13);
            BindingsBuilder reference;assert(bindings_builder_init(&reference,NULL));
            assert(cetta_kernel_select(CETTA_KERNEL_C,error,sizeof error));
            reference_walk=true;
            BindingValue left_value=binding_value_from_atom(left),right_value=binding_value_from_atom(right);
            if (specimen>=6) {
                CettaFrameIdentity lf={6001u},rf={6002u};
                if (specimen==7) right=left;
                left_value=binding_value_from_context(left,lf);
                right_value=binding_value_from_context(right,rf);
                if (specimen!=8) {
                    assert(bindings_builder_register_activation_source_frame(&reference,left,lf));
                    assert(bindings_builder_register_activation_source_frame(&reference,right,rf));
                }
            }
            Bindings seed;assert(bindings_clone(&seed,&reference.current));
            bool expected=match_binding_values_builder(left_value,right_value,&reference,&a);
            reference_walk=false;
            for (unsigned mode=0;mode<3;++mode) {
                BindingsBuilder actual;assert(bindings_builder_init(&actual,&seed));
                assert(cetta_kernel_select((CettaKernelMode)mode,error,sizeof error));
                uint32_t mark=bindings_builder_save(&actual);
                bool observed=match_binding_values_builder(left_value,right_value,&actual,&a);
                if (observed!=expected || !bindings_eq(&reference.current,&actual.current)) {
                    fprintf(stderr,"DAG private result differs: specimen=%u mode=%u C=%u actual=%u\n",specimen,mode,expected,observed);return 1;
                }
                bindings_builder_rollback(&actual,mark);assert(bindings_eq(&seed,&actual.current));
                bindings_builder_free(&actual);++checks;
            }
            bindings_free(&seed);bindings_builder_free(&reference);
        }
        CettaKernelStats stats=cetta_kernel_stats();assert(stats.x86_calls && stats.deferrals);
        printf("PASS: %u private full-binding DAG comparisons preserve aliases, refused prefixes, list/numeric deferral and caller rollback\n",checks);
    }
    arena_free(&a);bindings_thread_cache_free();g_var_intern=NULL;var_intern_free(&variables);g_symbols=NULL;symbol_table_free(&symbols);cetta_kernel_shutdown();
}

/* Optional scalar leaf lowering into the existing tcalc t-ISA implementation.
   This host tool emits a static assembly artifact; target code needs no loader. */
#define _POSIX_C_SOURCE 200809L
#include "cesta.h"
#include "digest.h"
#include "tisa/tisa.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct { uint8_t reg; TcIsaValType type; } Value;
typedef struct {
    TcIsaArena arena;
    TcIsaProgram program;
    const CeAtom *params;
    const TcIsaStmt *code;
    unsigned next;
} Lowering;

_Noreturn static void refuse(const char *message) {
    fprintf(stderr,"cesta-tisa: %s\n",message); exit(1);
}
static bool is(const CeAtom *a, const char *name) {
    return a && a->kind == CE_ATOM_SYMBOL && ce_is(a->text,name);
}
static bool form(const CeAtom *a, const char *name) {
    return a && a->kind == CE_ATOM_LIST && a->len && is(a->items[0],name);
}
static const CeAtom *part(const CeAtom *a, const char *name) {
    const CeAtom *found = NULL;
    for (size_t i = 1; i < a->len; ++i) if (form(a->items[i],name)) {
        if (found) refuse("duplicate source declaration");
        found = a->items[i];
    }
    if (!found) refuse("missing source declaration");
    return found;
}
static bool identifier(const char *s) {
    for (size_t i = 0; s[i]; ++i)
        if (!(s[i]=='_' || (s[i]>='a' && s[i]<='z') || (s[i]>='A' && s[i]<='Z') ||
              (i && s[i]>='0' && s[i]<='9'))) return false;
    return *s != 0;
}
static TcIsaValType type(const CeAtom *a) {
    if (is(a,"usize") || is(a,"u64")) return TISA_TYPE_U64;
    if (is(a,"u32")) return TISA_TYPE_U32Z;
    if (is(a,"bool")) return TISA_TYPE_BOOL;
    refuse("scalar leaf profile accepts bool, u32, u64 and 64-bit usize");
    return TISA_TYPE_UNINIT;
}
static Value instruction(Lowering *l, TcIsaInstr ins, TcIsaValType result) {
    if (l->next >= TISA_NUM_VREGS) refuse("scalar leaf exceeds the register capacity");
    ins.dst = (uint8_t)l->next++;
    l->code = tisa_stmt_seq(&l->arena,l->code,tisa_stmt_instr(&l->arena,ins));
    return (Value){ins.dst,result};
}
static Value expression(Lowering *l, const CeAtom *a) {
    if (a->kind == CE_ATOM_SYMBOL) {
        for (size_t i = 1; i < l->params->len; ++i) {
            const CeAtom *p = l->params->items[i];
            if (ce_atom_equal(p->items[0],a)) return (Value){(uint8_t)i,type(p->items[1])};
        }
        if (is(a,"true") || is(a,"false"))
            return instruction(l,(TcIsaInstr){.op=TISA_OP_IMM,.ty=TISA_TYPE_BOOL,
                .imm64=is(a,"true")},TISA_TYPE_BOOL);
        refuse("scalar leaf expression names an unknown parameter");
    }
    if (a->len == 2 && (form(a,"usize") || form(a,"u64") || form(a,"u32"))) {
        const CeAtom *n = a->items[1];
        if (n->kind != CE_ATOM_SYMBOL || !n->text.len) refuse("invalid unsigned literal");
        for (size_t i = 0; i < n->text.len; ++i)
            if (n->text.data[i]<'0' || n->text.data[i]>'9') refuse("invalid unsigned literal");
        errno=0; char *end; unsigned long long value=strtoull((const char *)n->text.data,&end,10);
        if (errno || *end || (form(a,"u32") && value>UINT32_MAX)) refuse("unsigned literal overflows");
        TcIsaValType ty=type(a->items[0]);
        return instruction(l,(TcIsaInstr){.op=TISA_OP_IMM,.ty=ty,.imm64=value},ty);
    }
    if (a->len == 3 && (form(a,"le") || form(a,"lt") || form(a,"eq") || form(a,"ne"))) {
        Value x=expression(l,a->items[1]), y=expression(l,a->items[2]);
        if (x.type != y.type) refuse("comparison operand types differ");
        unsigned op=form(a,"le") ? TISA_CMP_ULE : form(a,"lt") ? TISA_CMP_ULT :
                    form(a,"eq") ? TISA_CMP_EQ : TISA_CMP_NE;
        return instruction(l,(TcIsaInstr){.op=TISA_OP_CMP,.ty=x.type,.src1=x.reg,
            .src2=y.reg,.subop=(uint8_t)op},TISA_TYPE_BOOL);
    }
    refuse("expression is outside the optional unsigned-select leaf profile");
    return (Value){0,TISA_TYPE_UNINIT};
}
static Value returned(Lowering *l, const CeAtom *a) {
    if (!form(a,"return") || a->len != 2) refuse("leaf branch must return one pure expression");
    return expression(l,a->items[1]);
}
static void lower(Lowering *l, const CeAtom *function) {
    l->params=part(function,"params");
    if (l->params->len>6) refuse("scalar leaf accepts at most five parameters");
    l->program.init_reg_mask=1u;
    l->program.init_reg_types[0]=TISA_TYPE_U64;
    for (size_t i=1; i<l->params->len; ++i) {
        const CeAtom *p=l->params->items[i];
        if (p->kind!=CE_ATOM_LIST || p->len!=2 || p->items[0]->kind!=CE_ATOM_SYMBOL)
            refuse("scalar leaf parameter needs a name and unsigned scalar type");
        l->program.init_reg_mask |= (uint16_t)(1u<<i);
        l->program.init_reg_types[i]=type(p->items[1]);
    }
    if (part(function,"effects")->len!=1) refuse("scalar leaf must have no effects");
    const CeAtom *result=part(function,"returns"), *body=part(function,"body");
    if (result->len!=2 || body->len!=2) refuse("scalar leaf needs one return or pure conditional");
    Value value;
    const CeAtom *statement=body->items[1];
    if (form(statement,"return")) value=returned(l,statement);
    else if (form(statement,"if") && statement->len==4) {
        const CeAtom *yes=statement->items[2], *no=statement->items[3];
        if (!form(yes,"then") || yes->len!=2 || !form(no,"else") || no->len!=2)
            refuse("leaf conditional needs two pure returning arms");
        Value condition=expression(l,statement->items[1]);
        Value x=returned(l,yes->items[1]), y=returned(l,no->items[1]);
        if (condition.type!=TISA_TYPE_BOOL || x.type!=y.type) refuse("invalid leaf selection types");
        value=instruction(l,(TcIsaInstr){.op=TISA_OP_SELECT,.ty=x.type,
            .src1=x.reg,.src2=y.reg,.src3=condition.reg},x.type);
    } else { refuse("control is outside the optional unsigned-select leaf profile"); return; }
    if (value.type!=type(result->items[1])) refuse("leaf return type differs");
    l->program.root=tisa_stmt_seq(&l->arena,l->code,
        tisa_stmt_return(&l->arena,TISA_OUTCOME_FIRED,value.reg));
}
static bool check_source(const char *cesta, const char *source) {
    pid_t child=fork();
    if (child<0) return false;
    if (!child) { execlp(cesta,cesta,"checked","check",source,(char *)NULL); _exit(127); }
    int status;
    while (waitpid(child,&status,0)<0) if (errno!=EINTR) return false;
    return WIFEXITED(status) && WEXITSTATUS(status)==0;
}
int main(int argc, char **argv) {
    if (argc!=7) {
        fputs("usage: cesta-tisa CESTA SOURCE FUNCTION SYMBOL OUTPUT.S OUTPUT.json\n",stderr); return 2;
    }
    if (!identifier(argv[3]) || !identifier(argv[4])) refuse("function and symbol must be C identifiers");
    CeArena arena={0}; CeError error={0}; CeText source, checked; CeAtom *document;
    if (!ce_read_file(&arena,argv[2],&source,&error)) refuse(error.message);
    if (!check_source(argv[1],argv[2])) refuse("Cesta source admission failed");
    if (!ce_read_file(&arena,argv[2],&checked,&error)) refuse(error.message);
    if (!ce_equal(source,checked)) refuse("source changed during admission");
    if (!ce_atom_read(&arena,source,&document,&error))
        refuse(error.message);
    if (!form(document,"cesta") || document->len<3 || !is(document->items[1],"checked-c/1"))
        refuse("expected a checked-c/1 source document");
    const CeAtom *bits=part(document,"word-bits"), *function=NULL;
    if (bits->len!=2 || !is(bits->items[1],"64")) refuse("this target requires 64-bit words");
    for (size_t i=2; i<document->len; ++i) {
        const CeAtom *a=document->items[i];
        if (form(a,"fn") && a->len>1 && is(a->items[1],argv[3])) {
            if (function) refuse("duplicate selected function");
            function=a;
        }
    }
    if (!function) refuse("selected function was not found");
    Lowering l={.program={.name=argv[4],.prog_id=1},.next=6};
    tisa_arena_init(&l.arena,64); lower(&l,function);
    char message[256]={0}; TcIsaCompiledX86 compiled;
    if (!tisa_check_program(&l.program,message,sizeof(message)) ||
        !tisa_compile_x86(&l.program,TISA_PROFILE_P0,&compiled,message,sizeof(message))) refuse(message);
    FILE *assembly=fopen(argv[5],"w");
    if (!assembly) refuse("cannot write assembly");
    fprintf(assembly,".text\n.p2align 4\n.globl %s\n.type %s,@function\n%s:\n",argv[4],argv[4],argv[4]);
    for (size_t i=0; i<compiled.byte_len; ++i) {
        fputs(i%16 ? "," : "  .byte ",assembly); fprintf(assembly,"0x%02x",compiled.bytes[i]);
        if (i%16==15 || i+1==compiled.byte_len) fputc('\n',assembly);
    }
    fprintf(assembly,".size %s,.-%s\n.section .note.GNU-stack,\"\",@progbits\n",argv[4],argv[4]);
    if (fclose(assembly)) refuse("cannot finish assembly");
    CeDigest digest; char hash[65]; ce_digest_init(&digest);
    if (!ce_digest_update(&digest,source.data,source.len)) refuse("source digest overflow");
    ce_digest_finish(&digest,hash);
    FILE *manifest=fopen(argv[6],"w");
    if (!manifest) refuse("cannot write manifest");
    fprintf(manifest,"{\"format\":\"cesta-tisa-leaf/1\",\"profile\":\"unsigned-select/1\","
        "\"source_sha256\":\"%s\",\"function\":\"%s\",\"symbol\":\"%s\",\"program\":",hash,argv[3],argv[4]);
    bool ok=tisa_write_program_manifest_json(manifest,&l.program,&compiled);
    fputs("}\n",manifest);
    ok=fclose(manifest)==0 && ok;
    printf("cesta-tisa: %zu exact bytes, %u branches; existing fuel semantics retained\n",compiled.byte_len,compiled.branch_count);
    tisa_free_x86(&compiled); tisa_arena_free(&l.arena); ce_arena_free(&arena);
    if (!ok) refuse("cannot finish manifest");
    return 0;
}

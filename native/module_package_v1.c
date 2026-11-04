#define _XOPEN_SOURCE 700
#include "module_package_v1.h"
#include "module_value_internal_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "src/native_sha256.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Arena *arena;
    ModuleValuesV1 members;
    const char **active;
    size_t depth;
    const char *reason;
} Closure;

static bool text(Atom *a) {
    return a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING &&
        strlen(a->ground.sval) == atom_string_len(a);
}
static Atom *field(Arena *a, Atom *record, const char *key) {
    Atom *result = cetta_module_portable_field_v1(a, record, atom_string(a,key));
    return module_form_v1(result,"ModuleValueFoundV1",1u) ? result->expr.elems[1] : NULL;
}
static bool resolve(const char *origin, const char *reference, char out[PATH_MAX]) {
    if (!strncmp(reference,"file:",5)) reference += 5;
    if (!*reference || strchr(reference,':')) return false;
    char path[PATH_MAX];
    int n;
    if (*reference == '/') n = snprintf(path,sizeof(path),"%s",reference);
    else {
        const char *slash = strrchr(origin,'/');
        n = slash ? snprintf(path,sizeof(path),"%.*s/%s",(int)(slash-origin),origin,reference)
                  : snprintf(path,sizeof(path),"%s",reference);
    }
    return n >= 0 && (size_t)n < sizeof(path) && realpath(path,out) != NULL;
}
static Atom *find(Closure *c, const char *path) {
    for (size_t i=0;i<c->members.len;i++) {
        Atom *m = c->members.items[i];
        if (module_form_v1(m,"ModulePortableValueV1",2u) && text(m->expr.elems[1]) &&
            !strcmp(m->expr.elems[1]->ground.sval,path)) return m;
    }
    return NULL;
}
static bool commit_matches(Atom *commitment, const uint8_t *bytes, size_t length) {
    if (!module_form_v1(commitment,"Bytes",32u)) return false;
    char actual[65], expected[65];
    static const char hex[]="0123456789abcdef";
    for(size_t i=0;i<32;i++) {
        Atom *b=commitment->expr.elems[i+1];
        if(!b || b->kind!=ATOM_GROUNDED || b->ground.gkind!=GV_INT || b->ground.ival<0 || b->ground.ival>255) return false;
        expected[2*i]=hex[b->ground.ival/16]; expected[2*i+1]=hex[b->ground.ival%16];
    }
    expected[64]=0; cetta_native_sha256_hex(bytes,length,actual);
    return !strcmp(actual,expected);
}
static bool read_file(const char *path,uint8_t **bytes,size_t *length) {
    FILE *f=fopen(path,"rb"); if(!f)return false;
    bool ok=fseek(f,0,SEEK_END)==0;
    long n=ok?ftell(f):-1;
    ok=n>=0 && (unsigned long)n<SIZE_MAX && fseek(f,0,SEEK_SET)==0;
    uint8_t *data=ok?malloc((size_t)n+1u):NULL;
    ok=data && fread(data,1,(size_t)n,f)==(size_t)n && !ferror(f);
    fclose(f);
    if(!ok){free(data);return false;}
    data[n]=0; *bytes=data; *length=(size_t)n; return true;
}
static bool visit(Closure *c,Atom *module) {
    if(c->depth>=512u){c->reason="dependency depth exhausted";return false;}
    const char *origin=module->expr.elems[1]->ground.sval;
    for(size_t i=0;i<c->depth;i++)if(!strcmp(c->active[i],origin)){
        c->reason="cyclic module dependencies";return false;
    }
    if(find(c,origin))return true;
    Atom *dependencies=field(c->arena,module->expr.elems[2],"dependencies");
    if(!dependencies || dependencies->kind!=ATOM_EXPR || !dependencies->expr.len ||
        !atom_is_symbol(dependencies->expr.elems[0],"List")) {
        c->reason="module/1 envelope required";return false;
    }
    const char **active=realloc(c->active,(c->depth+1u)*sizeof(*active));
    if(!active){c->reason="module allocation failed";return false;}
    c->active=active; c->active[c->depth++]=origin;
    for(CettaExprIndex i=1;i<dependencies->expr.len;i++) {
        Atom *dep=dependencies->expr.elems[i];
        Atom *uri=field(c->arena,dep,"uri"),*commitment=field(c->arena,dep,"commitment");
        char path[PATH_MAX];
        if(!text(uri)||!resolve(origin,uri->ground.sval,path)){
            c->reason="unresolved module dependency";return false;
        }
        uint8_t *bytes=NULL;size_t length=0;
        if(!read_file(path,&bytes,&length)){c->reason="dependency read failed";return false;}
        if(!commit_matches(commitment,bytes,length)){
            free(bytes);c->reason="dependency file commitment mismatch";return false;
        }
        Atom *document=NULL,*loaded=NULL,*refusal=NULL;char error[256]={0};
        bool ok=fh_ground_term_v1_parse(c->arena,bytes,length,&document,error,sizeof(error))&&
            cetta_module_portable_v1_read(c->arena,atom_string(c->arena,path),document,
                1000000u,&loaded,&refusal);
        free(bytes);
        if(!ok){c->reason="invalid dependency module";return false;}
        for(size_t j=0;j<c->members.len;j++) {
            Atom *capture=c->members.items[j];
            if(module_form_v1(capture,"ModuleCaptureV1",2u)&&text(capture->expr.elems[1])&&
                !strcmp(capture->expr.elems[1]->ground.sval,path)&&
                !atom_eq(capture->expr.elems[2],commitment)) {
                c->reason="conflicting captures of one module file";return false;
            }
        }
        ModuleWorkV1 work={.arena=c->arena,.remaining=1000000u};
        Atom *capture_fields[]={atom_string(c->arena,path),commitment};
        Atom *link_fields[]={module->expr.elems[1],uri,capture_fields[0]};
        if(!module_push_v1(&work,&c->members,module_make_v1(c->arena,"ModuleCaptureV1",capture_fields,2u))||
           !module_push_v1(&work,&c->members,module_make_v1(c->arena,"ModuleLinkV1",link_fields,3u))){
            c->reason="module allocation failed";return false;
        }
        if(!visit(c,loaded))return false;
    }
    --c->depth;
    ModuleWorkV1 work={.arena=c->arena,.remaining=1000000u};
    if(!module_push_v1(&work,&c->members,module)){c->reason="module allocation failed";return false;}
    return true;
}

Atom *cetta_module_package_close_v1(Arena *arena,Atom *module) {
    Closure c={.arena=arena}; Atom *checked=NULL,*refusal=NULL;
    if(!module_form_v1(module,"ModulePortableValueV1",2u)||!text(module->expr.elems[1]))
        return atom_symbol(arena,"ModuleValueMalformedV1");
    char canonical[PATH_MAX];
    Atom *origin=module->expr.elems[1];
    if(realpath(origin->ground.sval,canonical))origin=atom_string(arena,canonical);
    Atom *data[]={module->expr.elems[2]};
    Atom *document=module_make_v1(arena,"module:document",data,1u);
    bool ok=cetta_module_portable_v1_read(arena,origin,document,1000000u,&checked,&refusal)&&visit(&c,checked);
    free(c.active);
    Atom *result;
    if(ok) {
        Atom *fields[]={checked,module_make_v1(arena,"ModuleMembersV1",c.members.items,c.members.len)};
        result=module_make_v1(arena,"ModuleClosureV1",fields,2u);
    }else {
        Atom *fields[]={module,atom_string(arena,c.reason?c.reason:"invalid portable module")};
        result=module_make_v1(arena,"ModuleClosureUnresolvedV1",fields,2u);
    }
    module_clear_v1(&c.members);return result;
}

/* A reference spec is exactly {dependency: URI, export: NAME}; all other
 * export specifications remain opaque. No implicit flattening or shadowing. */
static Atom *lookup(Arena *a,Atom *members,Atom *module,Atom *name,size_t depth) {
    if(depth>=512u)return atom_symbol(a,"ModuleValueMalformedV1");
    Atom *found=cetta_module_portable_export_v1(a,module,name);
    if(!module_form_v1(found,"ModuleValueFoundV1",1u))return found;
    Atom *spec=found->expr.elems[1];
    Atom *uri=field(a,spec,"dependency"),*export=field(a,spec,"export");
    if(uri && export) {
        if(spec->expr.len!=3u||!text(uri)||!text(export))return atom_symbol(a,"ModuleValueMalformedV1");
        Atom *dependencies=field(a,module->expr.elems[2],"dependencies");bool declared=false;
        if(dependencies && dependencies->kind==ATOM_EXPR)
            for(CettaExprIndex i=1;i<dependencies->expr.len;i++)
                if(atom_eq(uri,field(a,dependencies->expr.elems[i],"uri")))declared=true;
        Atom *path=NULL;
        if(declared)for(CettaExprIndex i=1;i<members->expr.len;i++) {
            Atom *link=members->expr.elems[i];
            if(module_form_v1(link,"ModuleLinkV1",3u)&&
                atom_eq(link->expr.elems[1],module->expr.elems[1])&&atom_eq(link->expr.elems[2],uri))
                path=link->expr.elems[3];
        }
        if(!text(path))return atom_symbol(a,"ModuleValueAbsentV1");
        for(CettaExprIndex i=1;i<members->expr.len;i++) {
            Atom *m=members->expr.elems[i];
            if(module_form_v1(m,"ModulePortableValueV1",2u)&&text(m->expr.elems[1])&&
                atom_eq(m->expr.elems[1],path))return lookup(a,members,m,export,depth+1u);
        }
        return atom_symbol(a,"ModuleValueAbsentV1");
    }
    Atom *fields[]={module->expr.elems[1],spec};
    return module_make_v1(a,"ModuleExportV1",fields,2u);
}
Atom *cetta_module_package_export_v1(Arena *a,Atom *closure,Atom *name) {
    if(!module_form_v1(closure,"ModuleClosureV1",2u)||!text(name)||
        !module_form_v1(closure->expr.elems[1],"ModulePortableValueV1",2u)||
        !closure->expr.elems[2]||closure->expr.elems[2]->kind!=ATOM_EXPR||
        !closure->expr.elems[2]->expr.len||!atom_is_symbol(closure->expr.elems[2]->expr.elems[0],"ModuleMembersV1"))
        return atom_symbol(a,"ModuleValueMalformedV1");
    return lookup(a,closure->expr.elems[2],closure->expr.elems[1],name,0);
}

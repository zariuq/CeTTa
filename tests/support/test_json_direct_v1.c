#define _POSIX_C_SOURCE 200809L
#include "native/json_direct_v1.h"
#include "native/json_value_v1.h"
#include "symbol.h"
#include "library_json.h"
#include "lang.h"
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static unsigned passed, failed;
static unsigned reference_limits;
static CettaLanguageId test_language = CETTA_LANGUAGE_HE;
CettaLanguageId eval_current_language_id(void) { return test_language; }
#define CHECK(c) do { if (c) passed++; else { failed++; fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); } } while (0)

static uint8_t *read_bytes(const char *path, size_t *len) {
    FILE *file=fopen(path,"rb");
    if (!file) return NULL;
    if (fseek(file,0,SEEK_END)) { fclose(file); return NULL; }
    long n=ftell(file);
    if (n<0 || fseek(file,0,SEEK_SET)) { fclose(file); return NULL; }
    uint8_t *bytes=malloc((size_t)n+1);
    if (!bytes || fread(bytes,1,(size_t)n,file)!=(size_t)n) { free(bytes); fclose(file); return NULL; }
    fclose(file); *len=(size_t)n; bytes[n]=0; return bytes;
}

static CettaJsonRuntimeV1 *reference(void) {
    size_t a=0,b=0,c=0;
    uint8_t *syntax=read_bytes("langdef/json/rfc8259_syntax_v1.metta",&a);
    uint8_t *profile=read_bytes("langdef/json/rfc8259_parser_profile_v1.metta",&b);
    uint8_t *target=read_bytes("langdef/json/occurrence_preserving_value_v1.metta",&c);
    char error[512]={0};
    CettaJsonRuntimeV1 *runtime=cetta_json_runtime_v1_new(syntax,a,profile,b,target,c,error,sizeof(error));
    free(syntax); free(profile); free(target);
    if (!runtime) fprintf(stderr,"reference preparation: %s\n",error);
    return runtime;
}

static bool agree(const CettaJsonRuntimeV1 *runtime, const uint8_t *bytes,size_t n) {
    Arena arena; arena_init(&arena);
    Atom *fast=NULL,*slow=NULL,*legacy=NULL,*converted=NULL;
    CettaJsonRuntimeV1Limits limits;
    CettaJsonRuntimeV1Status a,b;
    CettaJsonValueV1Status codec;
    char error[512]={0};
    cetta_json_runtime_v1_default_limits(&limits);
    bool x=cetta_json_direct_v1_parse(&arena,bytes,n,false,false,0,&fast,&a,error,sizeof(error));
    bool y=cetta_json_runtime_v1_parse(runtime,&arena,bytes,n,&limits,&slow,&b,error,sizeof(error));
    bool ok=x==y && (!x || atom_eq(fast,slow));
    if (x && y) {
        ok=ok && cetta_json_direct_v1_parse(&arena,bytes,n,true,false,0,&legacy,&a,error,sizeof(error)) &&
            cetta_json_value_v1_to_legacy(&arena,slow,4000000,1024,&converted,&codec,error,sizeof(error)) &&
            atom_eq(legacy,converted);
    }
    if (!ok && x && !y && b==CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT) {
        /* A reference resource cap does not refute a grammar-derived value.
         * Report it separately; ledger qualification still requires equality. */
        reference_limits++;
    }
    if (!ok) fprintf(stderr,"agreement failed: bytes=%zu direct=%d reference=%d reference-status=%u %s\n",n,x,y,b,error);
    arena_free(&arena); return ok;
}

typedef struct { Arena kept; Atom *copy; unsigned rows; size_t last; } Consumer;
static bool consume(void *context, Atom *value,size_t line) {
    Consumer *c=context;
    c->rows++; c->last=line; c->copy=atom_deep_copy(&c->kept,value); return c->copy!=NULL;
}

static void *concurrent(void *context) {
    unsigned *errors=context;
    const char *input="{\"x\":[1,true,\"\\u0000λ\"],\"x\":2}";
    for (unsigned i=0;i<100;i++) {
        Arena a; arena_init_detached(&a); Atom *value=NULL;
        if (!cetta_json_direct_v1_parse(&a,(const uint8_t *)input,strlen(input),true,true,32,&value,NULL,NULL,0)) (*errors)++;
        arena_free(&a);
    }
    return NULL;
}

static void deep_values(void) {
    const size_t depth = 100000u;
    for (unsigned object = 0; object < 2; object++) {
        size_t prefix = object ? 5u : 1u;
        size_t size = (prefix + 1u) * depth + 4u;
        uint8_t *text = malloc(size);
        CHECK(text != NULL);
        if (!text) return;
        for (size_t i = 0; i < depth; i++) memcpy(text + prefix*i, object ? "{\"x\":" : "[", prefix);
        memcpy(text + prefix*depth, "true", 4u);
        memset(text + prefix*depth + 4u, object ? '}' : ']', depth);
        for (unsigned legacy = 0; legacy < 2; legacy++) {
            for (unsigned petta = 0; petta < 2; petta++) {
                Arena arena; arena_init(&arena);
                Atom *value = NULL;
                CettaJsonRuntimeV1Status status;
                char error[256];
                bool ok = cetta_json_direct_v1_parse(&arena, text, size, legacy, petta,
                                                    0u, &value, &status, error, sizeof(error));
                CHECK(ok);
                Atom *leaf = value;
                for (size_t i = 0; ok && i < depth; i++) {
                    ok = leaf && leaf->kind == ATOM_EXPR && leaf->expr.len == 2u;
                    Atom *items = ok ? leaf->expr.elems[1] : NULL;
                    ok = ok && items && items->kind == ATOM_EXPR && items->expr.len == 1u;
                    if (ok) leaf = object ? items->expr.elems[0]->expr.elems[legacy ? 2 : 3] : items->expr.elems[0];
                }
                CHECK(ok && leaf && leaf->kind == ATOM_EXPR && leaf->expr.len == 2u &&
                    (petta ? atom_is_symbol(leaf->expr.elems[1], "true") :
                     leaf->expr.elems[1]->kind == ATOM_GROUNDED && leaf->expr.elems[1]->ground.gkind == GV_BOOL &&
                     leaf->expr.elems[1]->ground.bval));
                uint8_t *encoded = NULL; size_t encoded_size = 0;
                CettaJsonValueV1Status codec;
                CHECK(ok && cetta_json_value_v1_emit(value, legacy, 0u, 0u, 0u, &encoded,
                    &encoded_size, &codec, error, sizeof(error)) && encoded_size == size && !memcmp(encoded, text, size));
                free(encoded);
                CettaJsonLibraryRuntimeV1 *library = cetta_json_library_runtime_v1_new(error, sizeof(error));
                CHECK(library != NULL);
                test_language = petta ? CETTA_LANGUAGE_PETTA : CETTA_LANGUAGE_HE;
                Atom *source[] = {atom_string_n(&arena, (const char *)text, size)};
                Atom *parsed = library ? cetta_json_library_dispatch_v1(library, &arena,
                    atom_symbol(&arena, legacy ? "__cetta_lib_json_parse" : "__cetta_lib_json_parse_v1"), source, 1u) : NULL;
                CHECK(parsed && parsed->kind == ATOM_EXPR && atom_is_symbol(parsed->expr.elems[0],
                    object ? (legacy ? "JsonObject" : "JsonObjectV1") : (legacy ? "JsonArray" : "JsonArrayV1")));
                if (parsed) value = parsed;
                Atom *argument[] = {value};
                Atom *written = library ? cetta_json_library_dispatch_v1(library, &arena,
                    atom_symbol(&arena, legacy ? "__cetta_lib_json_stringify" : "__cetta_lib_json_stringify_v1"),
                    argument, 1u) : NULL;
                CHECK(written && written->kind == ATOM_GROUNDED && written->ground.gkind == GV_STRING &&
                    atom_string_len(written) == size && !memcmp(written->ground.sval, text, size));
                if (!legacy && library) {
                    Atom *projected = cetta_json_library_dispatch_v1(library, &arena,
                        atom_symbol(&arena, "__cetta_lib_json_to_legacy_v1"), argument, 1u);
                    argument[0] = projected;
                    written = cetta_json_library_dispatch_v1(library, &arena,
                        atom_symbol(&arena, "__cetta_lib_json_stringify"), argument, 1u);
                    CHECK(written && written->kind == ATOM_GROUNDED && written->ground.gkind == GV_STRING &&
                    atom_string_len(written) == size && !memcmp(written->ground.sval, text, size));
                    Atom *last = projected;
                    bool shape = true;
                    for (size_t i = 0; shape && i < depth; i++) {
                        shape = last && last->kind == ATOM_EXPR && last->expr.len == 2u &&
                            last->expr.elems[1] && last->expr.elems[1]->kind == ATOM_EXPR &&
                            last->expr.elems[1]->expr.len == 1u;
                        if (shape) last = object ? last->expr.elems[1]->expr.elems[0]->expr.elems[2] : last->expr.elems[1]->expr.elems[0];
                    }
                    CHECK(shape && last && last->kind == ATOM_EXPR &&
                        (petta ? atom_is_symbol(last->expr.elems[1], "true") :
                         last->expr.elems[1]->kind == ATOM_GROUNDED && last->expr.elems[1]->ground.gkind == GV_BOOL &&
                         last->expr.elems[1]->ground.bval));
                }
                cetta_json_library_runtime_v1_free(library);
                Atom *converted = NULL;
                bool converted_ok = legacy
                    ? cetta_json_value_v1_from_legacy(&arena, value, 0u, 0u, &converted, &codec, error, sizeof(error))
                    : cetta_json_value_v1_to_legacy(&arena, value, 0u, 0u, &converted, &codec, error, sizeof(error));
                CHECK(ok && converted_ok);
                encoded = NULL; encoded_size = 0;
                CHECK(converted_ok && cetta_json_value_v1_emit(converted, !legacy, 0u, 0u, 0u, &encoded,
                    &encoded_size, &codec, error, sizeof(error)) && encoded_size == size && !memcmp(encoded, text, size));
                free(encoded);
                ArenaMark mark = arena_mark(&arena);
                Atom *sentinel = value;
                CHECK(!cetta_json_direct_v1_parse(&arena, text, size-1u, legacy, petta, 0u,
                    &value, &status, error, sizeof(error)) && value == sentinel && arena.live_bytes == mark.live_bytes);
                arena_free(&arena);
            }
        }
        free(text);
    }
}

static void unit(const CettaJsonRuntimeV1 *runtime) {
    const char *cases[]={"null","false","true","0","-0","-12.50e+2"," \t\r\n[  ] ","{\"x\":1,\"x\":2}",
        "{\"λ\": \"λ\\u0000z\", \"x\":[{ },[ ]]}","\"\\uD834\\uDD1E\"","\"a\\tb\\nc\\rd\\fe\\bf\\/g\\\\h\\\"\"",
        "{ }","{  }","[  ]","[1,]","01","-","1e","[","{\"x\":}","\"\\uD800\"","\"\\uDC00\"","\"\\x\"",
        "true false","\"\n\"","\"unterminated","", " ","[0.01,1E-10,12345678901234567890]"};
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);i++) CHECK(agree(runtime,(const uint8_t *)cases[i],strlen(cases[i])));
    const uint8_t invalid[][6]={{34,0,34},{34,0xc0,0x80,34},{34,0xed,0xa0,0x80,34},{34,0xf4,0x90,0x80,0x80,34}};
    const size_t lengths[]={3,4,5,6};
    for(unsigned i=0;i<4;i++) CHECK(agree(runtime,invalid[i],lengths[i]));
    Arena arena; arena_init(&arena);
    Atom *sentinel=atom_string(&arena,"keep"), *value=sentinel;
    ArenaMark mark=arena_mark(&arena);
    char error[256]; CettaJsonRuntimeV1Status status;
    const char *bad="{\"x\":[1,2,3,]}";
    CHECK(!cetta_json_direct_v1_parse(&arena,(const uint8_t *)bad,strlen(bad),true,false,32,&value,&status,error,sizeof(error)));
    CHECK(value==sentinel && arena.live_bytes==mark.live_bytes && atom_string_equals_cstr(sentinel,"keep"));
    CHECK(strstr(error,"JSON byte ") && strstr(error,"expected "));
    const char *nested="[[[[0]]]]";
    CHECK(!cetta_json_direct_v1_parse(&arena,(const uint8_t *)nested,strlen(nested),true,false,4,&value,&status,error,sizeof(error)) && status==CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT);
    CHECK(cetta_json_direct_v1_parse(&arena,(const uint8_t *)nested,strlen(nested),true,false,5,&value,&status,error,sizeof(error)));
    Atom *canonical = NULL, *limited = sentinel;
    CettaJsonValueV1Status value_status;
    CHECK(cetta_json_direct_v1_parse(&arena,(const uint8_t *)nested,strlen(nested),false,false,0,&canonical,&status,error,sizeof(error)));
    ArenaMark before_conversion = arena_mark(&arena);
    CHECK(!cetta_json_value_v1_to_legacy(&arena, canonical, 2u, 0u, &limited, &value_status,
        error, sizeof(error)) && value_status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT && limited == sentinel &&
        arena.live_bytes == before_conversion.live_bytes);
    CHECK(!cetta_json_value_v1_to_legacy(&arena, canonical, 0u, 4u, &limited, &value_status,
        error, sizeof(error)) && value_status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT && limited == sentinel &&
        arena.live_bytes == before_conversion.live_bytes);
    uint8_t *limited_text = NULL; size_t limited_size = 17u;
    CHECK(!cetta_json_value_v1_emit(canonical, false, 0u, 4u, 0u, &limited_text, &limited_size,
        &value_status, error, sizeof(error)) && value_status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT &&
        !limited_text && limited_size == 17u);
    CHECK(!cetta_json_value_v1_emit(canonical, false, 0u, 0u, 2u, &limited_text, &limited_size,
        &value_status, error, sizeof(error)) && value_status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT &&
        !limited_text && limited_size == 17u);
    const char *numbers[]={"0","-1","0.1","1e-1","01","1.","NaN","1e+",""," 0"};
    for(unsigned i=0;i<10;i++) CHECK(cetta_json_direct_v1_number_valid((const uint8_t *)numbers[i],strlen(numbers[i]))==(i<4));
    char *wide=malloc(24010); size_t n=0;
    wide[n++]='[';
    for(unsigned i=0;i<6000;i++) { if(i) wide[n++]=','; wide[n++]='0'; }
    wide[n++]=']';
    CHECK(cetta_json_direct_v1_parse(&arena,(const uint8_t *)wide,n,true,false,2,&value,&status,error,sizeof(error)) && value->expr.elems[1]->expr.len==6000);
    free(wide); arena_free(&arena);
    arena_init(&arena);
    CHECK(cetta_json_direct_v1_parse(&arena, (const uint8_t *)cases[7], strlen(cases[7]),
                                    false, false, 0u, &value, &status, error, sizeof(error)));
    uint8_t *reference_text = NULL; size_t reference_size = 0;
    CettaJsonValueV1Status reference_status;
    CHECK(cetta_json_value_v1_stringify(runtime, value, false, 0u, 0u, 0u,
        &reference_text, &reference_size, &reference_status, error, sizeof(error)) &&
        reference_size == strlen(cases[7]) && !memcmp(reference_text, cases[7], reference_size));
    free(reference_text); arena_free(&arena);
    Consumer c={0}; arena_init(&c.kept);
    FILE *stream=tmpfile(); size_t failed_line=0;
    CHECK(stream!=NULL);
    if(stream) {
        fputs("1\n\n{\"x\":\"\\u0000\"}\n[1,]\n2\n",stream); rewind(stream);
        CHECK(!cetta_json_direct_v1_lines(stream,true,false,32,consume,&c,&failed_line,error,sizeof(error)));
        CHECK(c.rows==2 && c.last==3 && failed_line==4 && c.copy!=NULL);
        uint8_t *encoded=NULL; size_t len=0; CettaJsonValueV1Status s;
        CHECK(cetta_json_value_v1_emit(c.copy,true,1000,32,1000,&encoded,&len,&s,error,sizeof(error)) &&
              len==strlen("{\"x\":\"\\u0000\"}") && !memcmp(encoded,"{\"x\":\"\\u0000\"}",len));
        free(encoded); fclose(stream);
    }
    arena_free(&c.kept);
    pthread_t threads[4]; unsigned errors[4]={0};
    for(unsigned i=0;i<4;i++) CHECK(!pthread_create(threads+i,NULL,concurrent,errors+i));
    for(unsigned i=0;i<4;i++) { CHECK(!pthread_join(threads[i],NULL)); CHECK(errors[i]==0); }
    /* Deterministic short input mutations, including every truncation. */
    const char *sample="{\"λ\":[0,\"\\uD834\\uDD1E\",false],\"x\":null}";
    n=strlen(sample);
    for(size_t i=0;i<n;i++) CHECK(agree(runtime,(const uint8_t *)sample,i));
    uint32_t random=17419; uint8_t mutated[80];
    for(unsigned i=0;i<200;i++) {
        memcpy(mutated,sample,n); random=random*1664525u+1013904223u;
        mutated[random%n]=(uint8_t)(random>>24);
        CHECK(agree(runtime,mutated,n));
    }
    deep_values();
}

static bool corpus(const CettaJsonRuntimeV1 *runtime,const char *directory,bool use_reference) {
    DIR *dir=opendir(directory); if(!dir) return false;
    unsigned yes=0,no=0,policy=0,bad=0;
    struct dirent *entry;
    while((entry=readdir(dir))) {
        const char *name=entry->d_name;
        if(strlen(name)<3 || name[1]!='_' || (name[0]!='y' && name[0]!='n' && name[0]!='i')) continue;
        char path[4096]; snprintf(path,sizeof(path),"%s/%s",directory,name);
        size_t n=0; uint8_t *bytes=read_bytes(path,&n);
        if(!bytes) { bad++; continue; }
        Arena arena; arena_init(&arena); Atom *value=NULL;
        bool ok=cetta_json_direct_v1_parse(&arena,bytes,n,false,false,0,&value,NULL,NULL,0);
        if(name[0]=='y') { yes++; if(!ok) { bad++; fprintf(stderr,"valid rejected: %s\n",name); } }
        else if(name[0]=='n') { no++; if(ok) { bad++; fprintf(stderr,"invalid accepted: %s\n",name); } }
        else policy++;
        unsigned before=reference_limits;
        if(use_reference && !agree(runtime,bytes,n) && reference_limits==before) { bad++; fprintf(stderr,"corpus disagreement: %s\n",name); }
        arena_free(&arena); free(bytes);
    }
    closedir(dir); printf("{\"valid\":%u,\"invalid\":%u,\"policy\":%u,\"failed\":%u,\"reference_limits\":%u}\n",yes,no,policy,bad,reference_limits); return !bad;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static bool lines(const CettaJsonRuntimeV1 *runtime,const char *path,bool compare) {
    FILE *stream=fopen(path,"rb"); if(!stream) return false;
    char *line=NULL; size_t capacity=0,rows=0,bytes=0,peak=0; ssize_t n;
    double start=now(); bool ok=true;
    while((n=getline(&line,&capacity,stream))>=0) {
        bytes+=(size_t)n; rows++;
        if(compare) {
            if(!agree(runtime,(const uint8_t *)line,(size_t)n)) { fprintf(stderr,"row %zu\n",rows); ok=false; break; }
            if(rows%1000==0) { fprintf(stderr,"checked %zu rows\n",rows); fflush(stderr); }
        } else {
            Arena arena; arena_init(&arena); Atom *value=NULL; char error[256];
            bool accepted=cetta_json_direct_v1_parse(&arena,(const uint8_t *)line,(size_t)n,true,false,0,&value,NULL,error,sizeof(error));
            if(arena.live_bytes>peak) peak=arena.live_bytes;
            if(!accepted) { fprintf(stderr,"row %zu: %s\n",rows,error); ok=false; }
            arena_free(&arena); if(!accepted) break;
        }
    }
    if(ferror(stream)) ok=false;
    struct rusage usage; getrusage(RUSAGE_SELF,&usage);
    long peak_rss=usage.ru_maxrss;
    /* Linux getrusage may include the spawning process's pre-exec peak.
     * VmHWM counts this executable; other systems keep getrusage. */
    FILE *status=fopen("/proc/self/status","r");
    if(status) {
        char entry[256];
        while(fgets(entry,sizeof(entry),status))
            if(sscanf(entry,"VmHWM: %ld kB",&peak_rss)==1) break;
        fclose(status);
    }
    printf("{\"rows\":%zu,\"bytes\":%zu,\"seconds\":%.6f,\"peak_rss_kib\":%ld,\"peak_value_bytes\":%zu,\"agreement\":%s}\n",rows,bytes,now()-start,peak_rss,peak,compare?"true":"false");
    free(line); fclose(stream); return ok;
}

int main(int argc,char **argv) {
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols=&symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    bool fast_only=argc==3 && (!strcmp(argv[1],"--bench") || !strcmp(argv[1],"--corpus-fast"));
    CettaJsonRuntimeV1 *runtime=fast_only?NULL:reference();
    bool ok=fast_only || runtime;
    if(ok && argc==1) { unit(runtime); printf("JsonDirectSummary %u %u\n",passed,failed); ok=!failed; }
    else if(ok && argc==3 && !strncmp(argv[1],"--corpus",8)) ok=corpus(runtime,argv[2],!fast_only);
    else if(ok && argc==3) ok=lines(runtime,argv[2],!fast_only);
    else ok=false;
    cetta_json_runtime_v1_free(runtime); g_symbols=NULL; symbol_table_free(&symbols); return ok?0:1;
}

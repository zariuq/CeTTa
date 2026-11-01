#define _POSIX_C_SOURCE 200809L
#include "cesta.h"
#include "pp.h"
#include "native.h"
#include "diagnose.h"
#include "ownership.h"
#include "sema.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

CeText ce_builtin_C11Grammar(void);
CeText ce_builtin_CGNUGrammar(void);
CeText ce_builtin_C11Profile(void);
CeText ce_builtin_CGNUProfile(void);
CeText ce_builtin_CGNUC11Profile(void);
CeText ce_builtin_CClangProfile(void);
CeText ce_builtin_CClangC11Profile(void);
CeText ce_builtin_TheoryModuleSyntax(void);

static bool suffix(const char *text, const char *end) {
    size_t a = strlen(text), b = strlen(end);
    return a >= b && !strcmp(text+a-b,end);
}
static bool option_value(const char *option) {
    const char *names[] = {"-o","-I","-L","-l","-D","-U","-MF","-MT","-MQ","-x","-include",
        "-imacros","-isystem","-iquote","-idirafter","-iprefix","-iwithprefix","-iwithprefixbefore",
        "-isysroot","--sysroot","-target","--target","-Xclang","-Xpreprocessor","-Xassembler","-Xlinker","-B",
        "-dumpbase","-dumpbase-ext","-dumpdir"};
    for (size_t i = 0; i < sizeof(names)/sizeof(*names); ++i) if (!strcmp(option,names[i])) return true;
    return false;
}

typedef struct ResponseFrame {
    dev_t device; ino_t inode;
    const struct ResponseFrame *parent;
} ResponseFrame;
typedef struct {
    CeArena *arena; char **values; size_t len, capacity;
    CeText *files; size_t nfiles, file_capacity; bool collect_files;
} ResponseArgs;
static void response_file(ResponseArgs *out, const char *path) {
    if (!out->collect_files) return;
    for (size_t i = 0; i < out->nfiles; ++i)
        if (ce_same_file((const char *)out->files[i].data,path)) return;
    if (out->nfiles == out->file_capacity) {
        size_t capacity = out->file_capacity ? out->file_capacity*2 : 8;
        CeText *files = ce_alloc(out->arena,capacity*sizeof(*files));
        if (out->nfiles) memcpy(files,out->files,out->nfiles*sizeof(*files));
        out->files = files; out->file_capacity = capacity;
    }
    out->files[out->nfiles++] = ce_cstr(out->arena,path);
}
static void response_push(ResponseArgs *out, char *value) {
    if (out->len == out->capacity) {
        out->capacity = out->capacity ? out->capacity*2 : 32;
        out->values = ce_resize(out->values,out->capacity,sizeof(*out->values));
    }
    out->values[out->len++] = value;
}
static bool response_white(unsigned c) { return c == ' ' || (c >= 9 && c <= 13); }
/* GCC/Clang response-file words: whitespace, quotes, backslash escapes, and
   recursive @files. Ordinary C-only invocations still receive their original
   argv; expansion is used only when a compact input needs translation. */
static bool response_expand(ResponseArgs *out, char *value, const ResponseFrame *parent, CeError *error) {
    if (value[0] == '@' && value[1]) response_file(out,value+1);
    struct stat info;
    if (value[0] != '@' || !value[1] || stat(value+1,&info) || !S_ISREG(info.st_mode)) {
        response_push(out,value); return true;
    }
    for (const ResponseFrame *p = parent; p; p = p->parent)
        if (p->device == info.st_dev && p->inode == info.st_ino)
            return ce_error(error,0,"recursive compiler response file: %s",value+1);
    CeText text; CeError unreadable = {{0},0};
    if (!ce_read_file(out->arena,value+1,&text,&unreadable)) { response_push(out,value); return true; }
    ResponseFrame frame = {info.st_dev,info.st_ino,parent}; size_t pos = 0;
    char *buffer = ce_resize(NULL,text.len+1,1);
    while (pos < text.len) {
        while (pos < text.len && response_white(text.data[pos])) ++pos;
        if (pos == text.len) break;
        unsigned char quote = 0; size_t length = 0;
        char *word = buffer;
        while (pos < text.len) {
            unsigned char c = text.data[pos];
            if (!quote && response_white(c)) break;
            ++pos;
            if (c == '\\' && pos < text.len) { word[length++] = (char)text.data[pos++]; continue; }
            if (c == quote) { quote = 0; continue; }
            if (!quote && (c == '\'' || c == '"')) { quote = c; continue; }
            word[length++] = (char)c;
        }
        word[length] = 0;
        CeText stored = ce_text(out->arena,word,length);
        if (!response_expand(out,(char *)stored.data,&frame,error)) { free(buffer); return false; }
    }
    free(buffer);
    return true;
}

bool ce_native_arguments(CeArena *arena, size_t argc, char **argv,
                         CeNativeArguments *out, CeError *error) {
    *out = (CeNativeArguments){0};
    ResponseArgs expanded = {.arena=arena,.collect_files=true};
    bool ok = true;
    for (size_t i = 0; ok && i < argc; ++i) ok = response_expand(&expanded,argv[i],NULL,error);
    if (ok) {
        out->words = ce_alloc(arena,(expanded.len+1)*sizeof(*out->words));
        if (expanded.len) memcpy(out->words,expanded.values,expanded.len*sizeof(*out->words));
        out->nwords = expanded.len; out->files = expanded.files; out->nfiles = expanded.nfiles;
    }
    free(expanded.values);
    return ok;
}

static bool compact_arguments(size_t n, char **args) {
    bool after_options = false;
    for (size_t i = 0; i < n; ++i) {
        const char *a = args[i];
        if (!after_options && (!strcmp(a,"--cc") || !strcmp(a,"--presentation") || option_value(a))) { ++i; continue; }
        if (!after_options && !strcmp(a,"--")) { after_options = true; continue; }
        if ((after_options || a[0] != '-') && (suffix(a,".cesta") || suffix(a,".csexp"))) return true;
    }
    return false;
}
static bool directories(char *path, CeError *error) {
    for (char *p = path+1;; ++p) if (*p == '/' || !*p) {
        char saved = *p; *p = 0;
        int result = mkdir(path,0777); *p = saved;
        if (result && errno != EEXIST) return ce_error(error,0,"cannot create generated-source directory: %s",strerror(errno));
        if (!saved) break;
    }
    return true;
}
/* GCC's and Clang's GNU dialects differ lexically: Clang reads the
   interchange floating type names as identifiers. */
bool ce_builtin_dialect(CeGrammar *g, bool gnu, bool clang, CeError *error) {
    if (!ce_grammar_read(g,ce_builtin_CGNUGrammar(),error) || !ce_grammar_finish(g,error)) return false;
    CeProfileSource profiles[] = {
        {ce_cstr(&g->arena,"C11.profile"),ce_builtin_C11Profile()},
        {ce_cstr(&g->arena,"CGNU.profile"),ce_builtin_CGNUProfile()},
        {ce_cstr(&g->arena,"CGNUC11.profile"),ce_builtin_CGNUC11Profile()},
        {ce_cstr(&g->arena,"CClang.profile"),ce_builtin_CClangProfile()},
        {ce_cstr(&g->arena,"CClangC11.profile"),ce_builtin_CClangC11Profile()}
    };
    return ce_profile_bundle(g,profiles,5,profiles[clang ? (gnu ? 3 : 4) : (gnu ? 1 : 2)].name,error);
}
bool ce_builtin_language(CeGrammar *g, bool gnu, CeError *error) {
    return ce_builtin_dialect(g,gnu,false,error);
}

static CeText dependency_word(CeArena *a, const char *path) {
    size_t n = strlen(path); unsigned char *bytes = ce_alloc(a,n*2+1); size_t out = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)path[i];
        if (c == '$') bytes[out++] = '$';
        else if (c == ' ' || c == '\t' || c == '#' || c == '\\') bytes[out++] = '\\';
        bytes[out++] = c;
    }
    return (CeText){bytes,out};
}
static bool dependency_boundary(unsigned c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static bool dependency_write(CeArena *a, CeText source, FILE *file, char **generated,
                              char **original, size_t len, CeError *error) {
    CeText *before = ce_alloc(a,len*sizeof(*before)), *after = ce_alloc(a,len*sizeof(*after));
    for (size_t i = 0; i < len; ++i) {
        before[i] = dependency_word(a,generated[i]); after[i] = dependency_word(a,original[i]);
    }
    for (size_t pos = 0; pos < source.len;) {
        size_t found = len;
        if (!pos || dependency_boundary(source.data[pos-1])) for (size_t i = 0; i < len; ++i)
            if (before[i].len <= source.len-pos && !memcmp(source.data+pos,before[i].data,before[i].len) &&
                (pos+before[i].len == source.len || dependency_boundary(source.data[pos+before[i].len]))) { found = i; break; }
        if (found < len) {
            fwrite(after[found].data,1,after[found].len,file); pos += before[found].len;
        } else fputc(source.data[pos++],file);
    }
    return !ferror(file) || ce_error(error,0,"cannot finish generated dependency output");
}
static bool dependency_rewrite(CeArena *a, const char *path, char **generated, char **original,
                                size_t len, CeError *error) {
    CeText source;
    if (!ce_read_file(a,path,&source,error)) return false;
    FILE *file = fopen(path,"wb");
    if (!file) return ce_error(error,0,"cannot update generated dependency file: %s",strerror(errno));
    bool ok = dependency_write(a,source,file,generated,original,len,error);
    ok = fclose(file) == 0 && ok;
    return ok || ce_error(error,0,"cannot finish generated dependency file");
}

static bool module_dependencies_write(CeArena *arena, CeText text, const CeNativeOutput *native,
                                       FILE *file, CeError *error) {
    if (!native->ndependencies)
        return fwrite(text.data,1,text.len,file) == text.len || ce_error(error,0,"cannot write dependencies");
    size_t colon = 0; bool escaped = false;
    for (; colon < text.len; ++colon) {
        unsigned c = text.data[colon];
        if (c == ':' && !escaped) break;
        escaped = c == '\\' && !escaped;
    }
    if (colon == text.len) return ce_error(error,0,"native dependency output has no target");
    fwrite(text.data,1,colon+1,file);
    for (size_t i = 0; i < native->ndependencies; ++i) {
        CeText path = dependency_word(arena,(const char *)native->dependencies[i].data);
        fputc(' ',file); fwrite(path.data,1,path.len,file);
    }
    fwrite(text.data+colon+1,1,text.len-colon-1,file);
    return !ferror(file) || ce_error(error,0,"cannot write module dependencies");
}
static bool module_dependencies_file(CeArena *arena, const char *path, const CeNativeOutput *native,
                                      CeError *error) {
    if (!native->ndependencies) return true;
    CeText text;
    if (!ce_read_file(arena,path,&text,error)) return false;
    FILE *file = fopen(path,"wb");
    if (!file) return ce_error(error,0,"cannot update macro module dependencies");
    bool ok = module_dependencies_write(arena,text,native,file,error);
    return fclose(file) == 0 && ok;
}

static bool source_document(CeText source) {
    static const char prefix[] = "(cesta source/";
    size_t i = 0;
    while (i < source.len && dependency_boundary(source.data[i])) ++i;
    return source.len-i >= sizeof(prefix)-1 && !memcmp(source.data+i,prefix,sizeof(prefix)-1);
}

/* Options already consumed by phase 4 must not be applied again to an .i.
   Native code generation, target/ABI, diagnostic and link options remain. */
static bool preprocessing_option(const char *option) {
    const char *with_value[] = {"-I","-D","-U","-include","-imacros","-isystem","-iquote","-idirafter",
        "-iprefix","-iwithprefix","-iwithprefixbefore","-Xpreprocessor","-x"};
    for (size_t i = 0; i < sizeof(with_value)/sizeof(*with_value); ++i)
        if (!strncmp(option,with_value[i],strlen(with_value[i]))) return true;
    return !strcmp(option,"-nostdinc") || !strcmp(option,"-trigraphs") || !strcmp(option,"-C") ||
        !strcmp(option,"-CC") || !strcmp(option,"-P") || !strncmp(option,"-Wp,",4) ||
        !strncmp(option,"-finput-charset=",16) || !strncmp(option,"--embed-dir=",12) ||
        !strncmp(option,"-embed-dir=",11);
}

static char *stem_path(CeArena *arena, const char *path, bool basename) {
    const char *start = basename ? strrchr(path,'/') : NULL;
    start = start ? start+1 : path;
    const char *dot = strrchr(start,'.');
    if (dot && !basename) { const char *slash = strrchr(start,'/'); if (slash && dot < slash) dot = NULL; }
    size_t n = dot ? (size_t)(dot-start) : strlen(start);
    char *result = ce_alloc(arena,n+1); memcpy(result,start,n); result[n] = 0; return result;
}

static uint64_t text_hash(uint64_t hash, CeText text) {
    for (size_t i = 0; i < text.len; ++i) hash = (hash ^ text.data[i])*UINT64_C(1099511628211);
    return hash;
}

/* Publish a complete immutable input before starting a compiler. Separate
   generations cannot overwrite one another during parallel builds. */
static bool publish_text(CeArena *arena, const char *path, CeText text, CeError *error) {
    char *temporary = ce_alloc(arena,strlen(path)+16);
    sprintf(temporary,"%s.XXXXXX",path);
    int fd = mkstemp(temporary);
    if (fd < 0) return ce_error(error,0,"cannot create generated C input: %s",strerror(errno));
    FILE *file = fdopen(fd,"wb");
    if (!file) { close(fd); return ce_error(error,0,"cannot open generated C input"); }
    bool ok = fwrite(text.data,1,text.len,file) == text.len;
    ok = fclose(file) == 0 && ok;
    if (ok && rename(temporary,path)) ok = false;
    return ok || ce_error(error,0,"cannot publish generated C input: %s",strerror(errno));
}

static bool dependency_option(const char *a) {
    return !strcmp(a,"-M") || !strcmp(a,"-MM") || !strcmp(a,"-MD") || !strcmp(a,"-MMD") ||
        !strcmp(a,"-MP") || !strcmp(a,"-MG") || !strncmp(a,"-MF",3) || !strncmp(a,"-MT",3) || !strncmp(a,"-MQ",3);
}

static bool link_option(const char *a) {
    return !strncmp(a,"-Wl,",4) || !strncmp(a,"-l",2) || !strncmp(a,"-L",2) ||
        !strncmp(a,"-fuse-ld=",9) || !strncmp(a,"--ld-path=",10) ||
        !strcmp(a,"-Xlinker") || !strcmp(a,"-shared") || !strcmp(a,"-pie") || !strcmp(a,"-static") ||
        !strcmp(a,"-rdynamic") || !strcmp(a,"-no-pie") || !strcmp(a,"-s") ||
        !strcmp(a,"-nostdlib") || !strcmp(a,"-nodefaultlibs") || !strcmp(a,"-nostartfiles") ||
        !strcmp(a,"-static-libgcc") || !strcmp(a,"-static-pie");
}

/* The native compiler supplies active header identifiers and macro
   replacement spelling. This is a finite name inventory for macro lowering,
   not a Cesta preprocessing or C semantic interpretation. */
static bool native_render(const CeGrammar *g, CeArena *arena, CeText source,
                           const char *presentation, const char *identity,
                           size_t n, char **args, const char *cache, CeLazyLexer *lexer, bool readable,
                           FILE *plan, CeNativeOutput *out, CeError *error) {
    /* Compiled text maps back to the authored source; readable export does not. */
    bool lines = !readable;
    CeNativeEnvironment probe = {.unguarded=true,.readable=readable,.lines=lines,.lexer=lexer};
    if (!ce_native_text_at(g,arena,source,presentation,identity,&probe,out,error)) return false;
    if (!out->needs_macro_inventory) {
        if (!plan) return true;
        probe.plan = plan;
        return ce_native_text_at(g,arena,source,presentation,identity,&probe,out,error);
    }
    uint64_t path_hash = text_hash(UINT64_C(14695981039346656037),
        (CeText){(unsigned char *)identity,strlen(identity)});
    uint64_t content_hash = text_hash(UINT64_C(14695981039346656037),out->text);
    const char *base = strrchr(identity,'/'); base = base ? base+1 : identity;
    const char *dot = strrchr(base,'.'); size_t stem = dot ? (size_t)(dot-base) : strlen(base);
    size_t capacity = strlen(cache)+strlen(base)+96;
    char *directory = ce_alloc(arena,capacity);
    snprintf(directory,capacity,"%s/names/%016llx/%016llx",cache,
        (unsigned long long)path_hash,(unsigned long long)content_hash);
    if (!directories(directory,error)) return false;
    char *path = ce_alloc(arena,capacity);
    snprintf(path,capacity,"%s/%.*s.c",directory,(int)stem,base);
    if (!publish_text(arena,path,out->text,error)) return false;
    char *quoted = ce_alloc(arena,strlen(identity)+2); strcpy(quoted,identity);
    char *slash = strrchr(quoted,'/');
    if (slash) { if (slash == quoted) slash[1] = 0; else *slash = 0; }
    else strcpy(quoted,".");
    char **command = ce_alloc(arena,(n*2+10)*sizeof(*command)); size_t k = 1;
    command[0] = args[0];
    for (size_t i = 1; i < n; ++i) {
        const char *a = args[i];
        if (!strcmp(a,"-c") || !strcmp(a,"-S") || !strcmp(a,"-E") || !strcmp(a,"-fsyntax-only") ||
            !strcmp(a,"-dM") || !strcmp(a,"-dD") || !strcmp(a,"-P") ||
            dependency_option(a) || link_option(a) || !strncmp(a,"-o",2) || !strcmp(a,"-x")) {
            if (option_value(a) && i+1 < n) ++i;
            continue;
        }
        if (a[0] != '-') continue;
        command[k++] = args[i];
        if (option_value(a) && i+1 < n) command[k++] = args[++i];
    }
    command[k++] = "-E"; command[k++] = "-dD"; command[k++] = "-iquote"; command[k++] = quoted;
    command[k++] = "-x"; command[k++] = "c"; command[k++] = path; command[k] = NULL;
    CeText inventory;
    if (ce_execute_stdout(command,arena,&inventory,error))
        return ce_error(error,0,"native compiler could not admit the macro name environment");
    CeNativeEnvironment environment = {.identifiers=inventory,.readable=readable,.lines=lines,.lexer=lexer,.plan=plan};
    return ce_native_text_at(g,arena,source,presentation,identity,&environment,out,error);
}

static bool native_prepare(const CeGrammar *g, CeArena *arena, CeText source,
                           const char *presentation, const char *identity,
                           size_t n, char **args, const char *cache, CeLazyLexer *lexer, bool readable,
                           FILE *plan, CeNativeOutput *out, CeError *error) {
    if (!native_render(g,arena,source,presentation,identity,n,args,cache,lexer,readable,plan,out,error)) return false;
    if (!out->ownership) return true;
    char **flags = ce_alloc(arena,(n+1)*sizeof(*flags)); size_t count = 1; flags[0] = args[0];
    for (size_t i = 1; i < n; ++i) {
        const char *a = args[i];
        if (!strcmp(a,"-c") || !strcmp(a,"-S") || !strcmp(a,"-E") || !strcmp(a,"-fsyntax-only") ||
            dependency_option(a) || link_option(a) || !strncmp(a,"-o",2) || !strcmp(a,"-x")) {
            if (option_value(a) && i+1 < n) ++i;
            continue;
        }
        if (a[0] != '-') continue;
        flags[count++] = args[i];
        if (option_value(a) && i+1 < n) flags[count++] = args[++i];
    }
    return ce_ownership_check(arena,out->ownership,out->text,identity,count,flags,plan,error);
}

/* Both renderings print the same grammar, so they share one lexer. */
bool ce_native_prepare(const CeGrammar *g, CeArena *arena, CeText source,
                        const char *presentation, const char *identity,
                        size_t n, char **args, const char *cache,
                        CeNativeOutput *out, CeError *error) {
    CeLazyLexer lexer = {0};
    bool ok = native_prepare(g,arena,source,presentation,identity,n,args,cache,&lexer,false,NULL,out,error);
    ce_lazy_lexer_free(&lexer); return ok;
}

bool ce_native_export(const CeGrammar *g, CeArena *arena, CeText source,
                      const char *presentation, const char *identity,
                      size_t n, char **args, const char *cache, FILE *plan,
                      CeNativeOutput *out, CeError *error) {
    CeLazyLexer lexer = {0};
    bool ok = native_prepare(g,arena,source,presentation,identity,n,args,cache,&lexer,true,plan,out,error);
    ce_lazy_lexer_free(&lexer); return ok;
}

static bool compiler_is_clang(CeArena *arena, const char *compiler) {
    char *command[] = {(char *)compiler,"--version",NULL};
    CeText text; CeError ignored = {{0},0};
    if (ce_execute_stdout(command,arena,&text,&ignored) || !text.data) return false;
    return strstr((const char *)text.data,"clang") != NULL;
}

static char *directory_of(CeArena *arena, const char *path) {
    char *directory = ce_alloc(arena,strlen(path)+2); strcpy(directory,path);
    char *slash = strrchr(directory,'/');
    if (slash) { if (slash == directory) slash[1] = 0; else *slash = 0; }
    else strcpy(directory,".");
    return directory;
}

/* Each source compiles on its own, as the native compiler driver would compile
   it: a generated unit with its authored directory first for quoted includes,
   so mixed sources keep independent include contexts, and with the compiler's
   diagnostics routed to authored lines and columns. Linking follows. */
static int separate_compile(CeArena *arena, size_t n, char **args, const unsigned char *input,
                            char **identities, const CeNativeOutput *native, const CeText *authored,
                            const char *cache, const unsigned char *is_source, size_t sources,
                            CeError *error) {
    bool syntax = false, assembly = false, compile = false, deps = false, custom_target = false;
    const char *output = NULL, *depfile = NULL;
    for (size_t i = 1; i < n; ++i) {
        const char *a = args[i];
        if (!strcmp(a,"-S")) assembly = compile = true;
        if (!strcmp(a,"-c")) compile = true;
        if (!strcmp(a,"-fsyntax-only")) syntax = compile = true;
        if (!strcmp(a,"-MD") || !strcmp(a,"-MMD")) deps = true;
        if (!strncmp(a,"-MT",3) || !strncmp(a,"-MQ",3)) custom_target = true;
        if (!strcmp(a,"-o") && i+1 < n) output = args[i+1];
        else if (!strncmp(a,"-o",2) && a[2]) output = a+2;
        if (!strcmp(a,"-MF") && i+1 < n) depfile = args[i+1];
        else if (!strncmp(a,"-MF",3) && a[3]) depfile = a+3;
        if (option_value(a) && i+1 < n) ++i;
    }
    if (compile && !syntax && output && sources > 1)
        return ce_error(error,0,"one output cannot name multiple inputs"), 1;
    bool clang = compiler_is_clang(arena,args[0]);
    CeRouteStyle style = {.color=ce_route_color(STDERR_FILENO,n,args)};
    char **objects = ce_alloc(arena,n*sizeof(*objects));
    unsigned char *temporary = ce_alloc(arena,n); memset(temporary,0,n);
    int failures = 0;
    for (size_t at = 1; at < n; ++at) {
        if (!is_source[at]) continue;
        const char *original = identities[at] ? identities[at] : args[at];
        char **cc = ce_alloc(arena,(n*2+24)*sizeof(*cc)); size_t k = 0;
        cc[k++] = args[0];
        bool dependency_stdout = deps && depfile && !strcmp(depfile,"-");
        for (size_t i = 1; i < n; ++i) {
            const char *a = args[i];
            if (is_source[i]) continue;
            if (!strcmp(a,"-c") || !strcmp(a,"-S") || !strcmp(a,"-E") || !strcmp(a,"-fsyntax-only")) continue;
            if (!strncmp(a,"-o",2) || !strcmp(a,"-x")) { if (option_value(a) && i+1 < n) ++i; continue; }
            if (link_option(a)) { if (option_value(a) && i+1 < n) ++i; continue; }
            /* Dependencies for stdout pass through a file, to name authored sources. */
            if (dependency_stdout && !strncmp(a,"-MF",3)) { if (!a[3] && i+1 < n) ++i; continue; }
            if (a[0] != '-') continue; /* object and archive inputs belong to linking */
            cc[k++] = args[i];
            if (option_value(a) && i+1 < n) cc[k++] = args[++i];
        }
        char *dependency_file = NULL;
        if (dependency_stdout) {
            dependency_file = ce_alloc(arena,strlen(cache)+24);
            sprintf(dependency_file,"%s/deps-XXXXXX",cache);
            int fd = mkstemp(dependency_file);
            if (fd < 0) { ce_error(error,0,"cannot create native dependency output"); failures = -1; break; }
            close(fd); cc[k++] = "-MF"; cc[k++] = dependency_file;
        }
        const char *object = NULL;
        if (!syntax) {
            if (compile && output) object = output;
            else if (compile) {
                const char *stem = stem_path(arena,original,true);
                char *path = ce_alloc(arena,strlen(stem)+3);
                sprintf(path,"%s%s",stem,assembly ? ".s" : ".o"); object = path;
            } else {
                char *path = ce_alloc(arena,strlen(cache)+24);
                sprintf(path,"%s/object-XXXXXX",cache);
                int fd = mkstemp(path);
                if (fd < 0) { ce_error(error,0,"cannot create an intermediate object"); failures = -1; break; }
                close(fd); object = path; temporary[at] = 1;
            }
        }
        objects[at] = (char *)object;
        if (deps) {
            const char *stem = compile && output && sources == 1 ? stem_path(arena,output,false) : stem_path(arena,original,true);
            char *derived = ce_alloc(arena,strlen(stem)+5); sprintf(derived,"%s.d",stem);
            if (!depfile) { cc[k++] = "-MF"; cc[k++] = derived; }
            if (!custom_target) {
                char *target = ce_alloc(arena,strlen(stem)+5); sprintf(target,"%s.o",stem);
                cc[k++] = "-MT"; cc[k++] = (char *)(compile && output && sources == 1 ? output : target);
            }
        }
        if (input[at]) {
            cc[k++] = "-iquote"; cc[k++] = directory_of(arena,original);
            cc[k++] = clang ? "-fno-caret-diagnostics" : "-fno-diagnostics-show-caret";
            if (!clang) cc[k++] = "-fdiagnostics-column-unit=byte";
            cc[k++] = "-fdiagnostics-color=never";
        }
        cc[k++] = syntax ? "-fsyntax-only" : assembly ? "-S" : "-c";
        if (object) { cc[k++] = "-o"; cc[k++] = (char *)object; }
        cc[k++] = "-x"; cc[k++] = "c"; cc[k++] = args[at]; cc[k] = NULL;
        int status;
        if (input[at]) {
            CeText messages = {NULL,0};
            status = ce_execute_capture(cc,STDERR_FILENO,arena,&messages,error);
            CeRouteUnit unit = {.identity=original,.generated=args[at],.source=authored[at],.map=&native[at]};
            if (messages.len) ce_route_diagnostics(messages,&unit,1,&style,stderr);
            if (status == 2 && error->message[0]) { failures = -1; break; }
        } else status = ce_execute(cc,error);
        if (status) { if (dependency_file) unlink(dependency_file); ++failures; continue; }
        if (deps) {
            const char *stem = compile && output && sources == 1 ? stem_path(arena,output,false) : stem_path(arena,original,true);
            char *derived = ce_alloc(arena,strlen(stem)+5); sprintf(derived,"%s.d",stem);
            char *before[] = {args[at]}, *after[] = {(char *)original};
            const char *written = dependency_file ? dependency_file : depfile ? depfile : derived;
            bool ok = dependency_rewrite(arena,written,before,after,1,error) &&
                      (!input[at] || module_dependencies_file(arena,written,&native[at],error));
            if (ok && dependency_file) {
                CeText text;
                ok = ce_read_file(arena,dependency_file,&text,error) &&
                     fwrite(text.data,1,text.len,stdout) == text.len;
                if (!ok && !error->message[0]) ce_error(error,0,"cannot write native dependencies to stdout");
            }
            if (dependency_file) unlink(dependency_file);
            if (!ok) { failures = -1; break; }
        }
    }
    int result = failures < 0 ? 2 : failures ? 1 : 0;
    if (!result && !compile) {
        char **final = ce_alloc(arena,(n+2)*sizeof(*final)); size_t k = 0;
        final[k++] = args[0];
        for (size_t i = 1; i < n; ++i) {
            const char *a = args[i];
            if (is_source[i]) { final[k++] = objects[i]; continue; }
            if (preprocessing_option(a) || dependency_option(a)) { if (option_value(a) && i+1 < n) ++i; continue; }
            final[k++] = args[i];
            if (option_value(a) && i+1 < n) final[k++] = args[++i];
        }
        final[k] = NULL;
        result = ce_execute(final,error);
    }
    for (size_t i = 1; i < n; ++i) if (temporary[i]) unlink(objects[i]);
    return result;
}

/* This bridge delegates preprocessing to the selected native compiler. Cesta
   neither reads nor interprets its expanded tokens. A per-input invocation
   preserves quoted-include context when inputs live in different directories. */
static int native_authoring_compile(CeArena *arena, size_t n, char **args,
                                     unsigned char *input, char **identities,
                                     const CeNativeOutput *native, const CeText *authored,
                                     const char *cache, CeError *error) {
    bool only = false, deps_only = false, deps = false, compile = false, custom_target = false;
    const char *output = NULL, *depfile = NULL; size_t sources = 0;
    unsigned char *is_source = ce_alloc(arena,n);
    for (size_t i = 1; i < n; ++i) {
        const char *a = args[i];
        if (!strcmp(a,"-E")) only = true;
        if (!strcmp(a,"-M") || !strcmp(a,"-MM")) deps_only = only = true;
        if (!strcmp(a,"-MD") || !strcmp(a,"-MMD")) deps = true;
        if (!strcmp(a,"-c") || !strcmp(a,"-S") || !strcmp(a,"-fsyntax-only")) compile = true;
        if (!strncmp(a,"-MT",3) || !strncmp(a,"-MQ",3)) custom_target = true;
        if (!strcmp(a,"-o") && i+1 < n) output = args[i+1];
        else if (!strncmp(a,"-o",2) && a[2]) output = a+2;
        if (!strcmp(a,"-MF") && i+1 < n) depfile = args[i+1];
        else if (!strncmp(a,"-MF",3) && a[3]) depfile = a+3;
        if (option_value(a)) { if (i+1 < n) ++i; continue; }
        if (input[i] || (a[0] != '-' && suffix(a,".c"))) { is_source[i] = 1; ++sources; }
    }
    if (only && output && sources > 1) return ce_error(error,0,"one preprocessing output cannot name multiple inputs"), 1;
    if (!only) return separate_compile(arena,n,args,input,identities,native,authored,cache,is_source,sources,error);
    const char *generated_base = NULL;
    for (size_t i = 1; i < n; ++i) if (input[i]) { generated_base = args[i]; break; }
    for (size_t at = 1; at < n; ++at) {
        if (!is_source[at]) continue;
        const char *original = identities[at] ? identities[at] : args[at];
        const char *actual_depfile = depfile;
        bool dependency_stdout = deps && depfile && !strcmp(depfile,"-");
        char *directory = ce_alloc(arena,strlen(original)+2); strcpy(directory,original);
        char *slash = strrchr(directory,'/');
        if (slash) { if (slash == directory) slash[1] = 0; else *slash = 0; }
        else strcpy(directory,".");
        char **pp = ce_alloc(arena,(n*2+16)*sizeof(*pp)); size_t k = 1; pp[0] = args[0];
        for (size_t i = 1; i < n; ++i) {
            const char *a = args[i];
            if (is_source[i]) continue;
            if (!strcmp(a,"-c") || !strcmp(a,"-S") || !strcmp(a,"-E") || !strcmp(a,"-fsyntax-only")) continue;
            if (!strncmp(a,"-o",2) || !strcmp(a,"-x")) { if (option_value(a) && i+1 < n) ++i; continue; }
            if (link_option(a)) {
                if (option_value(a) && i+1 < n) ++i;
                continue;
            }
            if (a[0] != '-') continue; /* object and archive inputs belong to linking */
            pp[k++] = args[i];
            if (option_value(a) && i+1 < n) pp[k++] = args[++i];
        }
        if (deps) {
            const char *stem = output && sources == 1 ? stem_path(arena,output,false) : stem_path(arena,original,true);
            char *derived = ce_alloc(arena,strlen(stem)+5);
            sprintf(derived,"%s.d",stem);
            if (!depfile) { pp[k++] = "-MF"; pp[k++] = derived; }
            if (!custom_target) {
                char *target = ce_alloc(arena,strlen(stem)+5); sprintf(target,"%s.o",stem);
                pp[k++] = "-MT"; pp[k++] = (char *)(compile && output && sources == 1 ? output : target);
            }
            if (dependency_stdout) {
                char *temporary = ce_alloc(arena,strlen(generated_base)+24);
                sprintf(temporary,"%s.deps.XXXXXX",generated_base);
                int fd = mkstemp(temporary);
                if (fd < 0) return ce_error(error,0,"cannot create native dependency output"), 1;
                close(fd); actual_depfile = temporary;
                pp[k++] = "-MF"; pp[k++] = temporary;
            }
        }
        if (!deps_only) pp[k++] = "-E";
        if (input[at]) { pp[k++] = "-iquote"; pp[k++] = directory; }
        pp[k++] = "-x"; pp[k++] = "c"; pp[k++] = args[at]; pp[k] = NULL;
        CeText preprocessed;
        int status = ce_execute_stdout(pp,arena,&preprocessed,error);
        if (status) return status;
        char *before[] = {args[at]}, *after[] = {(char *)original};
        CeText dependency_text = {NULL,0};
        if (deps || (deps_only && depfile && strcmp(depfile,"-"))) {
            const char *stem = output && sources == 1 ? stem_path(arena,output,false) : stem_path(arena,original,true);
            char *derived = ce_alloc(arena,strlen(stem)+5); sprintf(derived,"%s.d",stem);
            if (!dependency_rewrite(arena,actual_depfile ? actual_depfile : derived,before,after,1,error)) return 1;
            if (!module_dependencies_file(arena,actual_depfile ? actual_depfile : derived,&native[at],error)) return 1;
            if (dependency_stdout && !ce_read_file(arena,actual_depfile,&dependency_text,error)) return 1;
        }
        if (only) {
            FILE *file = output ? fopen(output,"wb") : stdout;
            if (!file) return ce_error(error,0,"cannot open preprocessing output"), 1;
            bool ok;
            if (deps_only && native[at].ndependencies && (!depfile || !strcmp(depfile,"-"))) {
                char *buffer = NULL; size_t length = 0; FILE *stream = open_memstream(&buffer,&length);
                if (!stream) { if (output) fclose(file); return ce_error(error,0,"cannot format module dependencies"), 1; }
                ok = dependency_write(arena,preprocessed,stream,before,after,1,error);
                ok = fclose(stream) == 0 && ok;
                if (ok) ok = module_dependencies_write(arena,(CeText){(unsigned char *)buffer,length},&native[at],file,error);
                free(buffer);
            } else ok = deps_only ? dependency_write(arena,preprocessed,file,before,after,1,error) :
                fwrite(preprocessed.data,1,preprocessed.len,file) == preprocessed.len;
            if (output) ok = fclose(file) == 0 && ok;
            if (!ok) return ce_error(error,0,"cannot write preprocessing output"), 1;
        }
        if (dependency_stdout && fwrite(dependency_text.data,1,dependency_text.len,stdout) != dependency_text.len)
            return ce_error(error,0,"cannot write native dependencies to stdout"), 1;
    }
    return 0;
}

static bool source_dependency_row(CeArena *arena, FILE *out, const CePPResult *pp,
                                   const char *source, const char *target, bool user_only,
                                   bool phony, int argc, char **argv) {
    bool custom = false;
    for (int i = 0; i < argc; ++i) {
        if (strncmp(argv[i],"-MT",3) && strncmp(argv[i],"-MQ",3)) {
            if (option_value(argv[i]) && i+1 < argc) ++i;
            continue;
        }
        bool quote = argv[i][2] == 'Q';
        const char *name = argv[i]+3;
        if (!*name) { if (++i == argc) return false; name = argv[i]; }
        if (custom) fputc(' ',out);
        if (quote) { CeText word = dependency_word(arena,name); fwrite(word.data,1,word.len,out); }
        else fputs(name,out);
        custom = true;
    }
    if (!custom) { CeText word = dependency_word(arena,target); fwrite(word.data,1,word.len,out); }
    fputc(':',out);
    CeText word = dependency_word(arena,source); fputc(' ',out); fwrite(word.data,1,word.len,out);
    while (source[0] == '.' && source[1] == '/') source += 2;
    for (size_t i = 0; i < pp->ndependencies; ++i) {
        const CePPDependency *dependency = &pp->dependencies[i];
        if (!strcmp(dependency->file,source) || (user_only && dependency->system)) continue;
        word = dependency_word(arena,dependency->file); fputc(' ',out); fwrite(word.data,1,word.len,out);
    }
    fputc('\n',out);
    if (phony) for (size_t i = 0; i < pp->ndependencies; ++i) {
        const CePPDependency *dependency = &pp->dependencies[i];
        if (!strcmp(dependency->file,source) || (user_only && dependency->system)) continue;
        word = dependency_word(arena,dependency->file); fputc('\n',out); fwrite(word.data,1,word.len,out); fputs(":\n",out);
    }
    return !ferror(out);
}

/* Dependency output for preprocessed source documents comes from the files
   phase 4 actually read, rather than from the subsequently compiled .i. */
static bool source_dependencies(CeArena *arena, CePPResult *pp, char **sources, size_t len,
                                 int argc, char **argv, CeError *error) {
    bool requested = false, only = false, user_only = false, phony = false, compile = false;
    const char *depfile = NULL, *output = NULL;
    for (int i = 0; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a,"-M") || !strcmp(a,"-MM")) { requested = only = true; user_only = !strcmp(a,"-MM"); }
        if (!strcmp(a,"-MD") || !strcmp(a,"-MMD")) { requested = true; user_only = !strcmp(a,"-MMD"); }
        if (!strcmp(a,"-MP")) phony = true;
        if (!strcmp(a,"-c") || !strcmp(a,"-S")) compile = true;
        if ((!strcmp(a,"-MF") || !strcmp(a,"-o")) && i+1 < argc) {
            if (!strcmp(a,"-MF")) depfile = argv[++i]; else output = argv[++i];
        } else if (!strncmp(a,"-MF",3) && a[3]) depfile = a+3;
        else if (!strncmp(a,"-o",2) && a[2]) output = a+2;
        else if (option_value(a) && i+1 < argc) ++i;
    }
    if (!requested) return true;
    FILE *shared = NULL;
    if (only || depfile) {
        const char *path = depfile ? depfile : only ? output : NULL;
        shared = !path || !strcmp(path,"-") ? stdout : fopen(path,"wb");
        if (!shared) return ce_error(error,0,"cannot write dependency output: %s",strerror(errno));
    }
    bool ok = true;
    for (size_t i = 0; i < len && ok; ++i) {
        char *stem = stem_path(arena,sources[i],true);
        char *target = ce_alloc(arena,strlen(stem)+3);
        sprintf(target,"%s.o",stem);
        if (!only) target = (char *)(output ? output : compile ? target : "a.out");
        FILE *file = shared;
        if (!file) {
            char *base = output ? stem_path(arena,output,false) : compile ? stem : "a";
            size_t n = strlen(base)+strlen(stem)+6;
            char *path = ce_alloc(arena,n);
            if (len > 1 && !compile) snprintf(path,n,"%s-%s.d",base,stem);
            else snprintf(path,n,"%s.d",base);
            file = fopen(path,"wb");
            if (!file) { ok = ce_error(error,0,"cannot write dependency output: %s",strerror(errno)); break; }
        }
        ok = source_dependency_row(arena,file,&pp[i],sources[i],target,user_only,phony,argc,argv);
        if (!shared) ok = fclose(file) == 0 && ok;
    }
    if (shared && shared != stdout) ok = fclose(shared) == 0 && ok;
    return ok || ce_error(error,0,"cannot finish source dependency output");
}

static int source_document_compile(CeArena *arena, size_t n, char **arguments,
                                    unsigned char *inputs, CeText *contents, char **identities,
                                    const char *cache, const char *compiler, bool standard,
                                    int argc, char **argv, CeError *error) {
    CePPResult *expanded = ce_alloc(arena,n*sizeof(*expanded));
    char **sources = ce_alloc(arena,n*sizeof(*sources)); size_t count = 0;
    bool only = false, preprocess_only = false, markers = true; const char *output = NULL; int result = 1;
    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i],"-M") || !strcmp(argv[i],"-MM")) only = true;
        if (!strcmp(argv[i],"-E")) preprocess_only = true;
        if (!strcmp(argv[i],"-P")) markers = false;
        if (!strcmp(argv[i],"-o") && i+1 < argc) output = argv[++i];
        else if (!strncmp(argv[i],"-o",2) && argv[i][2]) output = argv[i]+2;
        else if (option_value(argv[i]) && i+1 < argc) ++i;
    }
    for (size_t i = 1; i < n; ++i) {
        if (option_value(arguments[i]) && i+1 < n) { ++i; continue; }
        if (!(inputs[i] || suffix(arguments[i],".c"))) continue;
        if (!inputs[i]) { identities[i] = arguments[i]; if (!ce_read_file(arena,arguments[i],&contents[i],error)) goto done; }
        char **pp_args = ce_alloc(arena,(n+6)*sizeof(*pp_args)); size_t k = 0;
        pp_args[k++] = "--cc"; pp_args[k++] = (char *)compiler;
        if (!standard) pp_args[k++] = "-std=gnu11";
        for (size_t j = 1; j < n; ++j) {
            const char *a = arguments[j];
            if (inputs[j] || a[0] != '-' || !strcmp(a,"--")) continue;
            if (!strcmp(a,"-o") || !strcmp(a,"-L") || !strcmp(a,"-l") || !strcmp(a,"-Xlinker")) { if (j+1 < n) ++j; continue; }
            if (!strncmp(a,"-Wl,",4) || !strncmp(a,"-L",2) || !strncmp(a,"-l",2) || (!strncmp(a,"-o",2) && a[2])) continue;
            pp_args[k++] = arguments[j];
            if (option_value(a) && j+1 < n) pp_args[k++] = arguments[++j];
        }
        pp_args[k++] = identities[i];
        CeText ce_builtin_CPPGNUGrammar(void);
        if (!ce_pp_run_source(ce_builtin_CPPGNUGrammar(),(int)k,pp_args,contents[i],markers,&expanded[count],error)) {
            ce_pp_diagnostic(stderr,&expanded[count],error); ++count; goto done;
        }
        uint64_t hash = UINT64_C(14695981039346656037);
        for (const unsigned char *p = (const unsigned char *)identities[i]; *p; ++p) hash = (hash ^ *p)*UINT64_C(1099511628211);
        for (size_t j = 0; j < expanded[count].text.len; ++j)
            hash = (hash ^ expanded[count].text.data[j])*UINT64_C(1099511628211);
        char *stem = stem_path(arena,identities[i],true);
        size_t capacity = strlen(cache)+strlen(stem)+40;
        char *parent = ce_alloc(arena,strlen(cache)+20);
        snprintf(parent,strlen(cache)+20,"%s/%016llx",cache,(unsigned long long)hash);
        if (!directories(parent,error)) { ++count; goto done; }
        char *path = ce_alloc(arena,capacity);
        snprintf(path,capacity,"%s/%s.i",parent,stem);
        char *temporary = ce_alloc(arena,strlen(path)+12);
        sprintf(temporary,"%s.XXXXXX",path);
        int descriptor = mkstemp(temporary);
        FILE *file = descriptor < 0 ? NULL : fdopen(descriptor,"wb");
        if (!file && descriptor >= 0) close(descriptor);
        if (!file) { ce_error(error,0,"cannot write preprocessed source document: %s",strerror(errno)); ++count; goto done; }
        bool written = fwrite(expanded[count].text.data,1,expanded[count].text.len,file) == expanded[count].text.len;
        written = fclose(file) == 0 && written;
        written = written && rename(temporary,path) == 0;
        if (!written) { ce_error(error,0,"cannot finish preprocessed source document"); ++count; goto done; }
        sources[count++] = identities[i]; arguments[i] = path;
    }
    if (preprocess_only && !only) {
        FILE *file = !output || !strcmp(output,"-") ? stdout : fopen(output,"wb");
        if (!file) { ce_error(error,0,"cannot write preprocessing output: %s",strerror(errno)); goto done; }
        bool written = true;
        for (size_t i = 0; i < count; ++i)
            written &= fwrite(expanded[i].text.data,1,expanded[i].text.len,file) == expanded[i].text.len;
        if (file != stdout) written = fclose(file) == 0 && written;
        if (!written) { ce_error(error,0,"cannot finish preprocessing output"); goto done; }
    } else if (!only) {
        size_t k = 1;
        for (size_t i = 1; i < n; ++i) {
            const char *a = arguments[i];
            if (preprocessing_option(a) || !strncmp(a,"-M",2)) {
                if (option_value(a) && i+1 < n) ++i;
                continue;
            }
            arguments[k++] = arguments[i];
            if (option_value(a) && i+1 < n) arguments[k++] = arguments[++i];
        }
        if (!standard) arguments[k++] = "-std=gnu11";
        arguments[k] = NULL;
        result = ce_execute(arguments,error);
        if (result) goto done;
    }
    result = source_dependencies(arena,expanded,sources,count,argc,argv,error) ? 0 : 1;
done:
    for (size_t i = 0; i < count; ++i) ce_pp_result_free(&expanded[i]);
    return result;
}

int ce_source_command(int argc, char **argv, unsigned composition) {
    CeArena arena = {0}; CeGrammar g = {0}; CeError error = {{0},0};
    CeCatalog catalog = {.composition=composition}; CeTokens tokens = {0};
    CeForest forest = {.root=-1}; CeDFA dfa = {0}; CeLR lr = {0}; CeText source = {NULL,0};
    bool module = !strcmp(argv[1],"check-module") || !strcmp(argv[1],"module-grammar");
    bool gnu = true; const char *path = argv[2]; int result = 1;
    CePPResult preprocessed = {0}; bool have_preprocessed = false;
    int compiler_at = -1;
    for (int i = 2; i < argc; ++i) if (!strcmp(argv[i],"--cc")) { compiler_at = i; break; }
    if (compiler_at >= 0) {
        if (strcmp(argv[1],"read")) { ce_error(&error,0,"--cc source preprocessing applies to read"); goto done; }
        path = argv[argc-1];
        for (int i = compiler_at+2; i < argc; ++i)
            if (!strncmp(argv[i],"-std=",5)) gnu = !(argv[i][5] == 'c' || !strncmp(argv[i]+5,"iso",3));
        CeText ce_builtin_CPPGNUGrammar(void);
        if (!ce_pp_run(ce_builtin_CPPGNUGrammar(),argc-compiler_at,argv+compiler_at,false,&preprocessed,&error)) goto done;
        have_preprocessed = true; source = preprocessed.text;
    } else if (!module && argc == 4) {
        if (!strcmp(argv[2],"--std=c11")) gnu = false;
        else if (strcmp(argv[2],"--std=gnu11")) {
            ce_error(&error,0,"source command supports --std=c11 or --std=gnu11"); goto done;
        }
        path = argv[3];
    }
    if (module) {
        if (!ce_grammar_read(&g,ce_builtin_TheoryModuleSyntax(),&error) || !ce_grammar_finish(&g,&error)) goto done;
        catalog.syntax = &g; CeModule *loaded; CePresentation presentation;
        if (!ce_read_file(&arena,path,&source,&error) ||
            !ce_module_load(&catalog,path,&loaded,&error) || !ce_module_check(&catalog,loaded,&error)) goto done;
        if (!strcmp(argv[1],"check-module")) puts("module: theory-source admission passed");
        else {
            if (!ce_module_theory(&catalog,loaded,argv[3],&presentation,&error)) goto done;
            for (size_t i = 0; i < presentation.len; ++i) {
                CeTree *tree = presentation.definitions[i].tree;
                if (!ce_tree_print(&g,tree,tree->symbol,stdout,&error)) goto done;
                fputs(";\n",stdout);
            }
        }
    } else {
        if (!(have_preprocessed ? ce_builtin_dialect(&g,gnu,preprocessed.clang,&error) : ce_builtin_language(&g,gnu,&error)) ||
            (!have_preprocessed && !ce_read_file(&arena,path,&source,&error))) goto done;
        int entry = g.entries[0]; CeTree *tree;
        if (!strcmp(argv[1],"write")) {
            if (!ce_builtin_decode(&g,source,NULL,&arena,&tree,&error) ||
                !ce_tree_print(&g,tree,entry,stdout,&error)) goto done;
            fputc('\n',stdout);
        } else {
            if (!ce_dfa_prepare(&g,&dfa,&error) || !ce_lr_prepare(&g,entry,&lr,&error) ||
                !ce_dfa_lex(&g,&dfa,source,&tokens,&error) || !ce_lr_parse(&g,&lr,&tokens,&forest,&error) ||
                !ce_tree_from_forest(&g,&forest,&tokens,&arena,&tree,&error)) goto done;
            ce_tree_encode(&g,tree,stdout); fputc('\n',stdout);
        }
    }
    result = ferror(stdout) ? 1 : 0;
done:
    if (result && error.message[0]) {
        if (!(have_preprocessed && ce_pp_diagnostic(stderr,&preprocessed,&error)))
            fprintf(stderr,"%s: byte %zu: %s\n",path,error.offset,error.message);
    }
    if (have_preprocessed) ce_pp_result_free(&preprocessed);
    ce_lr_free(&lr); ce_dfa_free(&dfa); ce_forest_free(&forest); ce_tokens_free(&tokens);
    ce_catalog_free(&catalog); ce_grammar_free(&g); ce_arena_free(&arena); return result;
}

/* What the generated C should sit beside: the -o file, else the -MF file, else
   the current directory when the command writes an object, assembly or program
   there; NULL when it writes no file (-E, -M, -MM, -fsyntax-only). */
static const char *generated_beside(const char *output, const char *dependency, int argc, char **argv) {
    if (output) return output;
    if (dependency && strcmp(dependency,"-")) return dependency;
    for (int i = 0; i < argc; ++i)
        if (!strcmp(argv[i],"-E") || !strcmp(argv[i],"-M") || !strcmp(argv[i],"-MM") ||
            !strcmp(argv[i],"-fsyntax-only")) return NULL;
    return "output";
}

/* An opt-in reader census does not rewrite plain C or change the native
   compiler's status. Its preprocessing context is the compile command's. */
static void read_warnings(size_t n, char **arguments, bool gnu) {
    for (size_t i = 1; i < n; ++i) {
        if (option_value(arguments[i]) && i+1 < n) { ++i; continue; }
        if (!strcmp(arguments[i],"-E") || !strcmp(arguments[i],"-M") || !strcmp(arguments[i],"-MM")) return;
    }
    for (size_t i = 1; i < n; ++i) {
        if (option_value(arguments[i]) && i+1 < n) { ++i; continue; }
        if (!suffix(arguments[i],".c")) continue;
        CeArena arena = {0}; CeGrammar grammar = {0}; CeError error = {{0},0};
        CePPResult pp = {0}; CeDFA dfa = {0}; CeLR lr = {0};
        CeTokens tokens = {0}; CeForest forest = {.root=-1};
        CeSema sema = {0}; bool sema_ready = false; CeTree *tree = NULL;
        char **flags = ce_alloc(&arena,(n+4)*sizeof(*flags)); size_t k = 0;
        flags[k++] = "--cc"; flags[k++] = arguments[0];
        for (size_t j = 1; j < n; ++j) {
            const char *a = arguments[j];
            if (!strcmp(a,"-o") || !strcmp(a,"-MF") || !strcmp(a,"-MT") || !strcmp(a,"-MQ") ||
                !strcmp(a,"-L") || !strcmp(a,"-l") || !strcmp(a,"-Xlinker")) { if (j+1 < n) ++j; continue; }
            if (a[0] != '-' || !strcmp(a,"--") || !strncmp(a,"-Wl,",4)) continue;
            flags[k++] = arguments[j];
            if (option_value(a) && j+1 < n) flags[k++] = arguments[++j];
        }
        flags[k++] = arguments[i];
        CeText ce_builtin_CPPGNUGrammar(void);
        bool ok = ce_pp_run(ce_builtin_CPPGNUGrammar(),(int)k,flags,false,&pp,&error) &&
            ce_builtin_dialect(&grammar,gnu,pp.clang,&error) &&
            ce_dfa_prepare(&grammar,&dfa,&error) && ce_lr_prepare(&grammar,grammar.entries[0],&lr,&error) &&
            ce_dfa_lex(&grammar,&dfa,pp.text,&tokens,&error) &&
            ce_lr_parse(&grammar,&lr,&tokens,&forest,&error) &&
            ce_tree_from_forest(&grammar,&forest,&tokens,&arena,&tree,&error);
        const CeAbi *abi = NULL;
        const char *macros = (const char *)pp.predefined.data;
        if (ok && macros) {
            if (strstr(macros,"#define __x86_64__ 1\n") && strstr(macros,"#define __SIZEOF_POINTER__ 8\n")) {
                if (strstr(macros,"#define __SIZEOF_LONG__ 8\n")) abi = &ce_abi_x86_64_sysv;
                else if (strstr(macros,"#define __SIZEOF_LONG__ 4\n")) abi = &ce_abi_x86_64_windows;
            } else if (strstr(macros,"#define __i386__ 1\n") && strstr(macros,"#define __SIZEOF_POINTER__ 4\n")) abi = &ce_abi_i386_sysv;
        }
        if (ok && !abi) ok = ce_error(&error,0,"type census has no ABI model for the selected compiler target");
        if (ok) {
            sema_ready = ce_sema_init(&sema,&grammar,pp.text,&error);
            sema.clang = pp.clang; sema.abi = abi;
            ok = sema_ready && ce_bind(&sema,tree,&error) &&
                ce_sema_extents(&sema,tree,&tokens,&error) && ce_elaborate(&sema,tree,&error);
        }
        if (ok) fprintf(stderr,"cesta cc: read: %s: syntax and types admitted (%s)\n",arguments[i],abi->name);
        else {
            fprintf(stderr,"cesta cc: warning: reader did not admit %s: %s (native compilation continues)\n",
                    arguments[i],error.message);
            /* Origins include headers and macros, rather than a flattened TU offset. */
            if (pp.text.len) ce_pp_diagnostic(stderr,&pp,&error);
        }
        ce_forest_free(&forest); ce_tokens_free(&tokens); ce_lr_free(&lr); ce_dfa_free(&dfa);
        if (sema_ready) ce_sema_free(&sema);
        ce_grammar_free(&grammar); ce_pp_result_free(&pp); ce_arena_free(&arena);
    }
}

int ce_cc(int argc, char **argv) {
    CeArena arena = {0}; CeGrammar g = {0}; CeError error = {{0},0};
    ResponseArgs expanded = {.arena=&arena}; CeError response_error = {{0},0}; bool responses_ok = true;
    for (int i = 0; i < argc && responses_ok; ++i) responses_ok = response_expand(&expanded,argv[i],NULL,&response_error);
    bool reader = false;
    for (size_t i = 0; i < expanded.len; ++i) reader |= !strcmp(expanded.values[i],"--read-warnings");
    if (reader || compact_arguments(expanded.len,expanded.values) || compact_arguments((size_t)argc,argv)) {
        if (!responses_ok || expanded.len > INT_MAX) {
            fprintf(stderr,"cesta cc: %s\n",responses_ok ? "too many compiler arguments" : response_error.message);
            free(expanded.values); ce_arena_free(&arena); return 1;
        }
        argc = (int)expanded.len; argv = ce_alloc(&arena,expanded.len*sizeof(*argv));
        if (expanded.len) memcpy(argv,expanded.values,expanded.len*sizeof(*argv));
    }
    free(expanded.values);
    const char *compiler = getenv("CESTA_CC");
    const char *presentation = NULL;
    const char *dependency_path = NULL, *output_path = NULL;
    bool dependencies = false, dependency_only = false, stop_before_link = false;
    if (!compiler || !*compiler) compiler = "cc";
    char **arguments = ce_alloc(&arena,((size_t)argc+12)*sizeof(*arguments));
    unsigned char *input = ce_alloc(&arena,(size_t)argc+5);
    CeText *contents = ce_alloc(&arena,((size_t)argc+5)*sizeof(*contents));
    CeNativeOutput *native_outputs = ce_alloc(&arena,((size_t)argc+5)*sizeof(*native_outputs));
    char **identities = ce_alloc(&arena,((size_t)argc+5)*sizeof(*identities));
    CeText *authored = ce_alloc(&arena,((size_t)argc+5)*sizeof(*authored));
    size_t n = 1, sexprs = 0;
    bool gnu = true, standard = false, after_options = false;
    bool supported_standard = true;
    bool documents = false;
    bool native_authoring = false, ast_units = false;
    for (int i = 0; i < argc; ++i) {
        if (!after_options && !strcmp(argv[i],"--cc")) {
            if (++i == argc) { ce_error(&error,0,"--cc requires a compiler executable"); goto failed; }
            compiler = argv[i]; continue;
        }
        if (!after_options && !strcmp(argv[i],"--presentation")) {
            if (++i == argc) { ce_error(&error,0,"--presentation requires a versioned key"); goto failed; }
            presentation = argv[i]; continue;
        }
        if (!after_options && !strcmp(argv[i],"--read-warnings")) continue;
        size_t at = n; arguments[n++] = argv[i];
        if (!after_options && !strcmp(argv[i],"--")) { after_options = true; continue; }
        if (!after_options && !strncmp(argv[i],"-std=",5)) {
            standard = true; gnu = !strncmp(argv[i]+5,"gnu",3);
            supported_standard = !strcmp(argv[i]+5,"c11") || !strcmp(argv[i]+5,"gnu11") || !strcmp(argv[i]+5,"iso9899:2011");
        }
        if (!after_options) {
            if (!strcmp(argv[i],"-MD") || !strcmp(argv[i],"-MMD")) dependencies = true;
            if (!strcmp(argv[i],"-M") || !strcmp(argv[i],"-MM")) dependencies = dependency_only = true;
            if (!strcmp(argv[i],"-c") || !strcmp(argv[i],"-S") || !strcmp(argv[i],"-E")) stop_before_link = true;
            if (!strncmp(argv[i],"-MF",3) && argv[i][3]) dependency_path = argv[i]+3;
            if (!strcmp(argv[i],"-MF") && i+1 < argc) dependency_path = argv[i+1];
            if (!strcmp(argv[i],"-o") && i+1 < argc) output_path = argv[i+1];
            if (!strncmp(argv[i],"-o",2) && argv[i][2]) output_path = argv[i]+2;
        }
        if (!after_options && option_value(argv[i])) {
            if (i+1 == argc) { ce_error(&error,0,"compiler option requires its argument"); goto failed; }
            arguments[n++] = argv[++i]; continue;
        }
        if ((after_options || argv[i][0] != '-') && suffix(argv[i],".csexp")) {
            input[at] = 1; ++sexprs;
        } else if ((after_options || argv[i][0] != '-') && suffix(argv[i],".cesta")) {
            input[at] = 2; ++sexprs;
        }
    }
    arguments[0] = (char *)compiler;
    if (reader) read_warnings(n,arguments,gnu);
    char **generated_sources = ce_alloc(&arena,sexprs*sizeof(*generated_sources));
    char **original_sources = ce_alloc(&arena,sexprs*sizeof(*original_sources)); size_t source_count = 0;
    if (sexprs) {
        if (!ce_builtin_language(&g,gnu,&error)) goto failed;
        const char *cache = ce_generated_cache(&arena,generated_beside(output_path,dependency_path,argc,argv));
        char *directory = ce_alloc(&arena,strlen(cache)+1); strcpy(directory,cache);
        if (!directories(directory,&error)) goto failed;
        for (size_t i = 1; i < n; ++i) if (input[i]) {
            const char *source_path = arguments[i]; CeText source; CeTree *tree = NULL;
            if (!ce_read_file(&arena,source_path,&source,&error)) {
                fprintf(stderr,"%s: byte %zu: %s\n",source_path,error.offset,error.message); goto done_failure;
            }
            identities[i] = (char *)source_path; authored[i] = source;
            CeRouteStyle style = {.color=ce_route_color(STDERR_FILENO,n,arguments)};
            bool lossless = source_document(source);
            CeAtom *atom = NULL;
            if (!lossless && !ce_atom_read(&arena,source,&atom,&error)) {
                ce_report_error(stderr,source_path,source,error.offset,error.message,&style); goto done_failure;
            }
            if (atom && ce_wire_envelope(atom) && !ce_wire_unpack(&arena,atom,&atom,&error)) goto failed;
            bool native = (presentation && !strcmp(presentation,"native-c/1")) || (atom && ce_native_document(atom));
            if (!supported_standard && !native) { ce_error(&error,0,"Cesta's AST compiler profile supports -std=c11 or -std=gnu11"); goto failed; }
            if (lossless) {
                documents = true;
                if (!ce_source_bytes(&arena,source,&contents[i],&error)) goto failed;
            } else if (native) {
                native_authoring = true;
                if (!ce_native_prepare(&g,&arena,source,presentation,source_path,n,arguments,cache,&native_outputs[i],&error)) {
                    ce_report_error(stderr,source_path,source,error.offset,error.message,&style); goto done_failure;
                }
                contents[i] = native_outputs[i].text;
            } else if (!ce_builtin_decode_default(&g,source,presentation,input[i] == 2 ? "compact-c/2" : "ast/1",&arena,&tree,&error)) {
                ce_report_error(stderr,source_path,source,error.offset,error.message,&style); goto done_failure;
            }
            uint64_t hash = UINT64_C(14695981039346656037);
            for (const unsigned char *p = (const unsigned char *)source_path; *p; ++p) hash = (hash ^ *p)*UINT64_C(1099511628211);
            const char *base = strrchr(source_path,'/'); base = base ? base+1 : source_path;
            size_t base_len = strlen(base)-6, capacity = strlen(cache)+base_len+64;
            char *parent = ce_alloc(&arena,strlen(cache)+20);
            snprintf(parent,strlen(cache)+20,"%s/%016llx",cache,(unsigned long long)hash);
            if (!directories(parent,&error)) goto failed;
            char *path = ce_alloc(&arena,capacity);
            if (native) {
                uint64_t content = text_hash(UINT64_C(14695981039346656037),contents[i]);
                char *generation = ce_alloc(&arena,strlen(parent)+20);
                sprintf(generation,"%s/%016llx",parent,(unsigned long long)content);
                if (!directories(generation,&error)) goto failed;
                snprintf(path,capacity,"%s/%.*s.c",generation,(int)base_len,base);
                if (!publish_text(&arena,path,contents[i],&error)) goto failed;
            } else snprintf(path,capacity,"%s/%.*s.c",parent,(int)base_len,base);
            if (!lossless && !native && !ce_tree_emit_checked(&g,tree,g.entries[0],path,&error)) {
                ce_report_error(stderr,source_path,source,error.offset,error.message,&style); goto done_failure;
            }
            if (!lossless && !native) {
                /* The checked print fixes the C; the compiler reads it with #line
                   directives naming the authored source. */
                char *buffer = NULL; size_t length = 0; FILE *file = open_memstream(&buffer,&length);
                CeSourceMap map = {0}; CeLazyLexer lexer = {0};
                CeLineMarks marks = {.sorts=ce_native_line_sorts(&g,&arena),.source=source,.path=source_path,.map=&map};
                bool ok = file && ce_tree_print_marked(&g,&lexer,tree,g.entries[0],&marks,file,&error);
                if (file) { fputc('\n',file); if (fclose(file)) ok = false; }
                if (ok) {
                    contents[i] = ce_text(&arena,(const unsigned char *)buffer,length);
                    ce_native_map_copy(&arena,&map,&native_outputs[i]);
                    ok = publish_text(&arena,path,contents[i],&error);
                }
                free(buffer); ce_source_map_free(&map); ce_lazy_lexer_free(&lexer);
                if (!ok) goto failed;
                ast_units = true;
            }
            arguments[i] = path;
            generated_sources[source_count] = path; original_sources[source_count++] = (char *)source_path;
        }
        if (documents) {
            int result = source_document_compile(&arena,n,arguments,input,contents,identities,cache,compiler,standard,argc,argv,&error);
            if (result && error.message[0]) fprintf(stderr,"cesta cc: %s\n",error.message);
            ce_grammar_free(&g); ce_arena_free(&arena); return result;
        }
        if (!standard && !native_authoring) {
            memmove(arguments+2,arguments+1,(n-1)*sizeof(*arguments));
            memmove(input+2,input+1,n-1);
            memmove(identities+2,identities+1,(n-1)*sizeof(*identities));
            arguments[1] = "-std=gnu11"; ++n;
            input[1] = 0; identities[1] = NULL;
        }
    }
    arguments[n] = NULL;
    if (native_authoring) {
        const char *cache = ce_generated_cache(&arena,generated_beside(output_path,dependency_path,argc,argv));
        int result = native_authoring_compile(&arena,n,arguments,input,identities,native_outputs,authored,cache,&error);
        if (result && error.message[0]) fprintf(stderr,"cesta cc: %s\n",error.message);
        ce_grammar_free(&g); ce_arena_free(&arena); return result;
    }
    {
        const char *depfile = dependency_path ? dependency_path : dependency_only ? output_path : NULL;
        bool capture = dependencies && source_count &&
            ((depfile && !strcmp(depfile,"-")) || (dependency_only && !depfile));
        CeText captured; int result;
        if (capture) {
            result = ce_execute_stdout(arguments,&arena,&captured,&error);
            if (!dependency_write(&arena,captured,stdout,generated_sources,original_sources,source_count,&error)) result = 1;
        } else if (ast_units) {
            /* The compiler reads every unit in one command; its diagnostics are
               routed to whichever unit they name. */
            bool clang = compiler_is_clang(&arena,arguments[0]);
            CeRouteStyle style = {.color=ce_route_color(STDERR_FILENO,n,arguments)};
            arguments[n++] = clang ? "-fno-caret-diagnostics" : "-fno-diagnostics-show-caret";
            if (!clang) arguments[n++] = "-fdiagnostics-column-unit=byte";
            arguments[n++] = "-fdiagnostics-color=never";
            arguments[n] = NULL;
            CeRouteUnit *units = ce_alloc(&arena,n*sizeof(*units)); size_t nunits = 0;
            for (size_t i = 1; i < n; ++i) if (native_outputs[i].ntokens && identities[i])
                units[nunits++] = (CeRouteUnit){.identity=identities[i],.generated=arguments[i],
                                                .source=authored[i],.map=&native_outputs[i]};
            CeText messages = {NULL,0};
            result = ce_execute_capture(arguments,STDERR_FILENO,&arena,&messages,&error);
            if (messages.len) ce_route_diagnostics(messages,units,nunits,&style,stderr);
        } else result = ce_execute(arguments,&error);
        if (!result && dependencies && source_count) {
            if (depfile && strcmp(depfile,"-")) {
                if (!dependency_rewrite(&arena,depfile,generated_sources,original_sources,source_count,&error)) result = 1;
            } else if (!capture && !dependency_path) {
                for (size_t i = 0; i < source_count && !result; ++i) {
                    const char *base = strrchr(original_sources[i],'/'); base = base ? base+1 : original_sources[i];
                    const char *object = output_path ? output_path : base;
                    size_t length = strlen(object), stem = output_path ? length : length-6;
                    if (output_path) {
                        const char *dot = strrchr(object,'.');
                        if (dot && (!strrchr(object,'/') || dot > strrchr(object,'/'))) stem = (size_t)(dot-object);
                    }
                    char *path = ce_alloc(&arena,stem+3); memcpy(path,object,stem); memcpy(path+stem,".d",3);
                    if (!output_path && !stop_before_link) {
                        struct stat info;
                        bool found = false;
                        char *fallback = ce_alloc(&arena,stem+5);
                        memcpy(fallback,"a-",2); memcpy(fallback+2,path,stem+3);
                        const char *choices[] = {path,fallback};
                        for (size_t choice = 0; choice < 2 && !result; ++choice) if (!stat(choices[choice],&info)) {
                            found = true;
                            if (!dependency_rewrite(&arena,choices[choice],generated_sources,original_sources,source_count,&error)) result = 1;
                        }
                        if (!found) { ce_error(&error,0,"native compiler produced no default dependency file"); result = 1; }
                        continue;
                    }
                    if (!dependency_rewrite(&arena,path,generated_sources,original_sources,source_count,&error)) result = 1;
                }
            }
        }
        if (result && error.message[0]) fprintf(stderr,"cesta cc: %s\n",error.message);
        ce_grammar_free(&g); ce_arena_free(&arena); return result;
    }
failed:
    fprintf(stderr,"cesta cc: %s\n",error.message);
done_failure:
    ce_grammar_free(&g); ce_arena_free(&arena); return 1;
}

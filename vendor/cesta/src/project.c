#define _POSIX_C_SOURCE 200809L
#include "native.h"
#include "project.h"
#include "cesta.h"
#include "diagnose.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* A project document:
     (cesta project/1
       (compiler gcc) (out "build/cesta")
       (profile NAME (flags "…" …) (link "…" …))
       (target NAME (profile NAME) (sources "glob" …) (include "dir" …)
                    (define "NAME=VALUE" …) (flags "…" …) (link "…" …)
                    (kind executable|archive) (output "path")))
   Paths are relative to the project directory. Each source compiles through
   `cesta cc`, so .cesta units report authored positions and plain C and
   assembly reach the compiler unchanged. */

typedef struct { char **items; size_t len, capacity; } Words;

typedef struct {
    const char *name;
    Words flags, link, inputs;
} Profile;

typedef struct {
    const char *path;
    Words flags, includes, defines;
} Source;

typedef struct {
    const char *name, *profile, *output, *kind;
    Words sources, includes, defines, flags, link, depends, inputs, command, outputs, compile_requires;
    Source *source_options; size_t nsource_options;
    const CeAtom *atom;
} Target;

typedef struct {
    CeArena *arena;
    const char *path;
    CeText text;
    const char *compiler, *out;
    Profile *profiles; size_t nprofiles;
    Target *targets; size_t ntargets;
    Words *inputs, *compile_inputs;
    bool *edges, *selected;
    size_t *order;
    bool checked_hints, read_warnings;
} Project;

static void push(Words *w, char *word) {
    if (w->len+1 >= w->capacity) {
        w->capacity = w->capacity ? w->capacity*2 : 16;
        w->items = ce_resize(w->items,w->capacity,sizeof(*w->items));
    }
    w->items[w->len++] = word; w->items[w->len] = NULL;
}

static void words_free(Words *w) { free(w->items); *w = (Words){0}; }

static bool head(const CeAtom *a, const char *name) {
    return a->kind == CE_ATOM_LIST && a->len && a->items[0]->kind == CE_ATOM_SYMBOL && ce_is(a->items[0]->text,name);
}

static char *word(Project *p, const CeAtom *a) {
    char *s = ce_alloc(p->arena,a->text.len+1);
    memcpy(s,a->text.data,a->text.len); s[a->text.len] = 0; return s;
}

static bool fail(Project *p, const CeAtom *a, const char *message) {
    CeRouteStyle style = {.color=ce_route_color(STDERR_FILENO,0,NULL)};
    ce_report_error(stderr,p->path,p->text,a ? a->offset : 0,message,&style);
    return false;
}

/* (NAME "s" …): every operand a string. */
static bool strings(Project *p, const CeAtom *a, Words *out) {
    for (size_t i = 1; i < a->len; ++i) {
        if (a->items[i]->kind != CE_ATOM_TEXT) return fail(p,a->items[i],"expected a string");
        if (memchr(a->items[i]->text.data,0,a->items[i]->text.len))
            return fail(p,a->items[i],"project operands cannot contain NUL");
        push(out,word(p,a->items[i]));
    }
    return true;
}

static bool names(Project *p, const CeAtom *a, Words *out) {
    for (size_t i = 1; i < a->len; ++i) {
        if (a->items[i]->kind == CE_ATOM_LIST ||
            memchr(a->items[i]->text.data,0,a->items[i]->text.len))
            return fail(p,a->items[i],"expected a target name");
        push(out,word(p,a->items[i]));
    }
    return true;
}

/* (NAME symbol) or (NAME "string"): one operand. */
static bool single(Project *p, const CeAtom *a, const char **out) {
    if (a->len != 2 || a->items[1]->kind == CE_ATOM_LIST) return fail(p,a,"expected exactly one name or string");
    if (memchr(a->items[1]->text.data,0,a->items[1]->text.len))
        return fail(p,a->items[1],"project operands cannot contain NUL");
    *out = word(p,a->items[1]); return true;
}

static bool source_option(Project *p, Target *t, const CeAtom *a) {
    if (a->len < 2 || a->items[1]->kind != CE_ATOM_TEXT)
        return fail(p,a,"a source needs a literal path");
    if (memchr(a->items[1]->text.data,0,a->items[1]->text.len))
        return fail(p,a,"source paths cannot contain NUL");
    Source *s = &t->source_options[t->nsource_options++];
    s->path = word(p,a->items[1]);
    for (size_t i = 0; i+1 < t->nsource_options; ++i)
        if (!strcmp(s->path,t->source_options[i].path)) return fail(p,a,"duplicate source options");
    push(&t->sources,(char *)s->path);
    for (size_t i = 2; i < a->len; ++i) {
        const CeAtom *f = a->items[i];
        if (head(f,"flags")) { if (!strings(p,f,&s->flags)) return false; }
        else if (head(f,"include")) { if (!strings(p,f,&s->includes)) return false; }
        else if (head(f,"define")) { if (!strings(p,f,&s->defines)) return false; }
        else return fail(p,f,"a source has flags, include and define");
    }
    return true;
}

static bool read_project(Project *p) {
    CeAtom *doc; CeError error = {{0},0};
    if (!ce_atom_read(p->arena,p->text,&doc,&error)) return fail(p,NULL,error.message);
    if (!head(doc,"cesta") || doc->len < 2 || doc->items[1]->kind != CE_ATOM_SYMBOL || !ce_is(doc->items[1]->text,"project/1"))
        return fail(p,doc,"a project is (cesta project/1 …)");
    p->compiler = "cc"; p->out = "build/cesta";
    p->profiles = ce_alloc(p->arena,doc->len*sizeof(*p->profiles));
    p->targets = ce_alloc(p->arena,doc->len*sizeof(*p->targets));
    for (size_t i = 2; i < doc->len; ++i) {
        const CeAtom *a = doc->items[i];
        if (head(a,"compiler")) { if (!single(p,a,&p->compiler)) return false; }
        else if (head(a,"out")) { if (!single(p,a,&p->out)) return false; }
        else if (head(a,"profile") || head(a,"target")) {
            bool profile = head(a,"profile");
            if (a->len < 2 || a->items[1]->kind != CE_ATOM_SYMBOL) return fail(p,a,"a profile or target needs a name");
            const char *name = word(p,a->items[1]);
            if (profile) {
                for (size_t k = 0; k < p->nprofiles; ++k)
                    if (!strcmp(p->profiles[k].name,name)) return fail(p,a,"duplicate profile");
                Profile *pr = &p->profiles[p->nprofiles++]; *pr = (Profile){.name=name};
                for (size_t k = 2; k < a->len; ++k) {
                    const CeAtom *f = a->items[k];
                    if (head(f,"flags")) { if (!strings(p,f,&pr->flags)) return false; }
                    else if (head(f,"link")) { if (!strings(p,f,&pr->link)) return false; }
                    else if (head(f,"link-inputs")) { if (!strings(p,f,&pr->inputs)) return false; }
                    else return fail(p,f,"a profile has flags, link and link-inputs");
                }
            } else {
                for (size_t k = 0; k < p->ntargets; ++k)
                    if (!strcmp(p->targets[k].name,name)) return fail(p,a,"duplicate target");
                Target *t = &p->targets[p->ntargets++]; *t = (Target){.name=name,.kind="executable",.atom=a};
                t->source_options = ce_alloc(p->arena,a->len*sizeof(*t->source_options));
                for (size_t k = 2; k < a->len; ++k) {
                    const CeAtom *f = a->items[k];
                    bool ok = head(f,"profile") ? single(p,f,&t->profile) :
                              head(f,"output") ? single(p,f,&t->output) :
                              head(f,"kind") ? single(p,f,&t->kind) :
                              head(f,"sources") ? strings(p,f,&t->sources) :
                              head(f,"source") ? source_option(p,t,f) :
                              head(f,"include") ? strings(p,f,&t->includes) :
                              head(f,"define") ? strings(p,f,&t->defines) :
                              head(f,"flags") ? strings(p,f,&t->flags) :
                              head(f,"link") ? strings(p,f,&t->link) :
                              head(f,"depends") ? names(p,f,&t->depends) :
                              head(f,"link-inputs") ? strings(p,f,&t->inputs) :
                              head(f,"compile-inputs") ? strings(p,f,&t->compile_requires) :
                              head(f,"inputs") ? strings(p,f,&t->inputs) :
                              head(f,"outputs") ? strings(p,f,&t->outputs) :
                              head(f,"command") ? strings(p,f,&t->command) :
                              fail(p,f,"unknown target field");
                    if (!ok) return false;
                }
                if (!strcmp(t->kind,"generator")) {
                    if (t->output) push(&t->outputs,(char *)t->output);
                    if (!t->command.len || !t->outputs.len || t->sources.len || t->profile ||
                        t->flags.len || t->includes.len || t->defines.len || t->link.len || t->compile_requires.len)
                        return fail(p,a,"a generator needs command and outputs, with inputs and depends only");
                    t->output = t->outputs.items[0];
                } else {
                    if (strcmp(t->kind,"executable") && strcmp(t->kind,"archive"))
                        return fail(p,a,"a target's kind is executable, archive or generator");
                    if (!t->sources.len) return fail(p,a,"a target needs sources");
                    if (t->outputs.len || t->command.len) return fail(p,a,"command and outputs belong to generators");
                }
            }
        } else return fail(p,a,"a project has compiler, out, profile and target");
    }
    for (size_t i = 0; i < p->ntargets; ++i) {
        Target *t = &p->targets[i];
        if (!t->profile) continue;
        bool found = false;
        for (size_t k = 0; k < p->nprofiles; ++k) found |= !strcmp(p->profiles[k].name,t->profile);
        if (!found) return fail(p,t->atom,"the target names an undeclared profile");
    }
    return true;
}

static const Profile *profile_of(const Project *p, const Target *t) {
    for (size_t k = 0; t->profile && k < p->nprofiles; ++k)
        if (!strcmp(p->profiles[k].name,t->profile)) return &p->profiles[k];
    return NULL;
}

static char *join(CeArena *arena, const char *a, const char *b, const char *c) {
    size_t n = strlen(a)+strlen(b)+strlen(c)+1;
    char *s = ce_alloc(arena,n); snprintf(s,n,"%s%s%s",a,b,c); return s;
}

static bool make_directories(const char *path) {
    char *copy = strdup(path); bool ok = copy != NULL;
    for (char *s = copy ? copy+1 : NULL; ok && s && *s; ++s) if (*s == '/') {
        *s = 0;
        if (mkdir(copy,0777) && errno != EEXIST) ok = false;
        *s = '/';
    }
    if (ok && mkdir(copy,0777) && errno != EEXIST) ok = false;
    free(copy); return ok;
}

static char *parent(CeArena *arena, const char *path) {
    const char *slash = strrchr(path,'/');
    if (!slash) return ".";
    char *s = ce_alloc(arena,(size_t)(slash-path)+1);
    memcpy(s,path,(size_t)(slash-path)); s[slash-path] = 0; return s;
}

static uint64_t command_hash(char *const *argv) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (; *argv; ++argv) {
        for (const unsigned char *c = (const unsigned char *)*argv; *c; ++c) h = (h ^ *c)*UINT64_C(1099511628211);
        h = (h ^ 0xff)*UINT64_C(1099511628211);
    }
    return h;
}

static bool mtime(const char *path, struct timespec *out) {
    struct stat st;
    if (stat(path,&st)) return false;
    *out = st.st_mtim; return true;
}

static bool newer(struct timespec a, struct timespec b) {
    return a.tv_sec > b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec > b.tv_nsec);
}

/* The recorded command for `output` equals `argv`. */
static bool same_command(CeArena *arena, const char *output, char *const *argv) {
    CeText text; CeError ignored = {{0},0};
    if (!ce_read_file(arena,join(arena,output,".cmd",""),&text,&ignored)) return false;
    char expected[32]; snprintf(expected,sizeof expected,"%016llx\n",(unsigned long long)command_hash(argv));
    return text.len == strlen(expected) && !memcmp(text.data,expected,text.len);
}

static void record_command(CeArena *arena, const char *output, char *const *argv) {
    FILE *f = fopen(join(arena,output,".cmd",""),"wb");
    if (!f) return;
    fprintf(f,"%016llx\n",(unsigned long long)command_hash(argv)); fclose(f);
}

/* Every prerequisite in the first rule of a make dependency file is no newer
   than `object`. A missing file or prerequisite means out of date. */
static bool read_dependencies(CeArena *arena, const char *depfile, Words *names) {
    CeText text; CeError ignored = {{0},0};
    if (!ce_read_file(arena,depfile,&text,&ignored)) return false;
    size_t i = 0;
    while (i < text.len && !(text.data[i] == ':' && (i+1 == text.len || text.data[i+1] == ' ' || text.data[i+1] == '\n'))) {
        if (text.data[i] == '\\') ++i;
        ++i;
    }
    if (i >= text.len) return false;
    ++i;
    char *name = ce_alloc(arena,text.len+1); size_t n = 0;
    for (; i <= text.len; ++i) {
        unsigned char c = i < text.len ? text.data[i] : '\n';
        if (c == '\\' && i+1 < text.len && text.data[i+1] == '\n') { ++i; c = ' '; }
        else if (c == '\\' && i+1 < text.len) { name[n++] = (char)text.data[++i]; continue; }
        else if (c == '$' && i+1 < text.len && text.data[i+1] == '$') { name[n++] = '$'; ++i; continue; }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (n) {
                name[n] = 0;
                char *copy = ce_alloc(arena,n+1); memcpy(copy,name,n+1); push(names,copy);
                n = 0;
            }
            if (c == '\n') break;
        } else name[n++] = (char)c;
    }
    return true;
}

static bool dependencies_current(CeArena *arena, const char *depfile, struct timespec object) {
    Words names = {0}; bool current = read_dependencies(arena,depfile,&names) && names.len != 0;
    for (size_t i = 0; current && i < names.len; ++i) {
        struct timespec t;
        current = mtime(names.items[i],&t) && !newer(t,object);
    }
    words_free(&names); return current;
}

typedef struct { pid_t pid; const char *output; char **identity; } Job;
static bool suffix_is(const char *path, const char *suffix);

static pid_t start(char **argv) {
    pid_t pid;
    int status = posix_spawnp(&pid,argv[0],NULL,NULL,argv,environ);
    if (status) { fprintf(stderr,"cesta build: cannot start %s: %s\n",argv[0],strerror(status)); return -1; }
    return pid;
}

static int run_with_input(char **argv, const char *path) {
    int fd = open(path,O_RDONLY|O_CLOEXEC);
    if (fd < 0) return 1;
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    bool initialized = error == 0;
    if (!error) error = posix_spawn_file_actions_adddup2(&actions,fd,STDIN_FILENO);
    if (!error && fd != STDIN_FILENO) error = posix_spawn_file_actions_addclose(&actions,fd);
    pid_t pid = -1;
    if (!error) error = posix_spawnp(&pid,argv[0],&actions,NULL,argv,environ);
    if (initialized) posix_spawn_file_actions_destroy(&actions);
    close(fd);
    if (error) { fprintf(stderr,"cesta build: cannot start %s: %s\n",argv[0],strerror(error)); return 1; }
    int status = 0;
    pid_t waited;
    do waited = waitpid(pid,&status,0); while (waited < 0 && errno == EINTR);
    return waited == pid && WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : 1;
}

static void echo(char *const *argv) {
    for (char *const *a = argv; *a; ++a) fprintf(stderr,"%s%s",a == argv ? "" : " ",*a);
    fputc('\n',stderr);
}

typedef struct { const char *self, *compiler; size_t jobs; bool verbose; } Options;

/* The target's sources: its patterns expanded in order, without repeats. */
static bool target_sources(Project *p, const Target *t, Words *sources) {
    for (size_t i = 0; i < t->sources.len; ++i) {
        glob_t g; int status = glob(t->sources.items[i],0,NULL,&g);
        if (status == GLOB_NOMATCH) {
            fprintf(stderr,"%s: target %s: no source matches %s\n",p->path,t->name,t->sources.items[i]);
            words_free(sources); return false;
        }
        if (status) { fprintf(stderr,"cesta build: cannot expand %s\n",t->sources.items[i]); words_free(sources); return false; }
        for (size_t k = 0; k < g.gl_pathc; ++k) {
            bool seen = false;
            for (size_t m = 0; m < sources->len; ++m) seen |= !strcmp(sources->items[m],g.gl_pathv[k]);
            if (!seen) push(sources,strcpy(ce_alloc(p->arena,strlen(g.gl_pathv[k])+1),g.gl_pathv[k]));
        }
        globfree(&g);
    }
    return true;
}

/* Compile flags common to every source of the target. */
static void compile_flags(Project *p, const Target *t, Words *out) {
    const Profile *profile = profile_of(p,t);
    for (size_t k = 0; profile && k < profile->flags.len; ++k) push(out,profile->flags.items[k]);
    for (size_t k = 0; k < t->flags.len; ++k) push(out,t->flags.items[k]);
    for (size_t k = 0; k < t->includes.len; ++k) push(out,join(p->arena,"-I",t->includes.items[k],""));
    for (size_t k = 0; k < t->defines.len; ++k) push(out,join(p->arena,"-D",t->defines.items[k],""));
    if (p->checked_hints) {
        push(out,"-UCESTA_CHECKED_OPTIMIZE");
        push(out,"-DCESTA_CHECKED_OPTIMIZE=1");
    }
}

static void source_flags(Project *p, const Target *t, const char *path, Words *out) {
    compile_flags(p,t,out);
    for (size_t i = 0; i < t->nsource_options; ++i) {
        const Source *s = &t->source_options[i];
        if (strcmp(path,s->path)) continue;
        for (size_t k = 0; k < s->flags.len; ++k) push(out,s->flags.items[k]);
        for (size_t k = 0; k < s->includes.len; ++k) push(out,join(p->arena,"-I",s->includes.items[k],""));
        for (size_t k = 0; k < s->defines.len; ++k) push(out,join(p->arena,"-D",s->defines.items[k],""));
    }
}

static const char *target_output(Project *p, const Target *t) {
    const char *directory = join(p->arena,p->out,"/",t->name);
    return t->output ? t->output :
        join(p->arena,directory,"/",!strcmp(t->kind,"archive") ? join(p->arena,"lib",t->name,".a") : t->name);
}

static size_t output_count(const Target *t) { return t->outputs.len ? t->outputs.len : 1; }

static const char *output_at(Project *p, const Target *t, size_t i) {
    return t->outputs.len ? t->outputs.items[i] : target_output(p,t);
}

/* Lexical project paths also work before a generated prerequisite exists. */
static char *normal_path(CeArena *arena, const char *path) {
    size_t length = strlen(path), count = 0;
    char *copy = join(arena,path,"","");
    char **parts = ce_alloc(arena,(length+1)*sizeof(*parts));
    bool absolute = path[0] == '/';
    char *state = NULL;
    for (char *part = strtok_r(copy,"/",&state); part; part = strtok_r(NULL,"/",&state)) {
        if (!strcmp(part,".")) continue;
        if (!strcmp(part,"..")) {
            if (count && strcmp(parts[count-1],"..")) { --count; continue; }
            if (absolute) continue;
        }
        parts[count++] = part;
    }
    char *out = ce_alloc(arena,length+2); size_t used = 0;
    if (absolute) out[used++] = '/';
    for (size_t i = 0; i < count; ++i) {
        if (i) out[used++] = '/';
        size_t n = strlen(parts[i]); memcpy(out+used,parts[i],n); used += n;
    }
    if (!used) out[used++] = '.';
    out[used] = 0;
    return out;
}

static void unique_path(Project *p, Words *words, const char *path) {
    char *normalized = normal_path(p->arena,path);
    for (size_t i = 0; i < words->len; ++i)
        if (!strcmp(words->items[i],normalized)) return;
    push(words,normalized);
}

/* Compiler and linker command files share the cc response reader. Inputs
   hidden by an unrecognised linker option can still be declared explicitly. */
static bool command_inputs(Project *p, const Target *t, const Words *flags, Words *out, CeNativeArguments *expanded) {
    CeError error = {{0},0};
    if (!ce_native_arguments(p->arena,flags->len,flags->items,expanded,&error)) return fail(p,t->atom,error.message);
    for (size_t i = 0; i < expanded->nfiles; ++i) unique_path(p,out,(const char *)expanded->files[i].data);
    return true;
}

static bool link_inputs(Project *p, const Target *t, const Words *flags, Words *out) {
    CeNativeArguments driver = {0}, linker = {0};
    if (!command_inputs(p,t,flags,out,&driver)) return false;
    Words tokens = {0};
    for (size_t i = 0; i < driver.nwords; ++i) {
        const char *arg = driver.words[i];
        if (!strcmp(arg,"-Xlinker")) {
            if (++i == driver.nwords) { words_free(&tokens); return fail(p,t->atom,"-Xlinker needs an operand"); }
            push(&tokens,driver.words[i]);
        } else if (!strncmp(arg,"-Wl,",4)) {
            char *copy = join(p->arena,arg+4,"",""); char *state = NULL;
            for (char *part = strtok_r(copy,",",&state); part; part = strtok_r(NULL,",",&state))
                push(&tokens,part);
        } else push(&tokens,driver.words[i]);
    }
    if (!command_inputs(p,t,&tokens,out,&linker)) { words_free(&tokens); return false; }
    words_free(&tokens);
    tokens.items = linker.words; tokens.len = linker.nwords;
    const char *file_options[] = {"-T","--script","--version-script","--dynamic-list",
                                  "--retain-symbols-file","--just-symbols","-R","-plugin","--plugin"};
    const char *value_options[] = {"-L","-l","-e","--entry","-u","--undefined",
        "-z","-m","--emulation","--defsym","-framework","-rpath","--rpath",
        "-rpath-link","--rpath-link","-soname","--soname","-h","-Map","--Map",
        "-o","--output","-O","--hash-style","--dynamic-linker","-I","-b",
        "--format","--oformat","--section-start","--pack-dyn-relocs",
        "-plugin-opt","--plugin-opt","--dependency-file","--wrap"};
    for (size_t i = 0; i < tokens.len; ++i) {
        const char *arg = tokens.items[i]; bool consumed = false;
        for (size_t k = 0; k < sizeof(file_options)/sizeof(*file_options); ++k) {
            size_t n = strlen(file_options[k]);
            if (!strcmp(arg,file_options[k])) {
                if (i+1 == tokens.len) return fail(p,t->atom,"a linker file option needs an operand");
                unique_path(p,out,tokens.items[++i]); consumed = true; break;
            }
            if (!strncmp(arg,file_options[k],n) && arg[n] == '=') {
                unique_path(p,out,arg+n+1); consumed = true; break;
            }
        }
        if (consumed) continue;
        if (!strncmp(arg,"-T",2) && arg[2]) { unique_path(p,out,arg+2); continue; }
        for (size_t k = 0; k < sizeof(value_options)/sizeof(*value_options); ++k)
            if (!strcmp(arg,value_options[k])) {
                if (i+1 < tokens.len) ++i;
                consumed = true; break;
            }
        if (consumed) continue;
        if (arg[0] == '@') unique_path(p,out,arg+1);
        else if (arg[0] != '-') unique_path(p,out,arg);
    }
    return true;
}

static void compile_flags(Project *p, const Target *t, Words *args);

/* One graph supplies prerequisite closure, deterministic build order, link
   currentness, and the rules emitted for an exported C project. */
static bool project_graph(Project *p) {
    size_t n = p->ntargets;
    if (n && n > SIZE_MAX/n) return fail(p,NULL,"too many target dependencies");
    p->inputs = ce_alloc(p->arena,n*sizeof(*p->inputs));
    p->compile_inputs = ce_alloc(p->arena,n*sizeof(*p->compile_inputs));
    p->edges = ce_alloc(p->arena,n*n*sizeof(*p->edges));
    p->selected = ce_alloc(p->arena,n*sizeof(*p->selected));
    p->order = ce_alloc(p->arena,n*sizeof(*p->order));
    for (size_t i = 0; i < n; ++i) {
        Target *t = &p->targets[i]; const Profile *profile = profile_of(p,t);
        Words flags = {0}; CeNativeArguments expanded = {0};
        compile_flags(p,t,&flags);
        bool flags_ok = command_inputs(p,t,&flags,&p->compile_inputs[i],&expanded);
        words_free(&flags);
        if (!flags_ok) return false;
        for (size_t k = 0; k < t->compile_requires.len; ++k)
            unique_path(p,&p->compile_inputs[i],t->compile_requires.items[k]);
        for (size_t s = 0; s < t->nsource_options; ++s) {
            source_flags(p,t,t->source_options[s].path,&flags);
            flags_ok = command_inputs(p,t,&flags,&p->compile_inputs[i],&expanded);
            words_free(&flags);
            if (!flags_ok) return false;
        }
        for (size_t oi = 0; oi < output_count(t); ++oi) {
        const char *output = normal_path(p->arena,output_at(p,t,oi));
        for (size_t j = 0; j <= i; ++j)
            for (size_t oj = 0; oj < output_count(&p->targets[j]); ++oj) {
                if (j == i && oj >= oi) break;
                if (!strcmp(output,normal_path(p->arena,output_at(p,&p->targets[j],oj))))
                    return fail(p,t->atom,"targets must have distinct output paths");
            }
        /* Each target's directory holds its objects, dependency and command
           records; an output there would replace or be replaced by them. */
        for (size_t j = 0; j < n; ++j) {
            const char *directory = normal_path(p->arena,join(p->arena,p->out,"/",p->targets[j].name));
            const char *objects = normal_path(p->arena,join(p->arena,directory,"/","obj"));
            size_t length = strlen(objects);
            if (!strcmp(output,directory) ||
                (!strncmp(output,objects,length) && (!output[length] || output[length] == '/')))
                return fail(p,t->atom,"a target's output cannot be a target's build directory or lie within its object directory");
        }
        }
        for (size_t j = 0; j < t->depends.len; ++j) {
            size_t k;
            for (k = 0; k < n; ++k) if (!strcmp(t->depends.items[j],p->targets[k].name)) break;
            if (k == n) return fail(p,t->atom,"target depends on an undeclared target");
            p->edges[i*n+k] = true;
            unique_path(p,&p->inputs[i],target_output(p,&p->targets[k]));
        }
        if (profile) {
            if (strcmp(t->kind,"archive") && !link_inputs(p,t,&profile->link,&p->inputs[i])) return false;
            for (size_t k = 0; k < profile->inputs.len; ++k) unique_path(p,&p->inputs[i],profile->inputs.items[k]);
        }
        if (strcmp(t->kind,"archive") && !link_inputs(p,t,&t->link,&p->inputs[i])) return false;
        for (size_t k = 0; k < t->inputs.len; ++k) unique_path(p,&p->inputs[i],t->inputs.items[k]);
        const Words *dependencies[] = {&p->inputs[i], &p->compile_inputs[i]};
        for (size_t group = 0; group < sizeof dependencies/sizeof *dependencies; ++group)
        for (size_t k = 0; k < dependencies[group]->len; ++k)
            for (size_t j = 0; j < n; ++j)
                for (size_t oj = 0; oj < output_count(&p->targets[j]); ++oj)
                    if (!strcmp(dependencies[group]->items[k],normal_path(p->arena,output_at(p,&p->targets[j],oj))))
                        p->edges[i*n+j] = true;
        /* Literal generated sources imply their producer. Generated headers
           and globs name the generator explicitly with depends. */
        for (size_t k = 0; k < t->sources.len; ++k)
            for (size_t j = 0; j < n; ++j)
                for (size_t oj = 0; oj < output_count(&p->targets[j]); ++oj)
                    if (!strcmp(normal_path(p->arena,t->sources.items[k]),
                                normal_path(p->arena,output_at(p,&p->targets[j],oj))))
                        p->edges[i*n+j] = true;
    }
    bool *done = ce_alloc(p->arena,n*sizeof(*done));
    for (size_t position = 0; position < n; ++position) {
        size_t ready = n;
        for (size_t i = 0; i < n && ready == n; ++i) if (!done[i]) {
            bool blocked = false;
            for (size_t j = 0; j < n; ++j) blocked |= p->edges[i*n+j] && !done[j];
            if (!blocked) ready = i;
        }
        if (ready == n) return fail(p,NULL,"target dependency cycle");
        done[ready] = true; p->order[position] = ready;
    }
    return true;
}

static bool select_targets(Project *p, char **wanted, size_t count) {
    for (size_t i = 0; i < p->ntargets; ++i) p->selected[i] = !count;
    for (size_t k = 0; k < count; ++k) {
        size_t i;
        for (i = 0; i < p->ntargets; ++i) if (!strcmp(p->targets[i].name,wanted[k])) break;
        if (i == p->ntargets) {
            fprintf(stderr,"cesta build: %s declares no target %s\n",p->path,wanted[k]); return false;
        }
        p->selected[i] = true;
    }
    for (size_t k = p->ntargets; k > 0; --k) {
        size_t i = p->order[k-1];
        if (p->selected[i]) for (size_t j = 0; j < p->ntargets; ++j)
            if (p->edges[i*p->ntargets+j]) p->selected[j] = true;
    }
    return true;
}

static int build_generator(Project *p, const Target *t, const Options *o) {
    const char *stamp = join(p->arena,t->output,".cmd","");
    struct timespec built = {0};
    bool current = mtime(stamp,&built) && same_command(p->arena,t->output,t->command.items);
    for (size_t i = 0; current && i < p->inputs[t-p->targets].len; ++i) {
        struct timespec input;
        current = mtime(p->inputs[t-p->targets].items[i],&input) && !newer(input,built);
    }
    for (size_t i = 0; i < t->outputs.len; ++i) {
        const char *output = t->outputs.items[i]; struct stat st;
        current = current && !stat(output,&st) && S_ISREG(st.st_mode) && !newer(st.st_mtim,built);
        if (!make_directories(parent(p->arena,output))) return 1;
    }
    if (current) { fprintf(stderr,"cesta build: %s: generator up to date\n",t->name); return 0; }
    /* Invalidate the success record before a command can partly write outputs. */
    FILE *record = fopen(stamp,"wb");
    if (!record || fclose(record)) { if (record) fprintf(stderr,"cesta build: cannot invalidate %s\n",stamp); return 1; }
    if (o->verbose) echo(t->command.items);
    pid_t pid = start(t->command.items); int status = 0; pid_t waited = -1;
    if (pid >= 0) do waited = waitpid(pid,&status,0); while (waited < 0 && errno == EINTR);
    if (waited != pid || pid < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr,"cesta build: %s: generator failed\n",t->name); return 1;
    }
    for (size_t i = 0; i < t->outputs.len; ++i) {
        struct stat st;
        if (stat(t->outputs.items[i],&st) || !S_ISREG(st.st_mode)) {
            fprintf(stderr,"cesta build: %s: generator did not produce %s\n",t->name,t->outputs.items[i]); return 1;
        }
    }
    record_command(p->arena,t->output,t->command.items);
    fprintf(stderr,"cesta build: %s: generated %zu outputs\n",t->name,t->outputs.len);
    return 0;
}

/* Compile every out-of-date source of the target, at most `jobs` at a time;
   then link or archive when needed. */
static int build_target(Project *p, const Target *t, const Options *o) {
    CeArena *arena = p->arena;
    const Words *required[] = {&p->compile_inputs[t-p->targets], &p->inputs[t-p->targets]};
    for (size_t group = 0; group < sizeof required/sizeof *required; ++group)
        for (size_t i = 0; i < required[group]->len; ++i) {
            const char *path = required[group]->items[i]; struct stat info;
            FILE *file = !stat(path,&info) && S_ISREG(info.st_mode) ? fopen(path,"rb") : NULL;
            if (!file) { fprintf(stderr,"cesta build: unreadable command input: %s\n",path); return 1; }
            fclose(file);
        }
    if (!strcmp(t->kind,"generator")) return build_generator(p,t,o);
    const Profile *profile = profile_of(p,t);
    const char *directory = join(arena,p->out,"/",t->name);
    const char *output = target_output(p,t);
    Words sources = {0}, objects = {0};
    if (!target_sources(p,t,&sources)) return 1;
    size_t compiled = 0, current = 0, failures = 0;
    Job *running = ce_alloc(arena,(o->jobs ? o->jobs : 1)*sizeof(*running)); size_t nrunning = 0;
    for (size_t i = 0; i <= sources.len; ++i) {
        /* Wait while the pool is full, and at the end until it drains. */
        while (nrunning && (nrunning == o->jobs || i == sources.len)) {
            int status; pid_t pid = waitpid(-1,&status,0);
            if (pid < 0) { if (errno == EINTR) continue; break; }
            for (size_t k = 0; k < nrunning; ++k) if (running[k].pid == pid) {
                bool ok = WIFEXITED(status) && !WEXITSTATUS(status);
                if (ok) record_command(arena,running[k].output,running[k].identity);
                else { ++failures; unlink(running[k].output); }
                running[k] = running[--nrunning]; break;
            }
        }
        if (i == sources.len) break;
        const char *source = sources.items[i];
        const char *object = join(arena,directory,"/obj/",join(arena,source,".o",""));
        const char *depfile = join(arena,object,".d","");
        push(&objects,(char *)object);
        Words cc = {0};
        push(&cc,(char *)o->self); push(&cc,"cc"); push(&cc,"--cc"); push(&cc,(char *)o->compiler);
        if (p->read_warnings) push(&cc,"--read-warnings");
        source_flags(p,t,source,&cc);
        push(&cc,"-MMD"); push(&cc,"-MF"); push(&cc,(char *)depfile);
        push(&cc,"-c"); push(&cc,(char *)source); push(&cc,"-o"); push(&cc,(char *)object);
        struct timespec built;
        struct timespec generator;
        bool uses_cesta = p->read_warnings || suffix_is(source,".cesta") || suffix_is(source,".csexp");
        /* Plain inputs delegate unchanged: cache their native command. An
           authored input or census additionally depends on this Cesta tool. */
        char **identity = uses_cesta ? cc.items : cc.items+3;
        bool response_current = true;
        if (mtime(object,&built)) for (size_t k = 0; k < p->compile_inputs[t-p->targets].len; ++k) {
            struct timespec input;
            response_current &= mtime(p->compile_inputs[t-p->targets].items[k],&input) && !newer(input,built);
        }
        if (mtime(object,&built) && same_command(arena,object,identity) &&
            (!uses_cesta || (mtime(o->self,&generator) && !newer(generator,built))) &&
            response_current && dependencies_current(arena,depfile,built)) {
            ++current; words_free(&cc); continue;
        }
        if (!make_directories(parent(arena,object))) {
            fprintf(stderr,"cesta build: cannot create %s\n",parent(arena,object)); ++failures; words_free(&cc); continue;
        }
        if (o->verbose) echo(cc.items);
        char **argv = ce_alloc(arena,(cc.len+1)*sizeof(*argv));
        memcpy(argv,cc.items,(cc.len+1)*sizeof(*argv)); words_free(&cc);
        pid_t pid = start(argv);
        if (pid < 0) { ++failures; continue; }
        running[nrunning++] = (Job){pid,object,uses_cesta ? argv : argv+3}; ++compiled;
    }
    int result = failures ? 1 : 0;
    if (!result) {
        Words link = {0};
        bool archive = !strcmp(t->kind,"archive");
        if (archive) { push(&link,"ar"); push(&link,"rcs"); push(&link,(char *)output); }
        else push(&link,(char *)o->compiler);
        for (size_t k = 0; k < objects.len; ++k) push(&link,objects.items[k]);
        if (!archive) {
            for (size_t k = 0; profile && k < profile->link.len; ++k) push(&link,profile->link.items[k]);
            for (size_t k = 0; k < t->link.len; ++k) push(&link,t->link.items[k]);
            push(&link,"-o"); push(&link,(char *)output);
        }
        struct timespec linked; bool needed = !mtime(output,&linked) || !same_command(arena,output,link.items);
        for (size_t k = 0; !needed && k < objects.len; ++k) {
            struct timespec t2;
            needed = !mtime(objects.items[k],&t2) || newer(t2,linked);
        }
        const Words *inputs = &p->inputs[(size_t)(t-p->targets)];
        for (size_t k = 0; !needed && k < inputs->len; ++k) {
            struct timespec t2;
            needed = !mtime(inputs->items[k],&t2) || newer(t2,linked);
        }
        if (needed) {
            if (!make_directories(parent(arena,output))) { fprintf(stderr,"cesta build: cannot create %s\n",parent(arena,output)); result = 1; }
            else {
                if (archive) unlink(output);
                if (o->verbose) echo(link.items);
                pid_t pid = start(link.items); int status = 0;
                if (pid < 0) result = 1;
                else {
                    while (waitpid(pid,&status,0) < 0 && errno == EINTR) {}
                    result = WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : 1;
                }
                if (!result) record_command(arena,output,link.items);
            }
        }
        fprintf(stderr,"cesta build: %s: %zu compiled, %zu up to date; %s %s\n",t->name,compiled,current,
                result ? "failed" : needed ? (archive ? "archived" : "linked") : "current",output);
        words_free(&link);
    } else fprintf(stderr,"cesta build: %s: %zu of %zu sources failed\n",t->name,failures,sources.len);
    words_free(&sources); words_free(&objects);
    return result;
}

static bool copy_file(const char *from, const char *to) {
    FILE *in = fopen(from,"rb");
    if (!in) return false;
    char *slash = strrchr(to,'/');
    if (slash) { *slash = 0; bool made = make_directories(to); *slash = '/'; if (!made) { fclose(in); return false; } }
    FILE *out = fopen(to,"wb");
    if (!out) { fclose(in); return false; }
    char buffer[65536]; size_t n; bool ok = true;
    while ((n = fread(buffer,1,sizeof buffer,in))) ok &= fwrite(buffer,1,n,out) == n;
    struct stat st;
    ok &= !ferror(in) && !fstat(fileno(in),&st);
    if (ok) ok = !fchmod(fileno(out),st.st_mode & 0777);
    fclose(in); ok &= fclose(out) == 0; return ok;
}

static bool is_file(const char *path) { struct stat st; return !stat(path,&st) && S_ISREG(st.st_mode); }

/* A word for a shell command inside a make recipe. */
static void make_word(FILE *f, const char *word) {
    fputc('\'',f);
    for (const char *c = word; *c; ++c) {
        if (*c == '\'') fputs("'\\''",f);
        else if (*c == '$') fputs("$$",f);
        else fputc(*c,f);
    }
    fputc('\'',f);
}

static void make_path(FILE *f, const char *path) {
    for (const char *c = path; *c; ++c) {
        if (*c == '$') fputc('$',f);
        else if (strchr(" #:\t\\%",*c)) fputc('\\',f);
        fputc(*c,f);
    }
}

static bool local_export_path(Project *p, const char *path) {
    const char *normalized = normal_path(p->arena,path);
    if (!strcmp(normalized,"GNUmakefile"))
        return fail(p,NULL,"export reserves GNUmakefile for its build rules");
    return (normalized[0] != '/' && strcmp(normalized,"..") && strncmp(normalized,"../",3) &&
            !strchr(normalized,'\n') && !strchr(normalized,'\r')) ||
           fail(p,NULL,"exported sources and outputs must stay within the project tree");
}

static bool target_file(Project *p, const char *path) {
    const char *normalized = normal_path(p->arena,path);
    for (size_t i = 0; i < p->ntargets; ++i)
        if (!strcmp(normalized,normal_path(p->arena,target_output(p,&p->targets[i])))) return true;
    return false;
}

static bool copy_input(Project *p, const char *into, const char *path) {
    /* Absolute SDK inputs remain external compiler/linker dependencies. */
    if (path[0] == '/' || target_file(p,path)) return true;
    if (!local_export_path(p,path)) return false;
    const char *normalized = normal_path(p->arena,path);
    if (!is_file(normalized) || !copy_file(normalized,join(p->arena,into,"/",normalized))) {
        fprintf(stderr,"cesta build: cannot export input %s\n",path); return false;
    }
    return true;
}

/* Ask the selected compiler for active non-system headers. Reading the C on
   stdin lets quoted includes search the authored directory without accidentally
   preferring an old copy in a reused export directory. This also makes include
   "." safe: no recursive copy of the project or the destination is needed. */
static bool export_headers(Project *p, const Options *o, const Words *flags,
                           const char *source, const char *rendered, const char *object,
                           const char *into) {
    if (suffix_is(source,".s")) return true;
    const char *depfile = join(p->arena,into,"/",join(p->arena,object,".d",""));
    if (!make_directories(parent(p->arena,depfile))) return false;
    Words cmd = {0};
    push(&cmd,(char *)o->compiler);
    for (size_t i = 0; i < flags->len; ++i) push(&cmd,flags->items[i]);
    push(&cmd,"-iquote"); push(&cmd,parent(p->arena,source));
    push(&cmd,"-x"); push(&cmd,suffix_is(source,".S") ? "assembler-with-cpp" : "c");
    push(&cmd,"-MM"); push(&cmd,"-MF"); push(&cmd,(char *)depfile);
    push(&cmd,"-MQ"); push(&cmd,(char *)object); push(&cmd,"-");
    if (o->verbose) echo(cmd.items);
    int result = run_with_input(cmd.items,rendered); words_free(&cmd);
    if (result) return false;
    CeText dependency_text; CeError error = {{0},0};
    bool ok = ce_read_file(p->arena,depfile,&dependency_text,&error);
    Words headers = {0};
    /* GCC emits an empty file for stdin with no non-system headers. */
    if (ok && dependency_text.len) ok = read_dependencies(p->arena,depfile,&headers);
    for (size_t i = 0; ok && i < headers.len; ++i) ok = copy_input(p,into,headers.items[i]);
    if (!ok) fprintf(stderr,"cesta build: cannot record exported headers for %s\n",source);
    words_free(&headers); return ok;
}

static bool suffix_is(const char *path, const char *suffix) {
    size_t n = strlen(path), m = strlen(suffix);
    return n >= m && !strcmp(path+n-m,suffix);
}

/* Readable C for building without Cesta: each .cesta source exported as C,
   other sources, include directories and files named by flags copied, and a
   Makefile with the project's commands. */
static int export_targets(Project *p, const Options *o, const char *into) {
    CeArena *arena = p->arena;
    if (!make_directories(into)) { fprintf(stderr,"cesta build: cannot create %s\n",into); return 1; }
    FILE *mk = fopen(join(arena,into,"/GNUmakefile",""),"wb");
    if (!mk) { fprintf(stderr,"cesta build: cannot write %s/GNUmakefile\n",into); return 1; }
    fprintf(mk,"# Readable C exported by cesta build from %s; builds without Cesta.\n",p->path);
    fprintf(mk,"CC = %s\nAR = ar\n.DEFAULT_GOAL := all\n.DELETE_ON_ERROR:\n\n.PHONY: all\nall:",o->compiler);
    for (size_t k = 0; k < p->ntargets; ++k) {
        if (p->selected[k]) { fputc(' ',mk); make_path(mk,target_output(p,&p->targets[k])); }
    }
    fputs("\n",mk);
    int result = 0;
    Words depfiles = {0};
    for (size_t position = 0; !result && position < p->ntargets; ++position) {
        size_t k = p->order[position];
        const Target *t = &p->targets[k];
        if (!p->selected[k]) continue;
        if (!strcmp(t->kind,"generator")) {
            for (size_t i = 0; !result && i < p->inputs[k].len; ++i)
                if (!copy_input(p,into,p->inputs[k].items[i])) result = 1;
            for (size_t i = 0; !result && i < t->outputs.len; ++i) {
                if (!local_export_path(p,t->outputs.items[i]) || !copy_input(p,into,t->outputs.items[i])) result = 1;
                if (i) fputc(' ',mk);
                make_path(mk,t->outputs.items[i]);
            }
            fputs(t->outputs.len > 1 ? " &: GNUmakefile" : ": GNUmakefile",mk);
            for (size_t i = 0; i < p->inputs[k].len; ++i) { fputc(' ',mk); make_path(mk,p->inputs[k].items[i]); }
            fputs("\n\t@mkdir -p",mk);
            for (size_t i = 0; i < t->outputs.len; ++i) { fputc(' ',mk); make_word(mk,parent(arena,t->outputs.items[i])); }
            fputs("\n\t",mk);
            for (size_t i = 0; i < t->command.len; ++i) { if (i) fputc(' ',mk); make_word(mk,t->command.items[i]); }
            fputs("\n",mk);
            for (size_t i = 0; i < t->outputs.len; ++i) {
                fputs("\t@test -f ",mk); make_word(mk,t->outputs.items[i]); fputc('\n',mk);
            }
            continue;
        }
        Words sources = {0}, flags = {0}, objects = {0};
        if (!target_sources(p,t,&sources)) { result = 1; break; }
        compile_flags(p,t,&flags);
        const Profile *profile = profile_of(p,t);
        if (!local_export_path(p,target_output(p,t))) result = 1;
        for (size_t i = 0; !result && i < p->inputs[k].len; ++i)
            if (!copy_input(p,into,p->inputs[k].items[i])) result = 1;
        for (size_t i = 0; !result && i < p->compile_inputs[k].len; ++i)
            if (!copy_input(p,into,p->compile_inputs[k].items[i])) result = 1;
        for (size_t i = 0; !result && i < sources.len; ++i) {
            const char *source = sources.items[i], *exported = source;
            words_free(&flags); source_flags(p,t,source,&flags);
            if (!local_export_path(p,source)) { result = 1; break; }
            if (suffix_is(source,".cesta") || suffix_is(source,".csexp")) {
                char *c = join(arena,source,"",""); strcpy(c+strlen(c)-6,".c"); exported = c;
                char *target_path = join(arena,into,"/",exported);
                if (!make_directories(parent(arena,target_path))) { result = 1; break; }
                Words cmd = {0};
                push(&cmd,(char *)o->self); push(&cmd,"export-c"); push(&cmd,"--cc"); push(&cmd,(char *)o->compiler);
                push(&cmd,"--presentation"); push(&cmd,"native-c/1");
                push(&cmd,"-o"); push(&cmd,target_path); push(&cmd,(char *)source); push(&cmd,"--");
                for (size_t f = 0; f < flags.len; ++f) push(&cmd,flags.items[f]);
                if (o->verbose) echo(cmd.items);
                pid_t pid = start(cmd.items); int status = 0;
                if (pid < 0) result = 1;
                else { while (waitpid(pid,&status,0) < 0 && errno == EINTR) {} if (!WIFEXITED(status) || WEXITSTATUS(status)) result = 1; }
                words_free(&cmd);
            } else if (!copy_file(source,join(arena,into,"/",source))) {
                fprintf(stderr,"cesta build: cannot copy %s\n",source); result = 1;
            }
            if (result) break;
            const char *object = join(arena,p->out,"/",join(arena,t->name,"/obj/",join(arena,source,".o","")));
            const char *rendered = join(arena,into,"/",exported);
            if (!export_headers(p,o,&flags,source,rendered,object,into)) { result = 1; break; }
            push(&objects,(char *)object);
            push(&depfiles,join(arena,object,".d",""));
            fputc('\n',mk); make_path(mk,object); fputs(": ",mk); make_path(mk,exported);
            for (size_t r = 0; r < p->compile_inputs[k].len; ++r) { fputc(' ',mk); make_path(mk,p->compile_inputs[k].items[r]); }
            fputs(" GNUmakefile\n\t@mkdir -p \"$(@D)\"\n\t$(CC)",mk);
            for (size_t f = 0; f < flags.len; ++f) { fputc(' ',mk); make_word(mk,flags.items[f]); }
            fputs(" -MMD -MP -MF \"$@.d\" -MQ \"$@\" -c ",mk); make_word(mk,exported);
            fputs(" -o \"$@\"\n",mk);
        }
        if (!result) {
            const char *output = target_output(p,t);
            bool archive = !strcmp(t->kind,"archive");
            fputc('\n',mk); make_path(mk,output); fputs(": GNUmakefile",mk);
            for (size_t i = 0; i < objects.len; ++i) { fputc(' ',mk); make_path(mk,objects.items[i]); }
            for (size_t i = 0; i < p->inputs[k].len; ++i) { fputc(' ',mk); make_path(mk,p->inputs[k].items[i]); }
            fputs("\n\t@mkdir -p \"$(@D)\"\n",mk);
            if (archive) {
                fputs("\trm -f \"$@\" && $(AR) rcs \"$@\"",mk);
                for (size_t i = 0; i < objects.len; ++i) { fputc(' ',mk); make_word(mk,objects.items[i]); }
                fputs("\n",mk);
            } else {
                fputs("\t$(CC)",mk);
                for (size_t i = 0; i < objects.len; ++i) { fputc(' ',mk); make_word(mk,objects.items[i]); }
                for (size_t i = 0; profile && i < profile->link.len; ++i) { fputc(' ',mk); make_word(mk,profile->link.items[i]); }
                for (size_t i = 0; i < t->link.len; ++i) { fputc(' ',mk); make_word(mk,t->link.items[i]); }
                fputs(" -o \"$@\"\n",mk);
            }
            fprintf(stderr,"cesta build: %s: exported %zu sources to %s\n",t->name,sources.len,into);
        }
        words_free(&sources); words_free(&flags); words_free(&objects);
    }
    fputs("\n-include",mk);
    for (size_t i = 0; i < depfiles.len; ++i) { fputc(' ',mk); make_path(mk,depfiles.items[i]); }
    fputc('\n',mk); words_free(&depfiles);
    if (fclose(mk)) result = 1;
    return result;
}

int ce_build_command(int argc, char **argv) {
    CeArena arena = {0};
    Options o = {.jobs=1};
    const char *file = "cesta.project", *directory = NULL, *export_directory = NULL;
    bool checked_hints = false, reader_warnings = false;
    char **wanted = ce_alloc(&arena,(size_t)argc*sizeof(*wanted)); size_t nwanted = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i],"-C") && i+1 < argc) directory = argv[++i];
        else if (!strcmp(argv[i],"-f") && i+1 < argc) file = argv[++i];
        else if (!strcmp(argv[i],"--cc") && i+1 < argc) o.compiler = argv[++i];
        else if (!strcmp(argv[i],"-j") && i+1 < argc) o.jobs = (size_t)strtoul(argv[++i],NULL,10);
        else if (!strncmp(argv[i],"-j",2) && argv[i][2]) o.jobs = (size_t)strtoul(argv[i]+2,NULL,10);
        else if (!strcmp(argv[i],"-v")) o.verbose = true;
        else if (!strcmp(argv[i],"--checked-hints")) checked_hints = true;
        else if (!strcmp(argv[i],"--read-warnings")) reader_warnings = true;
        else if (!strcmp(argv[i],"--export-c") && i+1 < argc) export_directory = argv[++i];
        else if (argv[i][0] == '-') {
            fprintf(stderr,"usage: cesta build [-C DIR] [-f FILE] [--cc COMPILER] [-j N] [-v] [--checked-hints] [--read-warnings] [--export-c DIR] [TARGET...]\n");
            ce_arena_free(&arena); return 2;
        } else wanted[nwanted++] = argv[i];
    }
    if (!o.jobs) o.jobs = 1;
    char self[PATH_MAX]; ssize_t length = readlink("/proc/self/exe",self,sizeof self-1);
    if (length <= 0) { fprintf(stderr,"cesta build: cannot locate the cesta executable\n"); ce_arena_free(&arena); return 2; }
    self[length] = 0; o.self = self;
    if (directory && chdir(directory)) {
        fprintf(stderr,"cesta build: cannot enter %s: %s\n",directory,strerror(errno)); ce_arena_free(&arena); return 2;
    }
    Project p = {.arena=&arena,.path=file,.checked_hints=checked_hints,.read_warnings=reader_warnings};
    CeError error = {{0},0};
    if (!ce_read_file(&arena,file,&p.text,&error)) {
        fprintf(stderr,"cesta build: %s: %s\n",file,error.message); ce_arena_free(&arena); return 2;
    }
    int result = 0;
    if (!read_project(&p) || !project_graph(&p)) result = 2;
    if (!o.compiler) o.compiler = p.compiler;
    if (!result && !select_targets(&p,wanted,nwanted)) result = 2;
    if (!result && export_directory) {
        bool *needed = ce_alloc(&arena,p.ntargets*sizeof(*needed));
        for (size_t k = 0; k < p.ntargets; ++k)
            needed[k] = p.selected[k] && !strcmp(p.targets[k].kind,"generator");
        for (size_t position = p.ntargets; position > 0; --position) {
            size_t k = p.order[position-1];
            if (needed[k]) for (size_t j = 0; j < p.ntargets; ++j)
                if (p.edges[k*p.ntargets+j]) needed[j] = true;
        }
        for (size_t position = 0; !result && position < p.ntargets; ++position) {
            size_t k = p.order[position];
            if (needed[k] && build_target(&p,&p.targets[k],&o)) result = 1;
        }
        if (!result) result = export_targets(&p,&o,export_directory);
    }
    else for (size_t position = 0; !result && position < p.ntargets; ++position) {
        size_t k = p.order[position];
        if (p.selected[k] && build_target(&p,&p.targets[k],&o)) result = 1;
    }
    for (size_t k = 0; k < p.nprofiles; ++k) {
        words_free(&p.profiles[k].flags); words_free(&p.profiles[k].link); words_free(&p.profiles[k].inputs);
    }
    for (size_t k = 0; k < p.ntargets; ++k) {
        Target *t = &p.targets[k];
        words_free(&t->sources); words_free(&t->includes); words_free(&t->defines);
        words_free(&t->flags); words_free(&t->link);
        words_free(&t->depends); words_free(&t->inputs);
        words_free(&t->command); words_free(&t->outputs);
        words_free(&t->compile_requires);
        for (size_t i = 0; i < t->nsource_options; ++i) {
            words_free(&t->source_options[i].flags); words_free(&t->source_options[i].includes);
            words_free(&t->source_options[i].defines);
        }
        if (p.inputs) words_free(&p.inputs[k]);
        if (p.compile_inputs) words_free(&p.compile_inputs[k]);
    }
    ce_arena_free(&arena); return result;
}

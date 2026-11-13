#define _POSIX_C_SOURCE 200809L
#include "cesta.h"

#include <errno.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static int wait_process(pid_t pid, CeError *error) {
    int status;
    while (waitpid(pid,&status,0) < 0) {
        if (errno == EINTR) continue;
        ce_error(error,0,"cannot wait for compiler/program: %s",strerror(errno)); return 2;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128+WTERMSIG(status) : 2;
}

int ce_execute(char *const *argv, CeError *error) {
    pid_t pid;
    int status = posix_spawnp(&pid,argv[0],NULL,NULL,argv,environ);
    if (status) { ce_error(error,0,"cannot start %s: %s",argv[0],strerror(status)); return 2; }
    return wait_process(pid,error);
}

int ce_execute_stdout(char *const *argv, CeArena *arena, CeText *out, CeError *error) {
    return ce_execute_capture(argv,STDOUT_FILENO,arena,out,error);
}

int ce_execute_capture(char *const *argv, int stream, CeArena *arena, CeText *out, CeError *error) {
    int descriptors[2]; pid_t pid; posix_spawn_file_actions_t actions;
    *out = (CeText){NULL,0};
    if (pipe(descriptors)) { ce_error(error,0,"cannot capture compiler output: %s",strerror(errno)); return 2; }
    int status = posix_spawn_file_actions_init(&actions);
    if (!status) {
        status = posix_spawn_file_actions_addclose(&actions,descriptors[0]);
        if (!status) status = posix_spawn_file_actions_adddup2(&actions,descriptors[1],stream);
        if (!status && descriptors[1] != stream) status = posix_spawn_file_actions_addclose(&actions,descriptors[1]);
        if (!status) status = posix_spawnp(&pid,argv[0],&actions,NULL,argv,environ);
        posix_spawn_file_actions_destroy(&actions);
    }
    close(descriptors[1]);
    if (status) {
        close(descriptors[0]); ce_error(error,0,"cannot capture %s: %s",argv[0],strerror(status)); return 2;
    }
    typedef struct Chunk { struct Chunk *next; size_t len; unsigned char bytes[8192]; } Chunk;
    Chunk *head = NULL, **tail = &head; size_t total = 0; bool ok = true;
    for (;;) {
        unsigned char bytes[8192]; ssize_t count = read(descriptors[0],bytes,sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { ce_error(error,0,"cannot read compiler output: %s",strerror(errno)); ok = false; break; }
        if (!count) break;
        Chunk *chunk = ce_alloc(arena,sizeof(*chunk)); chunk->len = (size_t)count; chunk->next = NULL;
        memcpy(chunk->bytes,bytes,chunk->len); *tail = chunk; tail = &chunk->next; total += chunk->len;
    }
    close(descriptors[0]);
    int result = wait_process(pid,error);
    unsigned char *bytes = ce_alloc(arena,total+1); size_t position = 0;
    for (Chunk *chunk = head; chunk; chunk = chunk->next) {
        memcpy(bytes+position,chunk->bytes,chunk->len); position += chunk->len;
    }
    bytes[total] = 0; *out = (CeText){bytes,total};
    return ok ? result : 2;
}

static bool check_source(const CeGrammar *g, const CeTree *tree, int entry,
                         const char *path, CeError *error) {
    CeArena a = {0}; CeText source; CeTokens tokens = {0};
    CeDFA dfa = {0}; CeLR lr = {0}; CeForest forest = {.root=-1}; CeTree *parsed = NULL;
    bool ok = ce_read_file(&a,path,&source,error) && ce_dfa_prepare(g,&dfa,error) &&
        ce_dfa_lex(g,&dfa,source,&tokens,error) && ce_lr_prepare(g,entry,&lr,error) &&
        ce_lr_parse(g,&lr,&tokens,&forest,error) && ce_tree_from_forest(g,&forest,&tokens,&a,&parsed,error);
    if (ok && !ce_tree_equal(g,tree,parsed)) ok = ce_error(error,tree->offset,"S-expression tree is not one the selected C parser profile produces");
    ce_forest_free(&forest); ce_lr_free(&lr); ce_dfa_free(&dfa); ce_tokens_free(&tokens); ce_arena_free(&a);
    return ok;
}

bool ce_tree_emit_checked(const CeGrammar *g, const CeTree *tree, int entry,
                          const char *path, CeError *error) {
    FILE *file = fopen(path,"wb");
    if (!file) return ce_error(error,0,"cannot open generated C: %s",strerror(errno));
    bool printed = ce_tree_print(g,tree,entry,file,error);
    if (printed) fputc('\n',file);
    if (fclose(file) != 0) printed = ce_error(error,0,"cannot finish generated C: %s",strerror(errno));
    return printed && check_source(g,tree,entry,path,error);
}

int ce_compile_tree(const CeGrammar *g, const CeTree *tree, int entry,
                    int argc, char **argv, bool check_only, bool run, CeError *error) {
    const char *compiler = "cc", *output = NULL;
    int flags = argc;
    for (int i = 0; i < argc; ++i) {
        if (!strcmp(argv[i],"--")) { flags = i+1; break; }
        if (!strcmp(argv[i],"--cc") && i+1 < argc) compiler = argv[++i];
        else if (!strcmp(argv[i],"-o") && i+1 < argc) output = argv[++i];
        else { ce_error(error,0,"compiler options are --cc COMPILER, -o OUTPUT and -- FLAGS"); return 2; }
    }
    if (!output) { ce_error(error,0,"build/check-program/run requires -o OUTPUT; generated C is kept as OUTPUT.c"); return 2; }
    CeArena a = {0}; size_t len = strlen(output);
    char *path = ce_alloc(&a,len+3); memcpy(path,output,len); memcpy(path+len,".c",3);
    if (!ce_tree_emit_checked(g,tree,entry,path,error)) { ce_arena_free(&a); return 1; }
    char **arguments = ce_alloc(&a,((size_t)(argc-flags)+9)*sizeof(*arguments)); size_t n = 0;
    arguments[n++] = (char *)compiler;
    arguments[n++] = ce_is(g->name,"CGNU") ? "-std=gnu11" : "-std=c11";
    for (int i = flags; i < argc; ++i) arguments[n++] = argv[i];
    if (check_only) arguments[n++] = "-fsyntax-only";
    else { arguments[n++] = "-o"; arguments[n++] = (char *)output; }
    arguments[n++] = path; arguments[n] = NULL;
    int result = ce_execute(arguments,error);
    if (!result && run) {
        char *executable = (char *)output;
        if (!strchr(output,'/')) {
            executable = ce_alloc(&a,len+3); memcpy(executable,"./",2); memcpy(executable+2,output,len+1);
        }
        char *program[] = {executable,NULL}; result = ce_execute(program,error);
    }
    ce_arena_free(&a); return result;
}

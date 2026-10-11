#include "term_graph.h"
#include "stats.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* The production allocator aborts on actual exhaustion; some scratch helpers
 * return NO_MEMORY. Exercise both contracts without installing a runtime knob.
 * This executable alone redirects abort to a recognizable child exit, avoiding
 * a core file for every deliberate failure. */
static size_t allocation_budget = SIZE_MAX;
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
static bool allow_allocation(void) {
    if (allocation_budget == SIZE_MAX) return true;
    if (!allocation_budget) return false;
    --allocation_budget;
    return true;
}
void *__wrap_malloc(size_t size) {
    return allow_allocation() ? __real_malloc(size) : NULL;
}
void *__wrap_calloc(size_t count, size_t size) {
    return allow_allocation() ? __real_calloc(count, size) : NULL;
}
void *__wrap_realloc(void *pointer, size_t size) {
    return allow_allocation() ? __real_realloc(pointer, size) : NULL;
}
_Noreturn void __wrap_abort(void) { _exit(86); }

static bool leaf_eq(Atom *left, Atom *right) {
    return left->kind == ATOM_VAR && right->kind == ATOM_VAR &&
           left->var_id == right->var_id;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init_detached(&arena);
    enum { N = 96 };
    CettaTermMatchPair pairs[N];
    for (unsigned i = 0; i < N; ++i) {
        pairs[i].left = atom_var_with_id(&arena, "pattern", 1u + i);
        pairs[i].right = atom_var_with_id(&arena, "subject", 1u + N + i);
    }
    CettaTermMatch *publication = mmap(NULL, sizeof(*publication), PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(publication != MAP_FAILED);
    Atom *sentinel = pairs[0].left;
    unsigned faults = 0, recoverable = 0, completed = 0;
    for (unsigned mode = CETTA_TERM_MATCH_FORWARD; mode <= CETTA_TERM_MATCH_VARIANT; ++mode) {
        bool success = false;
        for (size_t budget = 0; budget < 256u && !success; ++budget) {
            *publication = (CettaTermMatch){&sentinel, &sentinel, 777u};
            int output[2];
            assert(pipe(output) == 0);
            pid_t child = fork();
            assert(child >= 0);
            if (child == 0) {
                close(output[0]);
                assert(dup2(output[1], STDERR_FILENO) >= 0);
                close(output[1]);
                allocation_budget = budget;
                CettaTermMatchStatus status = term_graph_match_many(&arena, pairs, N,
                    (CettaTermMatchMode)mode, leaf_eq, publication);
                allocation_budget = SIZE_MAX;
                if (status == CETTA_TERM_MATCH_OK) _exit(0);
                if (status == CETTA_TERM_MATCH_NO_MEMORY) _exit(31);
                _exit(99);
            }
            close(output[1]);
            char diagnosis[512];
            size_t used = 0;
            ssize_t n;
            while ((n = read(output[0], diagnosis + used, sizeof(diagnosis) - 1u - used)) > 0)
                used += (size_t)n;
            diagnosis[used] = '\0';
            close(output[0]);
            int status;
            assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
            int code = WEXITSTATUS(status);
            if (code == 0) {
                assert(!used && publication->count == N && publication->variables != &sentinel);
                success = true;
                ++completed;
            } else {
                assert(code == 31 || (code == 86 &&
                    strncmp(diagnosis, "fatal: out of memory allocating ", 31u) == 0));
                assert(publication->count == 777u && publication->variables == &sentinel &&
                       publication->values == &sentinel);
                ++faults;
                recoverable += code == 31;
            }
        }
        assert(success);
    }
    assert(faults >= 15u && completed == 3u);
    assert(munmap(publication, sizeof(*publication)) == 0);
    arena_free(&arena);
    symbol_table_free(&symbols);
    printf("PASS: %u allocation failures (%u returned capacity), no partial matching publication\n",
        faults, recoverable);
    return 0;
}

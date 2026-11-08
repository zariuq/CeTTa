#include "sat_ipasir_v1.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>

static struct {
    bool loaded;
    const char *(*signature)(void);
    void *(*init)(void);
    void (*release)(void *);
    void (*add)(void *, int32_t);
    int (*solve)(void *);
    int32_t (*val)(void *, int32_t);
} ipasir;

static pthread_once_t ipasir_once = PTHREAD_ONCE_INIT;

static void sat_ipasir_load(void) {
    const char *path = getenv("CETTA_IPASIR_LIBRARY");
    void *handle = dlopen(path && *path ? path : "libcadical.so", RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return;
#define IPASIR_BIND(field, name) \
    if (!(*(void **)&ipasir.field = dlsym(handle, name))) { dlclose(handle); return; }
    IPASIR_BIND(signature, "ipasir_signature")
    IPASIR_BIND(init, "ipasir_init")
    IPASIR_BIND(release, "ipasir_release")
    IPASIR_BIND(add, "ipasir_add")
    IPASIR_BIND(solve, "ipasir_solve")
    IPASIR_BIND(val, "ipasir_val")
#undef IPASIR_BIND
    ipasir.loaded = true;
}

bool sat_ipasir_available(void) {
    pthread_once(&ipasir_once, sat_ipasir_load);
    return ipasir.loaded;
}

const char *sat_ipasir_signature(void) {
    return sat_ipasir_available() ? ipasir.signature() : NULL;
}

SatIpasirResult sat_ipasir_solve(uint32_t nvars, const int32_t *lits,
                                 const uint32_t *start, uint32_t nclauses,
                                 uint8_t *values) {
    if (!sat_ipasir_available())
        return SAT_IPASIR_FAILED;
    void *solver = ipasir.init();
    if (!solver)
        return SAT_IPASIR_FAILED;
    for (uint32_t i = 0u; i < nclauses; i++) {
        for (uint32_t k = start[i]; k < start[i + 1u]; k++)
            ipasir.add(solver, lits[k]);
        ipasir.add(solver, 0);
    }
    int r = ipasir.solve(solver);
    SatIpasirResult result = r == 10 ? SAT_IPASIR_SAT : r == 20 ? SAT_IPASIR_UNSAT
                                                                : SAT_IPASIR_FAILED;
    for (uint32_t v = 1u; result == SAT_IPASIR_SAT && v <= nvars; v++)
        values[v] = ipasir.val(solver, (int32_t)v) > 0;
    ipasir.release(solver);
    return result;
}

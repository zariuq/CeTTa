#include "petta_typecheck_census.h"

#if CETTA_BUILD_WITH_PETTA_TYPECHECK_CENSUS

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *scope;
    const char *kind;
    const char *mapping;
    const char *axis;
} CettaPettaTypecheckCensusDescriptor;

static const CettaPettaTypecheckCensusDescriptor census_descriptors[] = {
#define CETTA_PETTA_TYPECHECK_CENSUS_DESCRIPTOR(                            \
    tag, name, scope, kind, mapping, axis)                                  \
    [CETTA_PETTA_TYPECHECK_CENSUS_EVENT_##tag] = {                          \
        name, scope, kind, mapping, axis},
    CETTA_PETTA_TYPECHECK_CENSUS_EVENTS(
        CETTA_PETTA_TYPECHECK_CENSUS_DESCRIPTOR)
#undef CETTA_PETTA_TYPECHECK_CENSUS_DESCRIPTOR
};

static pthread_once_t census_once = PTHREAD_ONCE_INIT;
static bool census_enabled;
static atomic_bool census_seen[CETTA_PETTA_TYPECHECK_CENSUS_EVENT_COUNT];

static void census_initialize(void) {
    const char *value = getenv("CETTA_PETTA_TYPECHECK_CENSUS");
    census_enabled = value && value[0] != '\0' &&
        strcmp(value, "0") != 0 && strcmp(value, "false") != 0 &&
        strcmp(value, "off") != 0;
    if (!census_enabled)
        return;
    for (size_t index = 0u;
         index < CETTA_PETTA_TYPECHECK_CENSUS_EVENT_COUNT; index++) {
        const CettaPettaTypecheckCensusDescriptor *descriptor =
            &census_descriptors[index];
        fprintf(
            stderr,
            "CETTA_PETTA_TYPECHECK_CENSUS_V2\tcatalog\t%s\t%s\t%s\t%s\t%s\n",
            descriptor->name, descriptor->scope,
            descriptor->kind, descriptor->mapping, descriptor->axis);
    }
}

void cetta_petta_typecheck_census_hit_at(
    CettaPettaTypecheckCensusEvent event, const char *origin) {
    (void)pthread_once(&census_once, census_initialize);
    if (!census_enabled ||
        (unsigned)event >= CETTA_PETTA_TYPECHECK_CENSUS_EVENT_COUNT)
        return;
    if (atomic_exchange_explicit(
            &census_seen[event], true, memory_order_relaxed))
        return;
    fprintf(stderr, "CETTA_PETTA_TYPECHECK_CENSUS_V2\thit\t%s\t%s\n",
            census_descriptors[event].name,
            origin && origin[0] ? origin : "unknown");
}

#endif

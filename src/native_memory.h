#ifndef CETTA_NATIVE_MEMORY_H
#define CETTA_NATIVE_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct CettaNativeMemory CettaNativeMemory;
typedef struct { CettaNativeMemory *previous; bool selected; } CettaNativeMemoryScope;
CettaNativeMemory *cetta_native_memory_new(uint64_t limit);
void cetta_native_memory_retain(CettaNativeMemory *);
void cetta_native_memory_release(CettaNativeMemory *);
void cetta_native_memory_enter(CettaNativeMemoryScope *, CettaNativeMemory *) __attribute__((weak));
void cetta_native_memory_leave(CettaNativeMemoryScope *) __attribute__((weak));
CettaNativeMemory *cetta_native_memory_current(void) __attribute__((weak));
bool cetta_native_memory_recover(void) __attribute__((weak));
bool cetta_native_memory_failed(void) __attribute__((weak));
void cetta_native_memory_abandon(void) __attribute__((weak));
bool cetta_native_memory_abandoned(const CettaNativeMemory *) __attribute__((weak));
bool cetta_native_memory_limited(void) __attribute__((weak));
uint64_t cetta_native_memory_used(const CettaNativeMemory *);
void cetta_native_memory_clear_failure(CettaNativeMemory *);
void *cetta_native_malloc(size_t) __attribute__((weak));
void *cetta_native_calloc(size_t, size_t) __attribute__((weak));
void *cetta_native_realloc(void *, size_t) __attribute__((weak));
void cetta_native_free(void *) __attribute__((weak));
char *cetta_native_strdup(const char *) __attribute__((weak));
char *cetta_native_strndup(const char *, size_t) __attribute__((weak));
#endif

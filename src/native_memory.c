#define _POSIX_C_SOURCE 200809L
#define CETTA_NATIVE_MEMORY_IMPLEMENTATION
#include "native_memory.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>

struct CettaNativeMemory {
    atomic_uint references;
    _Atomic uint64_t used;
    atomic_bool failed;
    atomic_bool abandoned;
    uint64_t limit;
};
typedef struct Allocation {
    struct Allocation *next;
    void *pointer;
    size_t bytes;
    CettaNativeMemory *owner;
} Allocation;
enum { BUCKETS = 2048 };
static Allocation *allocations[BUCKETS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_size_t tracked;
static _Thread_local CettaNativeMemory *selected_owner;
static _Thread_local bool selected;
CettaNativeMemory *eval_native_memory_owner(void) __attribute__((weak));

CettaNativeMemory *cetta_native_memory_current(void) {
    return selected ? selected_owner : eval_native_memory_owner ? eval_native_memory_owner() : NULL;
}
CettaNativeMemory *cetta_native_memory_new(uint64_t limit) {
    CettaNativeMemory *owner = calloc(1u, sizeof(*owner));
    if (!owner) return NULL;
    atomic_init(&owner->references, 1u);
    atomic_init(&owner->used, 0u);
    atomic_init(&owner->failed, false);
    atomic_init(&owner->abandoned, false);
    owner->limit = limit;
    return owner;
}
void cetta_native_memory_retain(CettaNativeMemory *owner) {
    if (owner) atomic_fetch_add_explicit(&owner->references, 1u, memory_order_relaxed);
}
void cetta_native_memory_release(CettaNativeMemory *owner) {
    if (owner && atomic_fetch_sub_explicit(&owner->references, 1u, memory_order_acq_rel) == 1u) free(owner);
}
void cetta_native_memory_enter(CettaNativeMemoryScope *scope, CettaNativeMemory *owner) {
    *scope = (CettaNativeMemoryScope){selected_owner, selected};
    selected_owner = owner; selected = true;
}
void cetta_native_memory_leave(CettaNativeMemoryScope *scope) {
    selected_owner = scope->previous; selected = scope->selected;
}
bool cetta_native_memory_recover(void) {
    CettaNativeMemory *owner = cetta_native_memory_current();
    if (!owner) return false;
    atomic_store_explicit(&owner->failed, true, memory_order_release);
    errno = ENOMEM;
    return true;
}
bool cetta_native_memory_failed(void) {
    CettaNativeMemory *owner = cetta_native_memory_current();
    return owner && atomic_load_explicit(&owner->failed, memory_order_acquire);
}
void cetta_native_memory_abandon(void) {
    CettaNativeMemory *owner = cetta_native_memory_current();
    if (owner) atomic_store_explicit(&owner->abandoned, true, memory_order_release);
}
bool cetta_native_memory_abandoned(const CettaNativeMemory *owner) {
    return owner && atomic_load_explicit(&owner->abandoned, memory_order_acquire);
}
bool cetta_native_memory_limited(void) {
    CettaNativeMemory *owner = cetta_native_memory_current();
    return owner && owner->limit != UINT64_MAX;
}
uint64_t cetta_native_memory_used(const CettaNativeMemory *owner) {
    return owner ? atomic_load_explicit(&owner->used, memory_order_acquire) : 0u;
}
void cetta_native_memory_clear_failure(CettaNativeMemory *owner) {
    if (owner) atomic_store_explicit(&owner->failed, false, memory_order_release);
}
static void note_failure(CettaNativeMemory *owner) {
    if (owner) atomic_store_explicit(&owner->failed, true, memory_order_release);
    errno = ENOMEM;
}
static bool reserve(CettaNativeMemory *owner, uint64_t amount) {
    uint64_t used = atomic_load_explicit(&owner->used, memory_order_relaxed);
    do {
        if (used > owner->limit || amount > owner->limit - used) { note_failure(owner); return false; }
    } while (!atomic_compare_exchange_weak_explicit(&owner->used, &used, used + amount,
        memory_order_acq_rel, memory_order_relaxed));
    return true;
}
static void credit(CettaNativeMemory *owner, uint64_t amount) {
    atomic_fetch_sub_explicit(&owner->used, amount, memory_order_acq_rel);
}
static size_t bucket(const void *pointer) {
    uintptr_t word = (uintptr_t)pointer;
    return ((word >> 4u) ^ (word >> 17u)) & (BUCKETS - 1u);
}
static Allocation **find(void *pointer) {
    Allocation **at = &allocations[bucket(pointer)];
    while (*at && (*at)->pointer != pointer) at = &(*at)->next;
    return at;
}
static void insert(Allocation *record) {
    size_t at = bucket(record->pointer);
    record->next = allocations[at]; allocations[at] = record;
}
void *cetta_native_malloc(size_t size) {
    if (!size) size = 1u;
    CettaNativeMemory *owner = cetta_native_memory_current();
    if (!owner || owner->limit == UINT64_MAX) {
        void *pointer = malloc(size);
        if (!pointer) note_failure(owner);
        return pointer;
    }
    if (size > SIZE_MAX - sizeof(Allocation) || !reserve(owner, size + sizeof(Allocation))) {
        note_failure(owner); return NULL;
    }
    Allocation *record = malloc(sizeof(*record));
    void *pointer = record ? malloc(size) : NULL;
    if (!pointer) {
        free(record); credit(owner, size + sizeof(Allocation)); note_failure(owner); return NULL;
    }
    cetta_native_memory_retain(owner);
    *record = (Allocation){.pointer = pointer, .bytes = size, .owner = owner};
    pthread_mutex_lock(&lock);
    insert(record); atomic_fetch_add_explicit(&tracked, 1u, memory_order_release);
    pthread_mutex_unlock(&lock);
    return pointer;
}
void *cetta_native_calloc(size_t count, size_t width) {
    size_t size;
    if (__builtin_mul_overflow(count, width, &size)) { note_failure(cetta_native_memory_current()); return NULL; }
    CettaNativeMemory *owner = cetta_native_memory_current();
    if (!owner || owner->limit == UINT64_MAX) {
        void *pointer = calloc(count, width);
        if (!pointer) note_failure(owner);
        return pointer;
    }
    void *pointer = cetta_native_malloc(size);
    if (pointer) memset(pointer, 0, size);
    return pointer;
}
char *cetta_native_strndup(const char *text, size_t maximum) {
    CettaNativeMemory *owner = cetta_native_memory_current();
    if (!owner || owner->limit == UINT64_MAX) {
        char *pointer = strndup(text, maximum);
        if (!pointer) note_failure(owner);
        return pointer;
    }
    size_t length = strnlen(text, maximum);
    if (length == SIZE_MAX) { errno = ENOMEM; return NULL; }
    char *copy = cetta_native_malloc(length + 1u);
    if (copy) { memcpy(copy, text, length); copy[length] = '\0'; }
    return copy;
}
char *cetta_native_strdup(const char *text) {
    return cetta_native_strndup(text, strlen(text));
}
void cetta_native_free(void *pointer) {
    if (!pointer) return;
    Allocation *record = NULL;
    if (atomic_load_explicit(&tracked, memory_order_acquire)) {
        pthread_mutex_lock(&lock);
        Allocation **at = find(pointer);
        record = *at;
        if (record) {
            *at = record->next;
            atomic_fetch_sub_explicit(&tracked, 1u, memory_order_release);
        }
        pthread_mutex_unlock(&lock);
    }
    free(pointer);
    if (record) {
        credit(record->owner, record->bytes + sizeof(*record));
        cetta_native_memory_release(record->owner);
        free(record);
    }
}
void *cetta_native_realloc(void *pointer, size_t size) {
    if (!pointer) return cetta_native_malloc(size);
    if (!size) size = 1u;
    Allocation *record = NULL;
    if (atomic_load_explicit(&tracked, memory_order_acquire)) {
        pthread_mutex_lock(&lock);
        Allocation **at = find(pointer);
        record = *at;
        if (record) {
            size_t before = record->bytes;
            if (size > before && !reserve(record->owner, size - before)) {
                pthread_mutex_unlock(&lock); return NULL;
            }
            void *next = realloc(pointer, size);
            if (!next) {
                if (size > before) credit(record->owner, size - before);
                note_failure(record->owner);
                pthread_mutex_unlock(&lock); return NULL;
            }
            /* Keep the old address unobservable until its mapping moves:
             * another thread may immediately allocate that released address. */
            *at = record->next;
            record->pointer = next; record->bytes = size; insert(record);
            if (size < before) credit(record->owner, before - size);
            pthread_mutex_unlock(&lock);
            return next;
        }
        pthread_mutex_unlock(&lock);
    }
    if (!record) {
        CettaNativeMemory *owner = cetta_native_memory_current();
        if (!owner || owner->limit == UINT64_MAX) {
            void *next = realloc(pointer, size);
            if (!next) note_failure(owner);
            return next;
        }
        if (size > SIZE_MAX - sizeof(Allocation) || !reserve(owner, size + sizeof(Allocation))) {
            note_failure(owner); return NULL;
        }
        record = malloc(sizeof(*record));
        void *next = record ? realloc(pointer, size) : NULL;
        if (!next) { free(record); credit(owner, size + sizeof(Allocation)); note_failure(owner); return NULL; }
        cetta_native_memory_retain(owner);
        *record = (Allocation){.pointer = next, .bytes = size, .owner = owner};
        pthread_mutex_lock(&lock);
        insert(record); atomic_fetch_add_explicit(&tracked, 1u, memory_order_release);
        pthread_mutex_unlock(&lock);
        return next;
    }
    return NULL;
}

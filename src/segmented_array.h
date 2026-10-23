#ifndef CETTA_SEGMENTED_ARRAY_H
#define CETTA_SEGMENTED_ARRAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Single-writer, indexed storage with stable element addresses. The four
 * directory digits and page offset cover every uint32_t index. No directory
 * or element page moves when another page is installed. Logical length and
 * initialized element ownership belong to the client, not this backing store.
 * Each prepare installs at most one fixed-size, zero-initialized block. These
 * are allocator request bounds, not allocator execution-time guarantees. */
enum {
    CETTA_SEGMENTED_PAGE_BITS = 6,
    CETTA_SEGMENTED_PAGE_CELLS = 1u << CETTA_SEGMENTED_PAGE_BITS,
    CETTA_SEGMENTED_DIRECTORY_BITS = 7,
    CETTA_SEGMENTED_DIRECTORY_CELLS = 1u << CETTA_SEGMENTED_DIRECTORY_BITS,
    CETTA_SEGMENTED_DIRECTORY_LEVELS = 4,
    CETTA_SEGMENTED_MAX_ELEMENT_SIZE = 256
};

typedef struct CettaSegmentedBlock {
    struct CettaSegmentedBlock *next;
    size_t bytes;
    _Alignas(max_align_t) unsigned char data[];
} CettaSegmentedBlock;

typedef struct {
    CettaSegmentedBlock *root, *blocks;
    size_t element_size, live_bytes;
    bool retiring;
} CettaSegmentedArray;

typedef enum {
    CETTA_SEGMENTED_PENDING,
    CETTA_SEGMENTED_READY,
    CETTA_SEGMENTED_REFUSED
} CettaSegmentedStep;

static inline bool cetta_segmented_array_init(
        CettaSegmentedArray *array, size_t element_size) {
    if (!array || !element_size ||
        element_size > CETTA_SEGMENTED_MAX_ELEMENT_SIZE ||
        array->root || array->blocks || array->live_bytes || array->retiring)
        return false;
    *array = (CettaSegmentedArray){.element_size = element_size};
    return true;
}

static inline CettaSegmentedStep cetta_segmented_array_install(
        CettaSegmentedArray *array, CettaSegmentedBlock **slot, size_t extent,
        size_t byte_limit) {
    if (extent > SIZE_MAX - sizeof(CettaSegmentedBlock)) return CETTA_SEGMENTED_REFUSED;
    size_t bytes = sizeof(CettaSegmentedBlock) + extent;
    if (array->live_bytes > byte_limit || bytes > byte_limit - array->live_bytes)
        return CETTA_SEGMENTED_REFUSED;
    CettaSegmentedBlock *block = calloc(1u, bytes);
    if (!block) return CETTA_SEGMENTED_REFUSED;
    block->next = array->blocks;
    block->bytes = bytes;
    array->live_bytes += bytes;
    array->blocks = block;
    *slot = block;
    return CETTA_SEGMENTED_PENDING;
}

static inline CettaSegmentedStep cetta_segmented_array_prepare_limit(
        CettaSegmentedArray *array, uint32_t index, size_t byte_limit) {
    if (!array || !array->element_size ||
        array->element_size > CETTA_SEGMENTED_MAX_ELEMENT_SIZE || array->retiring ||
        array->live_bytes > byte_limit)
        return CETTA_SEGMENTED_REFUSED;
    CettaSegmentedBlock **slot = &array->root;
    for (unsigned level = 0u; level < CETTA_SEGMENTED_DIRECTORY_LEVELS; ++level) {
        if (!*slot)
            return cetta_segmented_array_install(array, slot,
                CETTA_SEGMENTED_DIRECTORY_CELLS * sizeof(CettaSegmentedBlock *), byte_limit);
        unsigned shift = CETTA_SEGMENTED_PAGE_BITS +
            CETTA_SEGMENTED_DIRECTORY_BITS *
            (CETTA_SEGMENTED_DIRECTORY_LEVELS - 1u - level);
        CettaSegmentedBlock **children = (CettaSegmentedBlock **)(*slot)->data;
        slot = &children[(index >> shift) & (CETTA_SEGMENTED_DIRECTORY_CELLS - 1u)];
    }
    if (!*slot)
        return cetta_segmented_array_install(array, slot,
            CETTA_SEGMENTED_PAGE_CELLS * array->element_size, byte_limit);
    return CETTA_SEGMENTED_READY;
}

static inline CettaSegmentedStep cetta_segmented_array_prepare(
        CettaSegmentedArray *array, uint32_t index) {
    return cetta_segmented_array_prepare_limit(array,index,SIZE_MAX);
}

/* A null result means the backing page is not prepared or is being retired;
 * it is not evidence that a client sequence has no element at this index. */
static inline void *cetta_segmented_array_at(
        const CettaSegmentedArray *array, uint32_t index) {
    if (!array || !array->element_size || array->element_size > CETTA_SEGMENTED_MAX_ELEMENT_SIZE || array->retiring) return NULL;
    CettaSegmentedBlock *block = array->root;
    for (unsigned level = 0u; block && level < CETTA_SEGMENTED_DIRECTORY_LEVELS; ++level) {
        unsigned shift = CETTA_SEGMENTED_PAGE_BITS +
            CETTA_SEGMENTED_DIRECTORY_BITS *
            (CETTA_SEGMENTED_DIRECTORY_LEVELS - 1u - level);
        CettaSegmentedBlock *const *children = (CettaSegmentedBlock *const *)block->data;
        block = children[(index >> shift) & (CETTA_SEGMENTED_DIRECTORY_CELLS - 1u)];
    }
    return block ? block->data +
        (size_t)(index & (CETTA_SEGMENTED_PAGE_CELLS - 1u)) * array->element_size : NULL;
}

/* The client first disposes its initialized element payloads. Retiring then
 * revokes indexed access and frees one allocation-chain block per call; no
 * directory traversal or destructor callback is needed. */
static inline bool cetta_segmented_array_release_one(CettaSegmentedArray *array) {
    if (!array) return true;
    array->retiring = true;
    array->root = NULL;
    CettaSegmentedBlock *block = array->blocks;
    if (block) {
        array->blocks = block->next;
        array->live_bytes -= block->bytes;
        free(block);
    }
    if (!array->blocks) {
        *array = (CettaSegmentedArray){0};
        return true;
    }
    return false;
}

static inline void cetta_segmented_array_free(CettaSegmentedArray *array) {
    while (!cetta_segmented_array_release_one(array)) {}
}

/* A retained stack of fixed-size records. Indexed backing contains record
 * pointers; record_size is fixed by the client's C type, independently of
 * stack depth. The client holds one typed pending record until preparation
 * publishes a complete allocated copy. No client payload is interpreted here.
 * A request never allocates or replays the parent's accepted progress. */
typedef struct {
    CettaSegmentedArray slots;
    size_t record_size;
    uint32_t length;
    bool pending;
} CettaRecordStack;

static inline bool cetta_record_stack_request(CettaRecordStack *stack,
                                             size_t record_size) {
    if (!stack || !record_size || stack->pending || stack->slots.retiring ||
        stack->length==UINT32_MAX ||
        (stack->record_size && stack->record_size!=record_size)) return false;
    stack->record_size=record_size;
    stack->length++;
    stack->pending=true;
    return true;
}

static inline void *cetta_record_stack_at(const CettaRecordStack *stack,
                                         uint32_t index, void *pending_record) {
    if (!stack || index>=stack->length || stack->slots.retiring) return NULL;
    if (stack->pending && index==stack->length-1u) return pending_record;
    void *const *slot=cetta_segmented_array_at(&stack->slots,index);
    return slot ? *slot : NULL;
}

/* Install at most one bounded pointer-backing block, or allocate/copy/publish
 * one fixed-size record. Even successful record publication yields, so a
 * caller never combines it with interpretation of the new frame. Failure
 * retains the typed pending record, logical depth and committed prefix. */
static inline CettaSegmentedStep cetta_record_stack_prepare(
        CettaRecordStack *stack, const void *pending_record) {
    if (!stack || !stack->record_size || stack->slots.retiring)
        return CETTA_SEGMENTED_REFUSED;
    if (!stack->pending) return CETTA_SEGMENTED_READY;
    if (!stack->length || !pending_record) return CETTA_SEGMENTED_REFUSED;
    if (!stack->slots.element_size &&
        !cetta_segmented_array_init(&stack->slots,sizeof(void *)))
        return CETTA_SEGMENTED_REFUSED;
    uint32_t index=stack->length-1u;
    CettaSegmentedStep prepared=cetta_segmented_array_prepare(&stack->slots,index);
    if (prepared!=CETTA_SEGMENTED_READY) return prepared;
    void **slot=cetta_segmented_array_at(&stack->slots,index);
    if (!slot || *slot) return CETTA_SEGMENTED_REFUSED;
    void *record=calloc(1u,stack->record_size);
    if (!record) return CETTA_SEGMENTED_REFUSED;
    memcpy(record,pending_record,stack->record_size);
    *slot=record;
    stack->pending=false;
    return CETTA_SEGMENTED_PENDING;
}

/* The client must first retire the top record's owned contents. A queued
 * record has no allocated copy; a committed record clears its slot before
 * its allocation is released. Other records and their addresses stay put. */
static inline bool cetta_record_stack_pop(CettaRecordStack *stack) {
    if (!stack || !stack->length || stack->slots.retiring) return false;
    if (stack->pending) stack->pending=false;
    else {
        void **slot=cetta_segmented_array_at(&stack->slots,stack->length-1u);
        if (!slot || !*slot) return false;
        void *record=*slot;
        *slot=NULL;
        free(record);
    }
    stack->length--;
    return true;
}

static inline CettaSegmentedStep cetta_record_stack_release_storage_one(
        CettaRecordStack *stack) {
    if (!stack || stack->length || stack->pending) return CETTA_SEGMENTED_REFUSED;
    return cetta_segmented_array_release_one(&stack->slots)
        ? CETTA_SEGMENTED_READY : CETTA_SEGMENTED_PENDING;
}

static inline bool cetta_record_stack_free_storage(CettaRecordStack *stack) {
    CettaSegmentedStep step;
    do { step=cetta_record_stack_release_storage_one(stack); }
    while (step==CETTA_SEGMENTED_PENDING);
    return step==CETTA_SEGMENTED_READY;
}

#endif

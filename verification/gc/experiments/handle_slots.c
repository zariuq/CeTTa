/* Isolated retained-key collector. No evaluator IDs or table entries change.
 * Strong clients own counted generation-checked handles; the lookup table is
 * weak. Bytes may be borrowed only while a strong handle is held. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CAPACITY = 128, WINDOW = 64, PAYLOAD = 4096 };
typedef struct { uint32_t slot, generation; } Handle;
typedef struct {
    unsigned char *bytes;
    size_t length;
    uint32_t generation, references;
} Slot;
typedef struct {
    Slot slots[CAPACITY];
    size_t payload_bytes, payload_peak;
    uint64_t allocations, collections;
    bool reuse_generation_mutant;
    bool omit_retain_mutant;
} Table;

static const Slot *borrow(const Table *table, Handle handle) {
    if (handle.slot >= CAPACITY) return NULL;
    const Slot *slot = &table->slots[handle.slot];
    return slot->bytes && slot->references &&
           slot->generation == handle.generation ? slot : NULL;
}

static bool release(Table *table, Handle handle) {
    if (!borrow(table, handle)) return false;
    table->slots[handle.slot].references--;
    return true;
}

static void collect(Table *table) {
    table->collections++;
    for (size_t i = 0; i < CAPACITY; i++) {
        Slot *slot = &table->slots[i];
        if (!slot->bytes || slot->references) continue;
        table->payload_bytes -= slot->length ? slot->length : 1u;
        free(slot->bytes);
        slot->bytes = NULL;
        slot->length = 0;
    }
}

static bool intern(Table *table, const unsigned char *bytes, size_t length,
                   Handle *result) {
    for (uint32_t i = 0; i < CAPACITY; i++) {
        Slot *slot = &table->slots[i];
        if (slot->bytes && slot->length == length &&
            memcmp(slot->bytes, bytes, length) == 0) {
            if (slot->references == UINT32_MAX) return false;
            if (!table->omit_retain_mutant) slot->references++;
            *result = (Handle){i, slot->generation};
            return true;
        }
    }
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        for (uint32_t i = 0; i < CAPACITY; i++) {
            Slot *slot = &table->slots[i];
            if (slot->bytes || slot->generation == UINT32_MAX) continue;
            size_t size = length ? length : 1u;
            slot->bytes = malloc(size);
            if (!slot->bytes) return false;
            if (length) memcpy(slot->bytes, bytes, length);
            slot->length = length;
            slot->references = 1u;
            if (!table->reuse_generation_mutant || !slot->generation)
                slot->generation++;
            *result = (Handle){i, slot->generation};
            table->payload_bytes += size;
            if (table->payload_bytes > table->payload_peak)
                table->payload_peak = table->payload_bytes;
            table->allocations++;
            return true;
        }
        collect(table);
    }
    return false;
}

static void destroy(Table *table) {
    for (size_t i = 0; i < CAPACITY; i++) free(table->slots[i].bytes);
}

static bool semantics(bool reuse_mutant, bool retain_mutant) {
    Table table = {.reuse_generation_mutant = reuse_mutant,
                   .omit_retain_mutant = retain_mutant};
    const unsigned char bytes[] = {'x', 0, 0xff};
    const unsigned char prefix[] = {'x'};
    Handle held, copy, other;
    assert(intern(&table, bytes, sizeof(bytes), &held));
    assert(intern(&table, bytes, sizeof(bytes), &copy));
    assert(held.slot == copy.slot && held.generation == copy.generation);
    assert(intern(&table, prefix, sizeof(prefix), &other));
    assert(held.slot != other.slot);
    const unsigned char *leased = borrow(&table, held)->bytes;
    assert(release(&table, held));
    collect(&table);
    if (!borrow(&table, copy)) {
        puts("detected-mutant: omitted strong copy invalidated a surviving borrower");
        destroy(&table);
        return false;
    }
    assert(memcmp(leased, bytes, sizeof(bytes)) == 0);
    assert(release(&table, copy) && release(&table, other));
    collect(&table);
    assert(table.payload_bytes == 0 && !borrow(&table, held));
    Handle replacement;
    assert(intern(&table, prefix, sizeof(prefix), &replacement));
    bool stale_rejected = borrow(&table, held) == NULL;
    if (!stale_rejected) {
        puts("detected-mutant: recycled slot accepted an obsolete generation");
        destroy(&table);
        return false;
    }
    assert(!release(&table, held));
    assert(release(&table, replacement));
    collect(&table);
    assert(intern(&table, bytes, sizeof(bytes), &replacement));
    assert(borrow(&table, replacement)->length == sizeof(bytes));
    assert(memcmp(borrow(&table, replacement)->bytes, bytes, sizeof(bytes)) == 0);
    destroy(&table);

    /* Exhaustion refuses allocation instead of wrapping a generation. */
    Table exhausted = {0};
    for (size_t i = 0; i < CAPACITY; i++) exhausted.slots[i].generation = UINT32_MAX;
    assert(!intern(&exhausted, bytes, sizeof(bytes), &replacement));
    assert(!borrow(&exhausted, held));
    destroy(&exhausted);
    return true;
}

int main(int argc, char **argv) {
    bool reuse_mutant = argc == 2 && strcmp(argv[1], "reuse-generation") == 0;
    bool retain_mutant = argc == 2 && strcmp(argv[1], "omit-retain") == 0;
    if (!semantics(reuse_mutant, retain_mutant)) return 1;
    if (reuse_mutant || retain_mutant) return 2; /* Surviving mutant. */
    Table table = {0};
    Handle held[WINDOW] = {0};
    unsigned char bytes[PAYLOAD];
    for (uint32_t i = 0; i < 32768; i++) {
        size_t position = i % WINDOW;
        if (i >= WINDOW) assert(release(&table, held[position]));
        memset(bytes, 'x', sizeof(bytes));
        memcpy(bytes, &i, sizeof(i));
        assert(intern(&table, bytes, sizeof(bytes), &held[position]));
        if ((i + 1u) % WINDOW == 0) collect(&table);
        if (i == 8191 || i == 32767) {
            printf("{\"created\":%u,\"payload_bytes\":%zu,\"metadata_bytes\":%zu,"
                   "\"payload_peak\":%zu,\"collections\":%llu}\n",
                   i + 1u, table.payload_bytes, sizeof(table), table.payload_peak,
                   (unsigned long long)table.collections);
            assert(table.payload_bytes == WINDOW * PAYLOAD);
        }
    }
    for (size_t i = 0; i < WINDOW; i++) assert(release(&table, held[i]));
    collect(&table);
    assert(table.payload_bytes == 0);
    printf("{\"after_release\":true,\"payload_bytes\":0,\"metadata_bytes\":%zu}\n", sizeof(table));
    destroy(&table);
    return 0;
}

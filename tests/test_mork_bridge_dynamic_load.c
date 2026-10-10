#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rust/cetta-space-bridge/include/mork_space_bridge.h"

static void *required_symbol(void *library, const char *name) {
    void *symbol = dlsym(library, name);
    if (!symbol) {
        fprintf(stderr, "%s: %s\n", name, dlerror());
        exit(1);
    }
    return symbol;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s bridge-library\n", argv[0]);
        return 2;
    }
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    MorkSpace *(*new_space)(void) = required_symbol(library, "mork_space_new");
    void (*free_space)(MorkSpace *) = required_symbol(library, "mork_space_free");
    MorkStatus (*add_text)(MorkSpace *, const uint8_t *, size_t) =
        required_symbol(library, "mork_space_add_text");
    MorkBuffer (*query)(MorkSpace *, const uint8_t *, size_t) =
        required_symbol(library, "mork_space_query_bindings_query_only_v2");
    void (*free_bytes)(uint8_t *, size_t) = required_symbol(library, "mork_bytes_free");
    MorkStatus (*count_expr)(const MorkSpace *, const uint8_t *, size_t) =
        required_symbol(library, "mork_space_count_expr_bytes");
    MorkStatus null_count = count_expr(NULL, NULL, 0u);
    assert(null_count.code == MORK_STATUS_NULL);
    if (null_count.message) free_bytes(null_count.message, null_count.message_len);

    MorkSpace *space = new_space();
    assert(space);
    const char *facts = "(pair a b)\n(pair a c)";
    MorkStatus added = add_text(space, (const uint8_t *)facts, strlen(facts));
    assert(added.code == MORK_STATUS_OK && added.value == 2);
    if (added.message) free_bytes(added.message, added.message_len);

    const char *pattern = "(pair a $x)";
    MorkBuffer packet = query(space, (const uint8_t *)pattern, strlen(pattern));
    free_space(space);
    // The returned packet owns its bytes independently of its source space.
    assert(packet.code == MORK_STATUS_OK && packet.count == 2);
    assert(packet.len >= 16 && packet.data[0] == 'C' && packet.data[1] == 'T');
    free_bytes(packet.data, packet.len);
    if (packet.message) free_bytes(packet.message, packet.message_len);
    assert(dlclose(library) == 0);
    puts("PASS: dynamically loaded MORK bridge keeps packets alive after source release");
    return 0;
}

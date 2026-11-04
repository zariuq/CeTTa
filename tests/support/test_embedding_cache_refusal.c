#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>

static atomic_uint refused_cleanup_registrations;
int __real_pthread_setspecific(pthread_key_t key, const void *value);
int __wrap_pthread_setspecific(pthread_key_t key, const void *value) {
    if (value) {
        atomic_fetch_add(&refused_cleanup_registrations, 1u);
        return ENOMEM;
    }
    return __real_pthread_setspecific(key, value);
}

#define main embedding_consumer_main
#include "../test_embedding.c"
#undef main

int main(void) {
    int status = embedding_consumer_main();
    assert(atomic_load(&refused_cleanup_registrations) > 0u);
    puts("embedding: refused thread-cache cleanup registration preserves answers and lifetimes");
    return status;
}

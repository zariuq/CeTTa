#include "host_notice.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <unistd.h>

struct CettaHostNoticeRegistration {
    int fd;
    struct CettaHostNoticeRegistration *next;
};
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_uint_fast64_t generation;
static CettaHostNoticeRegistration *registrations;

uint64_t cetta_host_notice_generation(void) {
    return atomic_load_explicit(&generation, memory_order_acquire);
}
void cetta_host_notice_publish(void) {
    atomic_fetch_add_explicit(&generation, 1, memory_order_release);
    pthread_mutex_lock(&lock);
    for (CettaHostNoticeRegistration *r = registrations; r; r = r->next) {
        char byte = 1; ssize_t status;
        do { status = write(r->fd, &byte, 1); } while (status < 0 && errno == EINTR);
        /* A full pipe already carries readiness; generation is authoritative. */
    }
    pthread_mutex_unlock(&lock);
}
CettaHostNoticeRegistration *cetta_host_notice_register(int fd) {
    CettaHostNoticeRegistration *r = malloc(sizeof(*r));
    if (!r) return NULL;
    r->fd = fd;
    pthread_mutex_lock(&lock);
    r->next = registrations; registrations = r;
    pthread_mutex_unlock(&lock);
    return r;
}
void cetta_host_notice_unregister(CettaHostNoticeRegistration *r) {
    if (!r) return;
    pthread_mutex_lock(&lock);
    CettaHostNoticeRegistration **p = &registrations;
    while (*p && *p != r) p = &(*p)->next;
    if (*p) *p = r->next;
    pthread_mutex_unlock(&lock);
    free(r);
}

#define _GNU_SOURCE
#include "proc_owner.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifndef __EMSCRIPTEN__
typedef struct OwnedChild {
    uint64_t id;
    CettaProcChild *child;
    bool abandoned, ready;
    struct OwnedChild *next;
} OwnedChild;

struct CettaProcOwner {
    pthread_t thread;
    pthread_mutex_t lock;
    int commands[2], wake_fd;
    bool stopping;
    OwnedChild *children;
};

static uint64_t monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u;
}

static void notify(int fd) {
    char byte = 1;
    ssize_t result;
    do { result = write(fd, &byte, 1); } while (result < 0 && errno == EINTR);
    /* EAGAIN means a notification is already readable. */
}

static OwnedChild *find(CettaProcOwner *owner, uint64_t id) {
    for (OwnedChild *r = owner->children; r; r = r->next)
        if (r->id == id && !r->abandoned) return r;
    return NULL;
}

static void *supervise(void *argument) {
    CettaProcOwner *owner = argument;
    for (;;) {
        char bytes[256];
        while (read(owner->commands[0], bytes, sizeof(bytes)) > 0) {}
        pthread_mutex_lock(&owner->lock);
        size_t count = 1;
        uint64_t now = monotonic_ms(), due = UINT64_MAX;
        OwnedChild **link = &owner->children;
        while (*link) {
            OwnedChild *r = *link;
            if (owner->stopping) r->abandoned = true;
            /* Cancellation owns the group while its unreaped leader still
             * reserves the identity. Never signal a group after reaping. */
            if (r->abandoned && !r->ready)
                (void)cetta_proc_signal(r->child, SIGKILL);
            if (!r->ready && cetta_proc_poll(r->child)) {
                r->ready = true;
                if (!r->abandoned) notify(owner->wake_fd);
            }
            if (r->ready && r->abandoned) {
                *link = r->next;
                cetta_proc_free(r->child); free(r);
                continue;
            }
            if (!r->ready) {
                int fd = cetta_proc_descriptor(r->child);
                if (fd >= 0) ++count;
                else if (now + 20 < due) due = now + 20;
                uint64_t deadline = cetta_proc_deadline(r->child);
                if (deadline < due) due = deadline;
            }
            link = &r->next;
        }
        if (owner->stopping && !owner->children) {
            pthread_mutex_unlock(&owner->lock);
            return NULL;
        }
        struct pollfd *fds = calloc(count, sizeof(*fds));
        if (fds) {
            count = 1;
            fds[0] = (struct pollfd){owner->commands[0], POLLIN, 0};
            for (OwnedChild *r = owner->children; r; r = r->next) {
                int fd = cetta_proc_descriptor(r->child);
                if (!r->ready && fd >= 0) fds[count++] = (struct pollfd){fd, POLLIN, 0};
            }
        }
        pthread_mutex_unlock(&owner->lock);
        now = monotonic_ms();
        int timeout = due == UINT64_MAX ? -1 : due <= now ? 0
                    : due - now > INT_MAX ? INT_MAX : (int)(due - now);
        if (fds) { (void)poll(fds, count, timeout); free(fds); }
        else {
            /* Still supervise children if allocating the optional pidfd set
             * fails. The command fd stays usable and no deadline is lost. */
            struct pollfd command = {owner->commands[0], POLLIN, 0};
            (void)poll(&command, 1, timeout < 0 || timeout > 20 ? 20 : timeout);
        }
    }
}

CettaProcOwner *cetta_proc_owner_new(int wake_fd) {
    CettaProcOwner *owner = calloc(1, sizeof(*owner));
    if (!owner) return NULL;
    owner->wake_fd = wake_fd;
    if (pthread_mutex_init(&owner->lock, NULL)) { free(owner); return NULL; }
    if (pipe2(owner->commands, O_NONBLOCK | O_CLOEXEC)) {
        pthread_mutex_destroy(&owner->lock); free(owner); return NULL;
    }
    sigset_t blocked, previous;
    sigfillset(&blocked);
    int mask_error = pthread_sigmask(SIG_SETMASK, &blocked, &previous);
    int start_error = mask_error ? mask_error
        : pthread_create(&owner->thread, NULL, supervise, owner);
    if (!mask_error) (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
    if (start_error) {
        close(owner->commands[0]); close(owner->commands[1]);
        pthread_mutex_destroy(&owner->lock); free(owner); return NULL;
    }
    return owner;
}

bool cetta_proc_owner_add(CettaProcOwner *owner, uint64_t id, CettaProcChild *child) {
    if (!owner || !child) return false;
    OwnedChild *r = calloc(1, sizeof(*r));
    if (!r) return false;
    r->id = id; r->child = child;
    pthread_mutex_lock(&owner->lock);
    bool accepted = !owner->stopping && !find(owner, id);
    if (accepted) { r->next = owner->children; owner->children = r; notify(owner->commands[1]); }
    pthread_mutex_unlock(&owner->lock);
    if (!accepted) free(r);
    return accepted;
}

bool cetta_proc_owner_status(CettaProcOwner *owner, uint64_t id, CettaProcSnapshot *out) {
    if (!owner || !out) return false;
    pthread_mutex_lock(&owner->lock);
    OwnedChild *r = find(owner, id);
    if (r) cetta_proc_snapshot(r->child, out);
    pthread_mutex_unlock(&owner->lock);
    return r != NULL;
}

bool cetta_proc_owner_signal(CettaProcOwner *owner, uint64_t id, int number) {
    if (!owner) return false;
    pthread_mutex_lock(&owner->lock);
    OwnedChild *r = find(owner, id);
    bool sent = r && !r->ready && cetta_proc_signal(r->child, number);
    if (sent) notify(owner->commands[1]);
    pthread_mutex_unlock(&owner->lock);
    return sent;
}

void cetta_proc_owner_abandon(CettaProcOwner *owner, uint64_t id) {
    if (!owner) return;
    pthread_mutex_lock(&owner->lock);
    OwnedChild *r = find(owner, id);
    if (r) {
        r->abandoned = true;
        if (!r->ready) (void)cetta_proc_signal(r->child, SIGKILL);
        notify(owner->commands[1]);
    }
    pthread_mutex_unlock(&owner->lock);
}

void cetta_proc_owner_free(CettaProcOwner *owner) {
    if (!owner) return;
    pthread_mutex_lock(&owner->lock);
    owner->stopping = true;
    notify(owner->commands[1]);
    pthread_mutex_unlock(&owner->lock);
    pthread_join(owner->thread, NULL);
    close(owner->commands[0]); close(owner->commands[1]);
    pthread_mutex_destroy(&owner->lock); free(owner);
}
#endif

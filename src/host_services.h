#ifndef CETTA_HOST_SERVICES_H
#define CETTA_HOST_SERVICES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum { CETTA_HOST_OK, CETTA_HOST_IO, CETTA_HOST_MEMORY, CETTA_HOST_INTERRUPTED } CettaHostFailure;
typedef struct {
    void *context;
    bool (*write)(void *, unsigned channel, const char *, size_t);
    bool (*flush)(void *, unsigned channel);
    bool (*poll)(void *);
} CettaHostServices;
typedef struct CettaHostScope {
    struct CettaHostScope *previous;
    CettaHostServices services;
    unsigned until_poll;
    bool polling;
    CettaHostFailure failure;
} CettaHostScope;
typedef struct {
    FILE *stream;
    char *bytes;
    size_t length, capacity;
    unsigned channel;
    int error;
    CettaHostScope *scope;
} CettaHostOutput;

void cetta_host_scope_enter(CettaHostScope *, const CettaHostServices *);
void cetta_host_scope_leave(CettaHostScope *);
void cetta_host_fail(CettaHostFailure);
CettaHostFailure cetta_host_failure(void);
bool cetta_host_poll(void) __attribute__((weak));
bool cetta_host_poll_immediate(void) __attribute__((weak));
bool cetta_host_output_open(CettaHostOutput *, unsigned) __attribute__((weak));
bool cetta_host_output_close(CettaHostOutput *) __attribute__((weak));
bool cetta_host_output_flush(unsigned) __attribute__((weak));

/* Standalone scalar tools retain ordinary stdio without a host owner. */
static inline bool cetta_output_open(CettaHostOutput *output, unsigned channel) {
    if (cetta_host_output_open) return cetta_host_output_open(output, channel);
    *output = (CettaHostOutput){.stream = channel ? stderr : stdout};
    return true;
}
static inline bool cetta_output_close(CettaHostOutput *output) {
    return cetta_host_output_close ? cetta_host_output_close(output) : fflush(output->stream) == 0;
}
static inline bool cetta_output_flush(unsigned channel) {
    return cetta_host_output_flush ? cetta_host_output_flush(channel) : fflush(channel ? stderr : stdout) == 0;
}
#endif

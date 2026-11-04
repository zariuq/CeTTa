#define _GNU_SOURCE
#include "host_services.h"
#include "native_memory.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local CettaHostScope *current;

void cetta_host_scope_enter(CettaHostScope *scope, const CettaHostServices *services) {
    *scope = (CettaHostScope){.previous = current, .services = services ? *services : (CettaHostServices){0}};
    current = scope;
}
void cetta_host_scope_leave(CettaHostScope *scope) { current = scope->previous; }
void cetta_host_fail(CettaHostFailure failure) {
    if (current && current->failure == CETTA_HOST_OK) current->failure = failure;
}
CettaHostFailure cetta_host_failure(void) { return current ? current->failure : CETTA_HOST_OK; }
bool cetta_host_poll_immediate(void) {
    if (!current) return false;
    if (current->failure != CETTA_HOST_OK) return true;
    if (!current->services.poll || current->polling) return false;
    current->polling = true;
    bool stop = current->services.poll(current->services.context);
    current->polling = false;
    if (stop) cetta_host_fail(CETTA_HOST_INTERRUPTED);
    return stop;
}
bool cetta_host_poll(void) {
    if (!current) return false;
    if (current->failure != CETTA_HOST_OK) return true;
    if (current->until_poll) { current->until_poll--; return false; }
    current->until_poll = 1023u;
    return cetta_host_poll_immediate();
}
static ssize_t capture(void *pointer, const char *bytes, size_t length) {
    CettaHostOutput *output = pointer;
    if (output->error || length > SIZE_MAX - output->length) {
        output->error = ENOMEM; errno = ENOMEM; return -1;
    }
    size_t needed = output->length + length;
    if (needed > output->capacity) {
        size_t capacity = output->capacity ? output->capacity : 128u;
        while (capacity < needed && capacity <= SIZE_MAX / 2u) capacity *= 2u;
        if (capacity < needed) capacity = needed;
        char *next = realloc(output->bytes, capacity);
        if (!next) { output->error = ENOMEM; return -1; }
        output->bytes = next; output->capacity = capacity;
    }
    if (length) memcpy(output->bytes + output->length, bytes, length);
    output->length += length;
    return (ssize_t)length;
}
bool cetta_host_output_open(CettaHostOutput *output, unsigned channel) {
    *output = (CettaHostOutput){.stream = channel ? stderr : stdout, .channel = channel};
    if (!current || !current->services.write) return true;
    output->scope = current;
    output->stream = fopencookie(output, "w", (cookie_io_functions_t){.write = capture});
    if (!output->stream) { cetta_host_fail(CETTA_HOST_MEMORY); return false; }
    setvbuf(output->stream, NULL, _IONBF, 0u);
    return true;
}
bool cetta_host_output_close(CettaHostOutput *output) {
    if (!output->scope) return fflush(output->stream) == 0;
    bool ok = !ferror(output->stream);
    if (fclose(output->stream)) ok = false;
    if (output->error) { cetta_host_fail(CETTA_HOST_MEMORY); ok = false; }
    if (ok && output->length)
        ok = output->scope->services.write(output->scope->services.context,
            output->channel, output->bytes, output->length);
    if (ok && output->scope->services.flush)
        ok = output->scope->services.flush(output->scope->services.context, output->channel);
    free(output->bytes);
    if (!ok) cetta_host_fail(CETTA_HOST_IO);
    return ok;
}
bool cetta_host_output_flush(unsigned channel) {
    if (!current || !current->services.write) return fflush(channel ? stderr : stdout) == 0;
    bool ok = !current->services.flush || current->services.flush(current->services.context, channel);
    if (!ok) cetta_host_fail(CETTA_HOST_IO);
    return ok;
}

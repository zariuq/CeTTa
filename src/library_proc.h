#ifndef CETTA_LIBRARY_PROC_H
#define CETTA_LIBRARY_PROC_H

#include "atom.h"

/*
 * Dispatch the internal operation exported by lib/proc.metta: run a program
 * as a child process with an explicit argument vector, working directory and
 * environment. Inheritance, output merging, observation, timeout and capture
 * size are caller-selected; zero timeout/capture size means unlimited.
 */
Atom *cetta_proc_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs);

/* Asynchronous children write to caller-selected files. These handles own
 * native bytes and descriptors only; no evaluator arena survives the call. */
typedef struct CettaProcChild CettaProcChild;
typedef struct {
    int64_t pid;
    uint64_t elapsed_ms;
    int status, error;
    bool finished, timed_out;
} CettaProcSnapshot;
CettaProcChild *cetta_proc_start(Atom *argv, const char *cwd, Atom *env,
    uint64_t timeout_ms, const char *out, const char *err, char *error, size_t error_size);
bool cetta_proc_poll(CettaProcChild *child);
int cetta_proc_descriptor(const CettaProcChild *child);
uint64_t cetta_proc_deadline(const CettaProcChild *child);
bool cetta_proc_signal(CettaProcChild *child, int signal_number);
void cetta_proc_snapshot(const CettaProcChild *child, CettaProcSnapshot *out);
Atom *cetta_proc_information(Arena *arena, CettaProcChild *child);
Atom *cetta_proc_outcome(Arena *arena, CettaProcChild *child);
void cetta_proc_free(CettaProcChild *child);
void cetta_proc_observe_wait(const char *path, uint64_t sequence);

#endif

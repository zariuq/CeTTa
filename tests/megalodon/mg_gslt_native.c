/*
 * mg-gslt-native (--tokens | --items) [--trace TRACE] [-I SIGNATURE]... FILE
 *
 * Reads FILE, after its signatures, with the Megalodon syntax presentations
 * (langdef/megalodon/) compiled to C (native/megalodon/mg_gslt_reader.h),
 * and prints each token or item as the PeTTa reading prints it
 * (tests/megalodon/compare_gslt_reader.py, READER_FUNCTIONS), then
 * then a mark: mg-run-end; mg-run-error, Megalodon's error, its message on
 * stderr; mg-run-refused, when the presentations have no reading; or
 * mg-run-malformed (exit 2), a step outside its contract.
 * The reading runs on a thread with a large stack: the compiled relations
 * recurse as deep as the terms they read are nested.  Built with -DGN_TRACE
 * (mg-gslt-native-trace), --trace writes the run's trace to TRACE
 * (gslt_native_runtime_v1.h).
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mg_gslt_reader.h"

/* Loops in the input run in constant stack; a term's nesting still takes
 * stack in proportion to its depth. */
#ifndef MG_GSLT_NATIVE_STACK
#define MG_GSLT_NATIVE_STACK ((size_t)2u << 30)
#endif

typedef struct {
    bool items;
    const char **files;
    int count;
    int status;
    MgGsltReadStatus read;
    char message[512];
    GnCtx *cx;                /* the values printed */
    FILE *trace;
} Job;

static void trace_to(Job *job) {
#ifdef GN_TRACE
    if (job->trace)
        gn_ctx_set_trace(job->cx, job->trace);
#else
    (void)job;
#endif
}

static void say(void *ctx, GnVal *v) {
    gn_print(((Job *)ctx)->cx, stdout, v);
    fputc('\n', stdout);
}

static void read_tokens(Job *job) {
    GnSourceV1 *src = mg_gslt_source_load(job->files[job->count - 1]);
    job->cx = src ? mg_gslt_reader_context() : NULL;
    if (!job->cx) {
        snprintf(job->message, sizeof job->message, "cannot read %s", job->files[job->count - 1]);
        job->status = 2;
    } else if (trace_to(job), !GN_TRY(job->cx, job->read = mg_gslt_read_tokens(job->cx, src, say, job))) {
        snprintf(job->message, sizeof job->message, "out of memory");
        job->status = 2;
    } else if (job->read == MG_GSLT_READ_ERROR) {
        snprintf(job->message, sizeof job->message, "%s", MG_GSLT_LEXING_FAILURE);
    }
    gn_ctx_free(job->cx);
    mg_gslt_source_free(src);
}

/* The signatures read through, then the file's items printed.  The reader
 * starts with its first input, after its trace is set. */
static void read_items(Job *job) {
    char **bytes = calloc((size_t)job->count, sizeof *bytes);
    MgSource *source = bytes ? mg_source_new(NULL, 0u) : NULL;
    if (bytes && !source) {
        snprintf(job->message, sizeof job->message, "out of memory");
        job->status = 2;
    }
    if (source) {
        job->cx = mg_source_context(source);
        trace_to(job);
    }
    job->read = MG_GSLT_READ_END;
    for (int f = 0; source && f < job->count && job->read == MG_GSLT_READ_END; f++) {
        size_t len;
        if (!(bytes[f] = mg_gslt_file_bytes(job->files[f], &len))) {
            snprintf(job->message, sizeof job->message, "cannot read %s", job->files[f]);
            job->status = 2;
            break;
        }
        mg_source_set_input(source, bytes[f], len);
        job->read = mg_source_read_items(source, f + 1 == job->count ? say : NULL, job);
        if (job->read != MG_GSLT_READ_END)
            snprintf(job->message, sizeof job->message, "%s", mg_source_error(source));
    }
    if (!bytes) {
        snprintf(job->message, sizeof job->message, "out of memory");
        job->status = 2;
    }
    mg_source_free(source);
    for (int f = 0; bytes && f < job->count; f++)
        free(bytes[f]);
    free(bytes);
}

static void run(Job *job) {
    if (job->items)
        read_items(job);
    else
        read_tokens(job);
    if (job->status)
        return;
    switch (job->read) {
    case MG_GSLT_READ_END: puts("mg-run-end"); break;
    case MG_GSLT_READ_ERROR: puts("mg-run-error"); break;
    case MG_GSLT_READ_REFUSED: puts("mg-run-refused"); break;
    default: puts("mg-run-malformed"); job->status = 2; break;
    }
}

static void *thread_main(void *arg) {
    run(arg);
    return NULL;
}

int main(int argc, char **argv) {
    Job job = {.files = calloc((size_t)argc, sizeof(char *))};
    int mode = -1;
    for (int i = 1; job.files && i < argc; i++) {
        if (strcmp(argv[i], "--tokens") == 0)
            mode = 0;
        else if (strcmp(argv[i], "--items") == 0)
            mode = 1;
        else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc)
            job.files[job.count++] = argv[++i];
        else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
#ifdef GN_TRACE
            if (!(job.trace = fopen(argv[++i], "w"))) {
                fprintf(stderr, "mg-gslt-native: cannot write %s\n", argv[i]);
                free(job.files);
                return 2;
            }
#else
            fprintf(stderr, "mg-gslt-native: built without GN_TRACE; --trace needs mg-gslt-native-trace\n");
            free(job.files);
            return 64;
#endif
        }
        else
            job.files[job.count++] = argv[i];
    }
    if (mode < 0 || job.count == 0) {
        fprintf(stderr, "usage: mg-gslt-native (--tokens | --items) [--trace TRACE] [-I SIGNATURE]... FILE\n");
        free(job.files);
        return 64;
    }
    job.items = mode == 1;
    pthread_attr_t attr;
    pthread_t th;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, MG_GSLT_NATIVE_STACK);
    if (pthread_create(&th, &attr, thread_main, &job) != 0) {
        fprintf(stderr, "mg-gslt-native: cannot start the reading\n");
        free(job.files);
        return 2;
    }
    pthread_join(th, NULL);
    fflush(stdout);
    if (job.message[0])
        fprintf(stderr, "%s%s\n", job.status ? "mg-gslt-native: " : "", job.message);
    if (job.trace && fclose(job.trace) != 0 && !job.status)
        job.status = 2;
    free(job.files);
    return job.status;
}

/*
 * mg-gslt-native (--tokens | --items) [--trace PREFIX] [-I SIGNATURE]... FILE
 *
 * Reads FILE, after its signatures, with the Megalodon syntax presentations
 * (langdef/megalodon/) compiled to C (native/megalodon/mg_gslt_reader.h),
 * and prints each token or item as the PeTTa reading prints it
 * (tests/megalodon/compare_gslt_reader.py, READER_FUNCTIONS), then
 * then a mark: mg-run-end; mg-run-error, Megalodon's error, its message on
 * stderr; mg-run-refused, when the presentations have no reading; or
 * mg-run-malformed (exit 2), a step outside its contract.
 * The reading runs on a thread with a large stack: the compiled relations
 * recurse as deep as the terms they read are nested.  --trace PREFIX writes
 * each step's derivation, as rulec's trace events, to PREFIX.ENTRY
 * (mg_gslt_reader_trace).
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
    const char *trace;        /* the trace's path prefix, or NULL */
} Job;

/* A reading in PeTTa's spelling: an application (and the unread input, read as
 * the list it is) as (NAME CHILD ...), a nullary one as its name.  Iterative:
 * readings nest as deeply as the text they read. */
static void say(void *ctx, const MgGsltValue *v) {
    (void)ctx;
    typedef struct { const MgGsltValue *v; size_t k; MgGsltNode n; } Frame;
    size_t len = 0, cap = 64;
    Frame *st = malloc(cap * sizeof *st);
    if (!st) return;
    st[len++] = (Frame){v, 0, {0}};
    mg_gslt_node(v, &st[0].n);
    while (len) {
        Frame *f = &st[len - 1];
        const MgGsltNode *n = &f->n;
        bool apply = n->kind == MG_GSLT_APPLY || n->kind == MG_GSLT_UNREAD;
        if (!apply || n->arity == 0) {
            if (n->kind == MG_GSLT_INT) printf("%lld", (long long)n->integer);
            else if (n->kind == MG_GSLT_STRING) printf("\"%.*s\"", (int)n->len, n->name);
            else printf("%.*s", (int)n->len, n->name);
            len--;
            continue;
        }
        if (f->k == n->arity) { putchar(')'); len--; continue; }
        if (f->k == 0) printf("(%.*s", (int)n->len, n->name);
        putchar(' ');
        const MgGsltValue *kid = mg_gslt_child(f->v, f->k++);
        if (len == cap) {
            cap *= 2;
            Frame *g = realloc(st, cap * sizeof *g);
            if (!g) { free(st); return; }
            st = g;
        }
        st[len] = (Frame){kid, 0, {0}};
        mg_gslt_node(kid, &st[len].n);
        len++;
    }
    free(st);
    putchar('\n');
}

static void read_tokens(Job *job) {
    size_t len;
    char *bytes = mg_gslt_file_bytes(job->files[job->count - 1], &len);
    if (!bytes) {
        snprintf(job->message, sizeof job->message, "cannot read %s", job->files[job->count - 1]);
        job->status = 2;
        return;
    }
    job->read = mg_gslt_read_tokens(bytes, len, say, job);
    if (job->read == MG_GSLT_READ_ERROR)
        snprintf(job->message, sizeof job->message, "%s", MG_GSLT_LEXING_FAILURE);
    free(bytes);
}

/* The signatures read through, then the file's items printed. */
static void read_items(Job *job) {
    char **bytes = calloc((size_t)job->count, sizeof *bytes);
    MgSource *source = bytes ? mg_source_new(NULL, 0u) : NULL;
    if (bytes && !source) {
        snprintf(job->message, sizeof job->message, "out of memory");
        job->status = 2;
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
    if (job->trace && !mg_gslt_reader_trace(job->trace)) {
        snprintf(job->message, sizeof job->message, "cannot write the trace %s", job->trace);
        job->status = 2;
        return;
    }
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
        else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc)
            job.trace = argv[++i];
        else
            job.files[job.count++] = argv[i];
    }
    if (mode < 0 || job.count == 0) {
        fprintf(stderr, "usage: mg-gslt-native (--tokens | --items) [--trace PREFIX] [-I SIGNATURE]... FILE\n");
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
    free(job.files);
    return job.status;
}

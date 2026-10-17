/* mm0_check - run the native MM0 verifier on one MMB file, for tests and
 * for comparison with mm0-c on the same inputs.
 *
 *   mm0_check [options] FILE.mmb [FILE.mm0]      (no FILE.mm0: read stdin)
 *     --no-spec      check the MMB file alone (as mm0-c built with NO_PARSER)
 *     --check-only   retain nothing, as mm0-c does
 *     --budget N     stop with LIMIT after N steps
 *     --repeat N     verify N times (for timing), each into a fresh theory
 *     --mmap         map the MMB file instead of reading it (mm0-c maps it)
 *
 * Exit status: the Mm0Status value (mm0.h): 0 verified; 1 MALFORMED;
 * 2 REFUSED; 3 UNSUPPORTED; 4 LIMIT; 5 NOMEM; 6 INTERRUPTED; 7 FOREIGN;
 * 64 usage; 66 unreadable input. */
#define _POSIX_C_SOURCE 200809L
#include "mm0.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned char *read_all(FILE *f, size_t *len) {
    size_t cap = 1 << 16, n = 0;
    unsigned char *buf = malloc(cap);
    if (!buf) return NULL;
    for (;;) {
        if (n == cap) {
            unsigned char *grown = cap < (size_t)-1 / 2 ? realloc(buf, cap * 2) : NULL;
            if (!grown) { free(buf); return NULL; }
            buf = grown;
            cap *= 2;
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if (got == 0) break;
    }
    if (ferror(f)) { free(buf); return NULL; }
    *len = n;
    return buf;
}

static unsigned char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    unsigned char *data = read_all(f, len);
    fclose(f);
    return data;
}

static unsigned char *map_file(const char *path, size_t *len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    void *p = MAP_FAILED;
    if (fstat(fd, &st) == 0 && st.st_size > 0) {
        *len = (size_t)st.st_size;
        p = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    close(fd);
    return p == MAP_FAILED ? NULL : p;
}

int main(int argc, char **argv) {
    Mm0Options options;
    mm0_options_init(&options);
    long repeat = 1;
    bool mapped = false;
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; ++i) {
        if (!strcmp(argv[i], "--no-spec")) options.without_spec = true;
        else if (!strcmp(argv[i], "--check-only")) options.check_only = true;
        else if (!strcmp(argv[i], "--budget") && i + 1 < argc) options.budget = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--mmap")) mapped = true;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 64; }
    }
    if (i >= argc || argc - i > 2 || repeat < 1) {
        fprintf(stderr, "usage: mm0_check [--no-spec] [--check-only] [--budget N] [--repeat N] [--mmap] "
                "FILE.mmb [FILE.mm0]\n");
        return 64;
    }
    size_t mmb_len = 0, spec_len = 0;
    unsigned char *mmb = mapped ? map_file(argv[i], &mmb_len) : read_file(argv[i], &mmb_len);
    if (!mmb) { fprintf(stderr, "cannot read %s\n", argv[i]); return 66; }
    unsigned char *spec = NULL;
    if (!options.without_spec) {
        spec = argc - i == 2 ? read_file(argv[i + 1], &spec_len) : read_all(stdin, &spec_len);
        if (!spec) {
            fprintf(stderr, "cannot read the specification\n");
            if (mapped) munmap(mmb, mmb_len); else free(mmb);
            return 66;
        }
    }
    Mm0Error error;
    Mm0Status status = MM0_OK;
    Mm0TheoryStats stats = {0};
    for (long r = 0; r < repeat && status == MM0_OK; ++r) {
        Mm0Theory *theory = mm0_theory_new();
        if (!theory) { status = MM0_NOMEM; error.reason = "theory allocation"; break; }
        status = mm0_load_mmb(theory, mmb, mmb_len, (const char *)spec, spec_len, &options, &error);
        stats = mm0_theory_stats(theory);
        mm0_theory_free(theory);
    }
    if (status == MM0_OK) {
        printf("verified: %zu sorts, %zu terms, %zu theorems; %zu bytes retained\n",
               stats.sorts, stats.terms, stats.theorems, stats.bytes);
    } else {
        fprintf(stderr, "%s: %s", mm0_status_name(status), error.reason ? error.reason : "");
        if (error.offset >= 0) fprintf(stderr, " (MMB offset %lld)", (long long)error.offset);
        if (error.line) fprintf(stderr, " (.mm0 line %u column %u)", error.line, error.column);
        if (error.decl[0]) fprintf(stderr, " [%s]", error.decl);
        fputc('\n', stderr);
    }
    if (mapped) munmap(mmb, mmb_len); else free(mmb);
    free(spec);
    return (int)status;
}

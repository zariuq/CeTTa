#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Observe flushed markers through a pipe; timings include the public call,
 * result-pattern matching and observation overhead. These are wall times,
 * not exclusive CPU attribution. Missing end markers never count as success. */
int main(int argc, char **argv) {
    static const char *const default_stages[] = {
        "read", "declarations", "lower", "validate", "denote", "load", "parse", "project"
    };
    if (argc < 2) return 2;
    const char *const *stages = argc > 2 ? (const char *const *)(argv + 2) : default_stages;
    size_t count = argc > 2 ? (size_t)(argc - 2) : sizeof(default_stages) / sizeof(default_stages[0]);
    FILE *receipt = fopen(argv[1], "wx");
    if (!receipt) { perror(argv[1]); return 2; }
    fputs("stage\tstatus\telapsed_s\n", receipt);
    size_t stage = 0;
    bool active = false, valid = true;
    struct timespec start = {0}, now;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    while ((length = getline(&line, &capacity, stdin)) >= 0) {
        if (fwrite(line, 1, (size_t)length, stdout) != (size_t)length)
            valid = false;
        if (strncmp(line, "(EbnfStage ", 11) != 0) continue;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            valid = false;
            break;
        }
        char expected[96];
        if (stage >= count) {
            valid = false;
            continue;
        }
        snprintf(expected, sizeof(expected), "(EbnfStage %s %s)\n",
                 active ? "end" : "begin", stages[stage]);
        if (strcmp(line, expected) != 0) {
            valid = false;
            continue;
        }
        if (!active) {
            start = now;
            active = true;
        } else {
            double seconds = (double)(now.tv_sec - start.tv_sec) +
                (double)(now.tv_nsec - start.tv_nsec) / 1e9;
            fprintf(receipt, "%s\tcompleted\t%.9f\n", stages[stage], seconds);
            if (fflush(receipt) != 0) valid = false;
            active = false;
            ++stage;
        }
    }
    if (active) fprintf(receipt, "%s\tincomplete\tNA\n", stages[stage]);
    if (ferror(stdin) || ferror(receipt) || fflush(stdout) != 0) valid = false;
    if (fclose(receipt) != 0) valid = false;
    free(line);
    return valid && !active && stage == count ? 0 : 1;
}

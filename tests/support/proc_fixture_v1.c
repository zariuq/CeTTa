#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int write_all(int fd, const void *buffer, size_t size) {
    const unsigned char *bytes = buffer;
    while (size) {
        ssize_t count = write(fd, bytes, size);
        if (count > 0) { bytes += count; size -= (size_t)count; }
        else if (count < 0 && errno == EINTR) continue;
        else return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
#ifdef FIXTURE_DEFAULT
    char *defaults[] = {argv[0], FIXTURE_DEFAULT, NULL};
    if (argc == 1) { argc = 2; argv = defaults; }
#endif
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "control")) { puts("PASS: fixture"); return 0; }
    if (!strcmp(argv[1], "mutant")) {
        fputs("fixture assertion: occurrence preserved\n", stderr);
        raise(SIGABRT); return 2;
    }
    if (!strcmp(argv[1], "bytes")) {
        unsigned char bytes[256];
        for (unsigned i = 0; i < 256; i++) bytes[i] = (unsigned char)i;
        return write_all(1, bytes, sizeof bytes) || write_all(2, bytes, sizeof bytes);
    }
    if (!strcmp(argv[1], "duplex")) {
        unsigned char bytes[4096]; memset(bytes, 'e', sizeof bytes);
        for (unsigned i = 0; i < 128; i++)
            if (write_all(2, bytes, sizeof bytes)) return 3;
        ssize_t count;
        while ((count = read(0, bytes, sizeof bytes)) != 0) {
            if (count < 0) { if (errno == EINTR) continue; return 4; }
            if (write_all(1, bytes, (size_t)count)) return 5;
        }
        return 0;
    }
    if (!strcmp(argv[1], "sleep")) {
        if (argc > 2) {
            FILE *file = fopen(argv[2], "w");
            if (!file) return 3;
            fprintf(file, "%ld\n", (long)getpid()); fclose(file);
        }
        struct timespec duration = {.tv_sec = 60};
        nanosleep(&duration, NULL); return 0;
    }
    if (!strcmp(argv[1], "signal")) { raise(SIGTERM); return 2; }
    if (!strcmp(argv[1], "exit")) return argc > 2 ? atoi(argv[2]) : 7;
    if (!strcmp(argv[1], "environment")) {
        char *cwd = getcwd(NULL, 0);
        const char *value = getenv("CETTA_PROC_FIXTURE");
        if (!cwd) return 2;
        printf("%s\n%s\n", cwd, value ? value : "absent"); free(cwd); return 0;
    }
    return 2;
}

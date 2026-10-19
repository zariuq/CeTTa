/* SPDX-License-Identifier: MIT
 * Synthetic-provider qualification driver. Never use it with real tokens. */
#define _GNU_SOURCE
#include "siwc.h"
#include "siwc_crypto.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

static char *load(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END)) return NULL;
    long n = ftell(file);
    if (n < 0 || n > 1048576 || fseek(file, 0, SEEK_SET)) { fclose(file); return NULL; }
    char *text = malloc((size_t)n + 1);
    if (!text || fread(text, 1, (size_t)n, file) != (size_t)n) { free(text); fclose(file); return NULL; }
    text[n] = 0; fclose(file); return text;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 2 && !strcmp(argv[1], "crypto")) {
        char challenge[44], random[44];
        if (!cetta_siwc_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", challenge) || !cetta_siwc_random(random)) return 1;
        printf("%s\n%s\n", challenge, random); return 0;
    }
    if (argc == 8 && !strcmp(argv[1], "verify")) {
        char *jwt = load(argv[2]), *keys = load(argv[3]);
        json_t *jwks = keys ? json_loads(keys, JSON_REJECT_DUPLICATES, NULL) : NULL;
        const char *error = NULL;
        json_t *claims = cetta_siwc_verify(jwt, jwks, argv[4], argv[5], argv[6], strtoll(argv[7], NULL, 10), &error);
        puts(error ? error : "ok"); json_decref(claims); json_decref(jwks); free(jwt); free(keys); return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "bearer")) {
        char *token = cetta_siwc_bearer(argv[2], argv[3]); puts(token ? "available" : "unavailable");
        cetta_siwc_secret_free(token); return 0;
    }
    if (argc != 6 || (strcmp(argv[1], "run") && strcmp(argv[1], "pair"))) return 2;
    if (!strcmp(argv[1], "pair")) {
        CettaSiwcOperation *first = cetta_siwc_start(argv[2], argv[3], 0, strtoull(argv[5], NULL, 10), -1);
        CettaSiwcOperation *second = cetta_siwc_start(argv[2], argv[3], 0, strtoull(argv[5], NULL, 10), -1);
        if (!first || !second) return 3;
        while (!cetta_siwc_ready(first) || !cetta_siwc_ready(second)) usleep(10000);
        char *a = cetta_siwc_result(first), *b = cetta_siwc_result(second);
        puts(a); puts(b); free(a); free(b); cetta_siwc_free(first); cetta_siwc_free(second); return 0;
    }
    CettaSiwcOperation *op = cetta_siwc_start(argv[2], argv[3], (unsigned)strtoul(argv[4], NULL, 10), strtoull(argv[5], NULL, 10), -1);
    if (!op) return 3;
    char *old = NULL;
    while (!cetta_siwc_ready(op)) {
        char *status = cetta_siwc_status(op);
        if (status && (!old || strcmp(old, status))) { puts(status); free(old); old = strdup(status); }
        free(status);
        struct pollfd input = {STDIN_FILENO, POLLIN, 0};
        if (poll(&input, 1, 10) > 0 && (input.revents & POLLIN)) {
            char command[32]; ssize_t n = read(STDIN_FILENO, command, sizeof(command));
            if (n > 0) { cetta_siwc_free(op); free(old); puts("cancelled"); return 0; }
        }
    }
    free(old); char *result = cetta_siwc_result(op); puts(result ? result : "missing-result");
    free(result); cetta_siwc_free(op); return 0;
}

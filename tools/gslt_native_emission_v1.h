#ifndef CETTA_GSLT_NATIVE_EMISSION_V1_H
#define CETTA_GSLT_NATIVE_EMISSION_V1_H

#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Shared byte I/O, output alias checks and C quoting for the native metadata
 * emitters. No language admission, rule selection or execution lives here. */

static unsigned char *read_bytes(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    long end;
    if (fseek(file, 0, SEEK_END) || (end = ftell(file)) < 0 ||
        (uintmax_t)end >= SIZE_MAX || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    unsigned char *bytes = malloc((size_t)end + 1u);
    bool ok = bytes && fread(bytes, 1u, (size_t)end, file) == (size_t)end;
    if (fclose(file)) ok = false;
    if (!ok) { free(bytes); return NULL; }
    bytes[end] = 0;
    *length = (size_t)end;
    return bytes;
}

static bool same_file(const char *left, const char *right) {
    if (!strcmp(left, right)) return true;
    struct stat a, b;
    return !stat(left, &a) && !stat(right, &b) &&
        a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

/* Resolve an output through its existing parent even before the file exists. */
static char *output_path(const char *path) {
    char *resolved = realpath(path, NULL);
    if (resolved) return resolved;
    struct stat status;
    if (!lstat(path, &status) && S_ISLNK(status.st_mode)) return NULL;
    char *copy = strdup(path);
    if (!copy) return NULL;
    char *slash = strrchr(copy, '/');
    const char *name = slash ? slash + 1u : copy;
    if (!*name || !strcmp(name, ".") || !strcmp(name, "..")) {
        free(copy); return NULL;
    }
    char *parent;
    if (slash) { *slash = 0; parent = realpath(*copy ? copy : "/", NULL); }
    else parent = realpath(".", NULL);
    if (parent) {
        size_t size = strlen(parent) + strlen(name) + 2u;
        resolved = malloc(size);
        if (resolved) (void)snprintf(resolved, size, "%s/%s", parent, name);
    }
    free(parent); free(copy);
    return resolved;
}

static void c_string(FILE *out, const char *text) {
    if (!text) { fputs("NULL", out); return; }
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\') fprintf(out, "\\%c", *p);
        else if (*p >= 32 && *p <= 126 && *p != '?') fputc(*p, out);
        else fprintf(out, "\\%03o", *p);
    }
    fputc('"', out);
}

static bool write_changed(const char *path, const char *bytes, size_t length) {
    size_t old_length = 0;
    unsigned char *old = read_bytes(path, &old_length);
    bool equal = old && length == old_length && !memcmp(old, bytes, length);
    free(old);
    if (equal) return true;
    FILE *out = fopen(path, "wb");
    if (!out) return false;
    bool ok = fwrite(bytes, 1u, length, out) == length;
    if (fclose(out)) ok = false;
    return ok;
}

static bool identifier(const char *text) {
    static const char *keywords[] = {"auto", "break", "case", "char", "const",
        "continue", "default", "do", "double", "else", "enum", "extern", "float",
        "for", "goto", "if", "inline", "int", "long", "register", "restrict",
        "return", "short", "signed", "sizeof", "static", "struct", "switch",
        "typedef", "union", "unsigned", "void", "volatile", "while", "_Alignas",
        "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
        "_Noreturn", "_Static_assert", "_Thread_local", NULL};
    if (!text || !(isalpha((unsigned char)*text) || *text == '_')) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        if (!(isalnum(*p) || *p == '_') || *p > 127u) return false;
    for (size_t i = 0; keywords[i]; i++) if (!strcmp(text, keywords[i])) return false;
    return true;
}

#endif

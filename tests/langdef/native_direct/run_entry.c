/*
 * run-entry: calls the entries of a GSLT composition compiled to C
 * (langdef-compile petta-direct --target c --c-prefix fixture).  Each line
 * of standard input is ENTRY INPUT..., ENTRY its RELATION:BITS and each
 * INPUT a value in PeTTa's spelling, or a source text "..." (with \xHH
 * and \" escapes) read as the source list langdef makes of a file: a
 * cursor over its bytes.  Each prints the call's answers as the PeTTa
 * target's entry collapses them: ((gslt:result:ENTRY OUTPUT...)) or ().
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native/gslt_native_runtime_v1.h"

extern const GnProgramV1 fixture_program;
extern const GnEntryV1 fixture_entries[];
extern const uint32_t fixture_entry_count;

/* A source text "...": its bytes into *src (malloc'd), *end past it. */
static bool parse_text(const char *t, GnSourceV1 *src, const char **end) {
    while (*t == ' ')
        t++;
    if (*t != '"')
        return false;
    uint8_t *bytes = malloc(strlen(t) + 1u);
    size_t n = 0;
    if (!bytes)
        return false;
    for (t++; *t && *t != '"'; t++) {
        if (t[0] == '\\' && t[1] == 'x' && t[2] && t[3]) {
            char hex[3] = {t[2], t[3], 0};
            bytes[n++] = (uint8_t)strtoul(hex, NULL, 16);
            t += 3;
        } else if (t[0] == '\\' && (t[1] == '"' || t[1] == '\\')) {
            bytes[n++] = (uint8_t)*++t;
        } else {
            bytes[n++] = (uint8_t)*t;
        }
    }
    if (*t != '"') {
        free(bytes);
        return false;
    }
    *src = (GnSourceV1){bytes, n};
    *end = t + 1;
    return true;
}

/* The call with out-of-memory caught, in a frame of its own (setjmp). */
static bool call_entry(GnCtx *cx, const GnEntryV1 *e, GnVal **in, GnVal **out, bool *answered) {
    return GN_TRY(cx, *answered = e->call(cx, in, out));
}

int main(void) {
    char line[65536];
    int status = 0;
    while (fgets(line, sizeof line, stdin)) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = 0;
        if (!line[0] || line[0] == ';')
            continue;
        char *space = strchr(line, ' ');
        size_t nlen = space ? (size_t)(space - line) : strlen(line);
        const GnEntryV1 *e = NULL;
        for (uint32_t i = 0; i < fixture_entry_count; i++)
            if (strlen(fixture_entries[i].name) == nlen && memcmp(fixture_entries[i].name, line, nlen) == 0)
                e = &fixture_entries[i];
        if (!e) {
            printf("no entry %.*s\n", (int)nlen, line);
            status = 1;
            continue;
        }
        GnCtx *cx = gn_ctx_new(&fixture_program);
        GnVal *in[16] = {0}, *out[16] = {0};
        GnSourceV1 texts[16];
        uint32_t text_count = 0;
        const char *t = space ? space : line + nlen;
        bool ok = e->inputs <= 16u && e->outputs <= 16u;
        for (uint32_t k = 0; ok && k < e->inputs; k++) {
            if (parse_text(t, &texts[text_count], &t)) {
                in[k] = gn_cursor(cx, &texts[text_count], 0u);
                text_count++;
            } else {
                ok = (in[k] = gn_parse(cx, t, &t)) != NULL;
            }
        }
        if (!ok) {
            printf("bad input for %s\n", e->name);
            status = 1;
        } else if (!call_entry(cx, e, in, out, &ok)) {
            printf("out of memory\n");
            status = 1;
        } else if (!ok) {
            printf("()\n");
        } else {
            printf("((gslt:result:%s", e->name);
            for (uint32_t k = 0; k < e->outputs; k++) {
                putchar(' ');
                gn_print(cx, stdout, out[k]);
            }
            printf("))\n");
        }
        gn_ctx_free(cx);
        for (uint32_t k = 0; k < text_count; k++)
            free((void *)texts[k].bytes);
    }
    return status;
}

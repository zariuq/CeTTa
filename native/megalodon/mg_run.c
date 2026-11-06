#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_run.h"

#include <stdlib.h>
#include <string.h>

#include "mg_gslt_reader.h"

/* The reader's error in Megalodon's words. */
static void source_error(const MgSource *src, char *message, size_t cap) {
    const char *err = mg_source_error(src);
    int l1, c1, l2, c2, pos = 0;
    if (sscanf(err, "parse error %d:%d-%d:%d: %n", &l1, &c1, &l2, &c2, &pos) == 4 && pos > 0) {
        snprintf(message, cap, "Syntax error between line %d char %d and line %d char %d:\n%s", l1, c1,
                 l2, c2, err + pos);
        return;
    }
    int line, ch;
    mg_source_position(src, &line, &ch);
    snprintf(message, cap, "Failure at line %d char %d: %s", line, ch,
             strncmp(err, "failure: ", 9) == 0 ? err + 9 : err);
}

MgRunStatus mg_run_files(MgElab *e, const MgRunFile *files, int count, char *message, size_t cap) {
    if (cap)
        message[0] = '\0';
    MgSource *src = NULL;
    char **texts = calloc(count > 0 ? (size_t)count : 1u, sizeof(char *));
    MgRunStatus status = texts ? MG_RUN_OK : MG_RUN_UNREADABLE;
    for (int f = 0; f < count && status == MG_RUN_OK; ++f) {
        size_t len;
        texts[f] = mg_gslt_file_bytes(files[f].path, &len);
        if (!texts[f]) {
            snprintf(message, cap, "cannot read %s", files[f].path);
            status = MG_RUN_UNREADABLE;
            break;
        }
        if (!src)
            src = mg_source_new(texts[f], len);
        else
            mg_source_set_input(src, texts[f], len);
        mg_elab_begin_file(e, files[f].signature);
        for (;;) {
            const MgSourceItem *item;
            int r = mg_source_next(src, &item);
            if (r == 0)
                break;
            if (r < 0) {
                source_error(src, message, cap);
                status = MG_RUN_FAILURE;
                break;
            }
            int v = mg_elab_item(e, item);
            if (v < 0) {
                snprintf(message, cap, "%s", mg_elab_error(e));
                status = v == -1 ? MG_RUN_FAILURE : v == -2 ? MG_RUN_EXCEPTION : MG_RUN_REFUSED;
                break;
            }
        }
    }
    mg_source_free(src);
    for (int f = 0; texts && f < count; ++f)
        free(texts[f]);
    free(texts);
    return status;
}

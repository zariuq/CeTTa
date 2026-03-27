#include "text_source.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void text_source_set_error(char *errbuf, size_t errbuf_sz,
                                  const char *message) {
    if (!errbuf || errbuf_sz == 0) return;
    snprintf(errbuf, errbuf_sz, "%s", message ? message : "text-source-error");
}

static bool text_source_copy_text(char *dst, size_t dst_sz, const char *src) {
    size_t len;
    if (!dst || dst_sz == 0 || !src) return false;
    len = strlen(src);
    if (len >= dst_sz) return false;
    memmove(dst, src, len + 1);
    return true;
}

bool cetta_text_path_parent_dir(char *dst, size_t dst_sz, const char *path) {
    size_t len;
    char scratch[PATH_MAX];
    char *slash;

    if (!dst || dst_sz == 0 || !path || !*path) return false;
    if (!text_source_copy_text(scratch, sizeof(scratch), path)) return false;
    len = strlen(scratch);
    while (len > 1 && scratch[len - 1] == '/') {
        scratch[--len] = '\0';
    }
    slash = strrchr(scratch, '/');
    if (!slash) {
        return text_source_copy_text(dst, dst_sz, ".");
    }
    if (slash == scratch) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return text_source_copy_text(dst, dst_sz, scratch);
}

static bool text_source_path_join(char *out, size_t out_sz,
                                  const char *dir, const char *leaf) {
    int n = snprintf(out, out_sz, "%s/%s", dir, leaf);
    return n > 0 && (size_t)n < out_sz;
}

bool cetta_text_path_resolve(const char *base_dir, const char *path,
                             char *resolved, size_t resolved_sz) {
    char joined[PATH_MAX];
    char *real;

    if (!path || !*path) return false;
    if (path[0] == '/') {
        if (snprintf(joined, sizeof(joined), "%s", path) >= (int)sizeof(joined)) {
            return false;
        }
    } else if (base_dir && *base_dir) {
        if (!text_source_path_join(joined, sizeof(joined), base_dir, path)) {
            return false;
        }
    } else {
        if (snprintf(joined, sizeof(joined), "%s", path) >= (int)sizeof(joined)) {
            return false;
        }
    }

    real = realpath(joined, resolved);
    if (real) return true;
    return snprintf(resolved, resolved_sz, "%s", joined) < (int)resolved_sz;
}

bool cetta_text_source_open(CettaTextSource *src, const char *base_dir,
                            const char *path, char *errbuf, size_t errbuf_sz) {
    char resolved[PATH_MAX];
    FILE *fp;

    if (!src || !path) {
        text_source_set_error(errbuf, errbuf_sz, "bad-text-source-open");
        return false;
    }
    if (!cetta_text_path_resolve(base_dir ? base_dir : ".", path,
                                 resolved, sizeof(resolved))) {
        text_source_set_error(errbuf, errbuf_sz, "path-too-long");
        return false;
    }
    fp = fopen(resolved, "rb");
    if (!fp) {
        if (errbuf && errbuf_sz > 0) {
            snprintf(errbuf, errbuf_sz, "cannot-open-file:%s", strerror(errno));
        }
        return false;
    }

    memset(src, 0, sizeof(*src));
    if (!text_source_copy_text(src->path, sizeof(src->path), resolved) ||
        !cetta_text_path_parent_dir(src->dir, sizeof(src->dir), resolved)) {
        fclose(fp);
        text_source_set_error(errbuf, errbuf_sz, "path-too-long");
        return false;
    }
    src->fp = fp;
    src->line = 1;
    src->col = 1;
    return true;
}

void cetta_text_source_close(CettaTextSource *src) {
    if (!src) return;
    if (src->fp) fclose(src->fp);
    src->fp = NULL;
    src->path[0] = '\0';
    src->dir[0] = '\0';
    src->pos = 0;
    src->len = 0;
    src->eof = false;
    src->line = 1;
    src->col = 1;
}

static bool cetta_text_source_fill(CettaTextSource *src, size_t offset,
                                   char *errbuf, size_t errbuf_sz) {
    if (!src || !src->fp) {
        text_source_set_error(errbuf, errbuf_sz, "closed-text-source");
        return false;
    }
    while (src->pos + offset >= src->len && !src->eof) {
        if (src->pos > 0) {
            size_t remaining = src->len - src->pos;
            memmove(src->buf, src->buf + src->pos, remaining);
            src->len = remaining;
            src->pos = 0;
        }
        if (src->pos + offset < src->len) break;
        if (src->len == sizeof(src->buf)) {
            text_source_set_error(errbuf, errbuf_sz, "text-lookahead-too-large");
            return false;
        }
        size_t nread = fread(src->buf + src->len, 1, sizeof(src->buf) - src->len,
                             src->fp);
        if (nread == 0) {
            if (ferror(src->fp)) {
                if (errbuf && errbuf_sz > 0) {
                    snprintf(errbuf, errbuf_sz, "cannot-read-file:%s",
                             strerror(errno));
                }
                return false;
            }
            src->eof = true;
            break;
        }
        src->len += nread;
    }
    return true;
}

bool cetta_text_source_peek(CettaTextSource *src, size_t offset, int *ch_out,
                            char *errbuf, size_t errbuf_sz) {
    if (!ch_out) {
        text_source_set_error(errbuf, errbuf_sz, "missing-peek-output");
        return false;
    }
    if (!cetta_text_source_fill(src, offset, errbuf, errbuf_sz)) return false;
    if (!src || src->pos + offset >= src->len) {
        *ch_out = -1;
        return true;
    }
    *ch_out = (unsigned char)src->buf[src->pos + offset];
    return true;
}

void cetta_text_source_advance(CettaTextSource *src, size_t count) {
    if (!src) return;
    while (count > 0) {
        if (src->pos >= src->len) {
            if (src->eof || !cetta_text_source_fill(src, 0, NULL, 0) ||
                src->pos >= src->len) {
                return;
            }
        }
        unsigned char ch = src->buf[src->pos++];
        if (ch == '\n') {
            src->line++;
            src->col = 1;
        } else {
            src->col++;
        }
        count--;
    }
}

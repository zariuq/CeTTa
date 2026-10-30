#define _XOPEN_SOURCE 700
#include "cesta.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct CeBlock { struct CeBlock *next; max_align_t align; unsigned char data[]; };

void *ce_alloc(CeArena *arena, size_t bytes) {
    if (bytes > SIZE_MAX - sizeof(CeBlock)) {
        fputs("cesta: allocation size overflow\n", stderr);
        exit(2);
    }
    CeBlock *block = calloc(1, sizeof(*block) + bytes);
    if (!block) { fputs("cesta: out of memory\n", stderr); exit(2); }
    block->next = arena->blocks;
    arena->blocks = block;
    return block->data;
}

void ce_arena_free(CeArena *arena) {
    while (arena->blocks) {
        CeBlock *block = arena->blocks;
        arena->blocks = block->next;
        free(block);
    }
}

void *ce_resize(void *old, size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) {
        fputs("cesta: allocation size overflow\n", stderr); exit(2);
    }
    void *result = realloc(old, count * size);
    if (!result && count && size) { fputs("cesta: out of memory\n", stderr); exit(2); }
    return result;
}

CeText ce_text(CeArena *arena, const void *bytes, size_t len) {
    CeText text = {ce_alloc(arena, len + 1), len};
    if (len) memcpy(text.data, bytes, len);
    return text;
}

CeText ce_cstr(CeArena *arena, const char *str) { return ce_text(arena, str, strlen(str)); }
bool ce_equal(CeText a, CeText b) { return a.len == b.len && (!a.len || !memcmp(a.data, b.data, a.len)); }
bool ce_is(CeText text, const char *str) {
    return text.len == strlen(str) && (!text.len || !memcmp(text.data, str, text.len));
}

bool ce_error(CeError *error, size_t offset, const char *fmt, ...) {
    if (error) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error->message, sizeof(error->message), fmt, args);
        va_end(args);
        error->offset = offset;
    }
    return false;
}

bool ce_same_file(const char *a, const char *b) {
    if (!strcmp(a,b)) return true;
    struct stat x, y;
    if (!stat(a,&x) && !stat(b,&y) && x.st_dev == y.st_dev && x.st_ino == y.st_ino) return true;
    /* Outputs may not exist yet. Resolve their containing directories too. */
    const char *an = strrchr(a,'/'), *bn = strrchr(b,'/');
    if (strcmp(an ? an+1 : a,bn ? bn+1 : b)) return false;
    char *ad = an ? strndup(a,an == a ? 1 : (size_t)(an-a)) : strdup(".");
    char *bd = bn ? strndup(b,bn == b ? 1 : (size_t)(bn-b)) : strdup(".");
    char *ap = ad ? realpath(ad,NULL) : NULL, *bp = bd ? realpath(bd,NULL) : NULL;
    bool same = ap && bp && !strcmp(ap,bp);
    free(ad); free(bd); free(ap); free(bp); return same;
}

bool ce_read_file(CeArena *arena, const char *path, CeText *out, CeError *error) {
    FILE *file = !strcmp(path, "-") ? stdin : fopen(path, "rb");
    if (!file) return ce_error(error, 0, "cannot open %s", path);
    unsigned char *bytes = NULL;
    size_t len = 0, capacity = 0;
    for (;;) {
        if (capacity - len < 8192) {
            capacity = capacity ? capacity * 2 : 8192;
            bytes = ce_resize(bytes, capacity, 1);
        }
        size_t n = fread(bytes + len, 1, capacity - len, file);
        len += n;
        if (!n) break;
    }
    bool ok = !ferror(file);
    if (file != stdin) fclose(file);
    if (ok) *out = ce_text(arena, bytes, len);
    free(bytes);
    return ok || ce_error(error, len, "cannot read %s", path);
}

/* One byte of a printed string atom; returns the number of bytes written. */
static size_t quote_byte(unsigned c, char out[4]) {
    if (c == '"' || c == '\\') { out[0] = '\\'; out[1] = (char)c; return 2; }
    if (c == '\n') { out[0] = '\\'; out[1] = 'n'; return 2; }
    if (c == '\r') { out[0] = '\\'; out[1] = 'r'; return 2; }
    if (c == '\t') { out[0] = '\\'; out[1] = 't'; return 2; }
    if (c < 32 || c >= 127) {
        static const char digits[] = "0123456789abcdef";
        out[0] = '\\'; out[1] = 'x'; out[2] = digits[c >> 4]; out[3] = digits[c & 15]; return 4;
    }
    out[0] = (char)c; return 1;
}
void ce_quote(FILE *file, CeText text) {
    fputc('"', file);
    for (size_t i = 0; i < text.len; ++i) {
        char bytes[4]; fwrite(bytes, 1, quote_byte(text.data[i], bytes), file);
    }
    fputc('"', file);
}
CeText ce_quoted(CeArena *arena, CeText text) {
    unsigned char *bytes = ce_alloc(arena, 4*text.len+3);
    size_t len = 0; bytes[len++] = '"';
    for (size_t i = 0; i < text.len; ++i) len += quote_byte(text.data[i], (char *)bytes+len);
    bytes[len++] = '"';
    return (CeText){bytes, len};
}

static bool prefix(CeText source, size_t pos, const char *str) {
    size_t n = strlen(str);
    return n <= source.len - pos && !memcmp(source.data + pos, str, n);
}
static bool white(unsigned c) { return c == ' ' || (c >= 9 && c <= 13); }
static bool alpha(unsigned c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int hex(unsigned c) {
    if (c >= '0' && c <= '9') return (int)c - '0';
    if (c >= 'a' && c <= 'f') return (int)c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return (int)c - 'A' + 10;
    return -1;
}

bool ce_quoted_value(CeArena *arena, CeText source, size_t *position,
                     CeText *out, CeError *error) {
    size_t begin = *position;
    if (begin >= source.len || (source.data[begin] != '"' && source.data[begin] != '\''))
        return ce_error(error,begin,"expected quoted bytes");
    unsigned quote = source.data[(*position)++];
    size_t end = *position;
    while (end < source.len && source.data[end] != quote) {
        if (source.data[end] == '\\' && end+1 < source.len) ++end;
        ++end;
    }
    unsigned char *value = ce_alloc(arena,end-begin+1);
    size_t len = 0;
    while (*position < source.len && source.data[*position] != quote) {
        unsigned v = source.data[(*position)++];
        if (v == '\\') {
            if (*position == source.len) return ce_error(error,begin,"unterminated escape");
            v = source.data[(*position)++];
            if (v == 'n') v = '\n';
            else if (v == 'r') v = '\r';
            else if (v == 't') v = '\t';
            else if (v == 'b') v = '\b';
            else if (v == 'f') v = '\f';
            else if (v == 'x') {
                if (source.len-*position < 2 || hex(source.data[*position]) < 0 || hex(source.data[*position+1]) < 0)
                    return ce_error(error,*position,"expected two hexadecimal digits");
                v = (unsigned)(hex(source.data[*position])*16+hex(source.data[*position+1]));
                *position += 2;
            } else if (v != '"' && v != '\'' && v != '\\')
                return ce_error(error,*position-1,"unknown string escape");
        }
        value[len++] = (unsigned char)v;
    }
    if (*position == source.len) return ce_error(error,begin,"unterminated quoted bytes");
    ++*position;
    *out = (CeText){value,len};
    return true;
}

bool ce_meta_next(CeMetaScanner *s) {
    for (;;) {
        while (s->pos < s->source.len && white(s->source.data[s->pos])) ++s->pos;
        if (prefix(s->source, s->pos, "--")) {
            while (s->pos < s->source.len && s->source.data[s->pos] != '\n') ++s->pos;
        } else if (prefix(s->source, s->pos, "{-")) {
            size_t begin = s->pos;
            s->pos += 2;
            while (s->pos < s->source.len && !prefix(s->source, s->pos, "-}")) ++s->pos;
            if (s->pos == s->source.len) return ce_error(s->error, begin, "unterminated grammar comment");
            s->pos += 2;
        } else break;
    }
    size_t begin = s->pos;
    s->token.offset = begin;
    if (begin == s->source.len) {
        s->token.kind = CE_META_END;
        s->token.text = ce_cstr(s->arena, "");
        return true;
    }
    unsigned c = s->source.data[s->pos++];
    if (c == '"' || c == '\'') {
        s->pos = begin;
        if (!ce_quoted_value(s->arena,s->source,&s->pos,&s->token.text,s->error)) return false;
        s->token.kind = c == '"' ? CE_META_STRING : CE_META_CHAR;
        return true;
    }
    if (alpha(c) || (c >= '0' && c <= '9')) {
        while (s->pos < s->source.len) {
            unsigned d = s->source.data[s->pos];
            if (!alpha(d) && !(d >= '0' && d <= '9') && d != '\'') break;
            ++s->pos;
        }
        s->token.kind = CE_META_NAME;
    } else {
        const char *multi[] = {"::=", "->", "=>", "~>", "==", "\\/", "/\\", "..."};
        for (size_t i = 0; i < sizeof(multi)/sizeof(*multi); ++i) {
            if (prefix(s->source, begin, multi[i])) { s->pos = begin + strlen(multi[i]); break; }
        }
        s->token.kind = CE_META_PUNCT;
    }
    s->token.text = ce_text(s->arena, s->source.data + begin, s->pos - begin);
    return true;
}

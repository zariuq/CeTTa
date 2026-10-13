/* Streaming filesystem operations. MeTTa owns the user-facing presentation. */
#include "native_sha256.h"
#include <dlfcn.h>
#include <pwd.h>
#if defined(__has_include)
#if __has_include(<pcre2.h>)
#define CETTA_FS_PCRE2 1
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif
#endif

static void fs_sb_text(CettaStringBuf *b, const char *s) {
    cetta_sb_append_n(b, s, strlen(s));
}
static void fs_sb_number(CettaStringBuf *b, size_t n) {
    char text[32]; snprintf(text, sizeof(text), "%zu", n); fs_sb_text(b, text);
}
static void fs_detail(CettaStringBuf *b, const char *path, int error) {
    char prefix[80]; snprintf(prefix, sizeof(prefix), "[Errno %d] %s: '", error, strerror(error));
    fs_sb_text(b, prefix); fs_sb_text(b, path); fs_sb_text(b, "'");
}
static Atom *fs_tools_error(Arena *a, const char *path, int error) {
    CettaStringBuf b; cetta_sb_init(&b); fs_detail(&b, path, error);
    Atom *r = atom_expr2(a, atom_string(a, "error"), atom_string_n(a, b.buf, b.len));
    cetta_sb_free(&b); return r;
}
static Atom *fs_tools_message(Arena *a, const char *message) {
    return atom_expr2(a, atom_string(a, "error"), atom_string(a, message));
}
static Atom *fs_sb_atom(Arena *a, CettaStringBuf *b) {
    return atom_string_n(a, b->buf ? b->buf : "", b->len);
}

/* Buffered universal-newline reader: CRLF and CR count as one newline.
 * It reads only the requested window, never cuts a line, and handles NUL. */
typedef struct { FILE *file; uint8_t data[65536]; size_t pos, len; int pushed; } FsLineReader;
static int fs_next_byte(FsLineReader *r) {
    if (r->pushed >= 0) { int c = r->pushed; r->pushed = -1; return c; }
    if (r->pos == r->len) {
        r->len = fread(r->data, 1, sizeof(r->data), r->file); r->pos = 0;
        if (!r->len) return EOF;
    }
    return r->data[r->pos++];
}
static bool fs_next_line(FsLineReader *r, CettaStringBuf *line) {
    line->len = 0;
    for (;;) {
        if (r->pushed >= 0) {
            char c=(char)r->pushed; r->pushed=-1;
            cetta_sb_append_n(line,&c,1);
        }
        if (r->pos == r->len) {
            r->len=fread(r->data,1,sizeof(r->data),r->file); r->pos=0;
            if (!r->len) break;
        }
        uint8_t *begin=r->data+r->pos; size_t available=r->len-r->pos;
        uint8_t *lf=memchr(begin,'\n',available), *cr=memchr(begin,'\r',available);
        uint8_t *stop=lf && cr ? (lf<cr ? lf : cr) : lf ? lf : cr;
        size_t span=stop ? (size_t)(stop-begin) : available;
        cetta_sb_append_n(line,(char *)begin,span); r->pos+=span;
        if (!stop) continue;
        r->pos++;
        if (*stop=='\r') { int c=fs_next_byte(r); if (c!='\n' && c!=EOF) r->pushed=c; }
        cetta_sb_append_n(line,"\n",1); break;
    }
    return line->len != 0;
}

static Atom *fs_line_window(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path; size_t start, count;
    if (nargs != 3 || !(path = library_text_arg(args[0])) ||
        !fs_byte_limit(args[1], &start) || !fs_byte_limit(args[2], &count))
        return library_signature_error(a, head, args, nargs, "expected filename, first line and line count");
    if (!start) start = 1;
    if (!count) count = 1;
    FILE *f = fopen(path, "rb"); if (!f) return fs_tools_error(a, path, errno);
    struct stat st;
    int stat_error = fstat(fileno(f), &st) != 0 ? errno : S_ISDIR(st.st_mode) ? EISDIR : 0;
    if (stat_error) {
        int e = stat_error; fclose(f); return fs_tools_error(a, path, e);
    }
    FsLineReader reader = {.file = f, .pushed = -1};
    CettaStringBuf line, body; cetta_sb_init(&line); cetta_sb_init(&body);
    size_t n = 0, number = 1;
    while (number < start && fs_next_line(&reader, &line)) number++;
    while (n < count && fs_next_line(&reader, &line)) {
        fs_replace_utf8(&line);
        if (line.len && line.buf[line.len-1] == '\n') line.len--;
        if (n) fs_sb_text(&body, "\n");
        char prefix[40]; snprintf(prefix, sizeof(prefix), "%5zu\t", start + n);
        fs_sb_text(&body, prefix); cetta_sb_append_n(&body, line.buf, line.len); n++;
    }
    bool more = fs_next_line(&reader, &line);
    int error = ferror(f) ? (errno ? errno : EIO) : 0; fclose(f);
    Atom *items[] = {atom_string(a, "window"), atom_int(a, start), atom_int(a, n),
                    atom_bool(a, more), fs_sb_atom(a, &body), atom_bool(a, st.st_size == 0)};
    Atom *result = error ? fs_tools_error(a, path, error) : atom_expr(a, items, 6);
    cetta_sb_free(&line); cetta_sb_free(&body); return result;
}

typedef struct { char *name; bool directory, file; } FsEntry;
static char *fs_join_path(const char *root, const char *name) {
    size_t n = strlen(root), m = strlen(name); bool slash = n && root[n-1] != '/';
    char *path = cetta_malloc(n + m + slash + 1);
    memcpy(path, root, n); if (slash) path[n++] = '/'; memcpy(path+n, name, m+1); return path;
}
static bool fs_skipped(Atom *skips, const char *name) {
    if (skips->kind != ATOM_EXPR) return false;
    for (uint32_t i = 0; i < skips->expr.len; i++) {
        const char *s = library_text_arg(skips->expr.elems[i]); if (s && !strcmp(s, name)) return true;
    }
    return false;
}
static int fs_entry_compare(const void *av, const void *bv) {
    const FsEntry *a = av, *b = bv;
    if (a->file != b->file) return a->file ? 1 : -1;
    return strcmp(a->name, b->name);
}
static FsEntry *fs_entries(const char *path, size_t *count, int *error, bool tree) {
    *count = 0; *error = 0; DIR *d = opendir(path);
    if (!d) { *error = errno; return NULL; }
    FsEntry *entries = NULL; size_t cap = 0; struct dirent *entry;
    errno = 0;
    while ((entry = readdir(d))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        bool directory = entry->d_type == DT_DIR;
        bool file = entry->d_type == DT_REG;
        if (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
            char *p = fs_join_path(path, entry->d_name); struct stat st;
            if (lstat(p, &st) == 0) {
                directory = S_ISDIR(st.st_mode); file = S_ISREG(st.st_mode);
                if (S_ISLNK(st.st_mode) && stat(p, &st) == 0) {
                    file = S_ISREG(st.st_mode);
                    if (!tree) directory = S_ISDIR(st.st_mode);
                }
            }
            free(p);
        }
        if (*count == cap) { cap = cap ? cap*2 : 32; entries = cetta_realloc(entries, cap*sizeof(*entries)); }
        entries[(*count)++] = (FsEntry){strdup(entry->d_name), directory, tree ? file : !directory};
        errno = 0;
    }
    *error = errno; closedir(d); if (*count) qsort(entries, *count, sizeof(*entries), fs_entry_compare);
    return entries;
}
static void fs_entries_free(FsEntry *e, size_t n) { for (size_t i=0; i<n; i++) free(e[i].name); free(e); }
typedef struct { size_t seen, emitted, limit, offset; bool more; Atom *skips; CettaStringBuf body; } FsTreePage;
static bool fs_tree_emit(FsTreePage *p, const char *text, size_t level, bool directory) {
    if (p->seen++ < p->offset) return true;
    if (p->emitted == p->limit) { p->more = true; return false; }
    if (p->emitted++) fs_sb_text(&p->body, "\n");
    for (size_t i=0; i<level; i++) fs_sb_text(&p->body, "  ");
    fs_sb_text(&p->body, text); if (directory) fs_sb_text(&p->body, "/"); return true;
}
static void fs_tree_walk(FsTreePage *p, const char *path, size_t level, size_t depth) {
    size_t count; int error; FsEntry *entries = fs_entries(path, &count, &error, true);
    if (error) {
        CettaStringBuf text; cetta_sb_init(&text); fs_sb_text(&text, "["); fs_detail(&text, path, error); fs_sb_text(&text, "]");
        fs_tree_emit(p, text.buf, level, false); cetta_sb_free(&text);
    }
    for (size_t i=0; i<count && !p->more; i++) {
        FsEntry *e = &entries[i]; if (fs_skipped(p->skips, e->name)) continue;
        if (!fs_tree_emit(p, e->name, level, e->directory)) break;
        if (e->directory && level+1 < depth) {
            char *next = fs_join_path(path, e->name); fs_tree_walk(p, next, level+1, depth); free(next);
        }
    }
    fs_entries_free(entries, count);
}
static Atom *fs_tree_page(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path; size_t depth, limit, offset;
    if (nargs != 5 || !(path = library_text_arg(args[0])) || !fs_byte_limit(args[1], &depth) ||
        !fs_byte_limit(args[2], &limit) || !fs_byte_limit(args[3], &offset) || args[4]->kind != ATOM_EXPR)
        return library_signature_error(a, head, args, nargs, "expected directory, depth, page length, offset and skipped names");
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) return atom_expr2(a, atom_string(a, "not-directory"), atom_string(a, path));
    FsTreePage page = {.limit = limit ? limit : 1, .offset = offset, .skips = args[4]}; cetta_sb_init(&page.body);
    fs_tree_walk(&page, path, 0, depth ? depth : 1);
    Atom *items[] = {atom_string(a, "page"), fs_sb_atom(a, &page.body), atom_bool(a, page.more), atom_int(a, offset + page.limit)};
    Atom *result = atom_expr(a, items, 4); cetta_sb_free(&page.body); return result;
}

#ifdef CETTA_FS_PCRE2
typedef struct {
    void *library;
    pcre2_code *(*compile)(PCRE2_SPTR, PCRE2_SIZE, uint32_t, int *, PCRE2_SIZE *, pcre2_compile_context *);
    void (*code_free)(pcre2_code *);
    pcre2_match_data *(*data_create)(const pcre2_code *, pcre2_general_context *);
    void (*data_free)(pcre2_match_data *);
    int (*match)(const pcre2_code *, PCRE2_SPTR, PCRE2_SIZE, PCRE2_SIZE, uint32_t, pcre2_match_data *, pcre2_match_context *);
} FsRegexApi;
static FsRegexApi fs_regex_api(void) {
    FsRegexApi api = {0}; api.library = dlopen("libpcre2-8.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!api.library) return api;
    *(void **)(&api.compile) = dlsym(api.library, "pcre2_compile_8");
    *(void **)(&api.code_free) = dlsym(api.library, "pcre2_code_free_8");
    *(void **)(&api.data_create) = dlsym(api.library, "pcre2_match_data_create_from_pattern_8");
    *(void **)(&api.data_free) = dlsym(api.library, "pcre2_match_data_free_8");
    *(void **)(&api.match) = dlsym(api.library, "pcre2_match_8");
    if (!api.compile || !api.code_free || !api.data_create || !api.data_free || !api.match) { dlclose(api.library); memset(&api, 0, sizeof(api)); }
    return api;
}
typedef struct {
    FsRegexApi api; pcre2_code *regex; pcre2_match_data *match;
    const char *literal; size_t literal_len, seen, emitted, limit, offset, scanned;
    bool more; int match_error; Atom *skips; CettaStringBuf body, errors;
} FsGrepPage;
static bool fs_unicode_space(uint32_t c) {
    return (c >= 9 && c <= 13) || (c >= 28 && c <= 32) || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
static size_t fs_rstrip_size(CettaStringBuf *line) {
    size_t pos=0, kept=0;
    while (pos < line->len) { uint32_t c, w; if (!cetta_utf8_decode_one((uint8_t *)line->buf, line->len, pos, &c, &w)) break;
        pos += w; if (!fs_unicode_space(c)) kept=pos; }
    return kept;
}
static void fs_grep_error(FsGrepPage *p, const char *path, int error) {
    if (p->errors.len) fs_sb_text(&p->errors, "\n");
    fs_sb_text(&p->errors, path); fs_sb_text(&p->errors, ": "); fs_detail(&p->errors, path, error);
}
static void fs_grep_file(FsGrepPage *p, const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { fs_grep_error(p, path, errno); return; }
    char sample[1024]; size_t length = fread(sample, 1, sizeof(sample), f);
    if (memchr(sample, 0, length)) { fclose(f); return; }
    rewind(f); p->scanned++;
    FsLineReader reader = {.file = f, .pushed = -1}; CettaStringBuf line; cetta_sb_init(&line); size_t number=0;
    while (!p->more && !p->match_error && fs_next_line(&reader, &line)) {
        number++; fs_replace_utf8(&line);
        int match = p->regex ? p->api.match(p->regex, (PCRE2_SPTR)line.buf, line.len, 0, 0, p->match, NULL) :
                              (memmem(line.buf, line.len, p->literal, p->literal_len) ? 1 : PCRE2_ERROR_NOMATCH);
        if (match < 0 && match != PCRE2_ERROR_NOMATCH) { p->match_error = match; break; }
        if (match < 0 || p->seen++ < p->offset) continue;
        if (p->emitted == p->limit) { p->more = true; break; }
        if (p->emitted++) fs_sb_text(&p->body, "\n");
        fs_sb_text(&p->body, path); fs_sb_text(&p->body, ":"); fs_sb_number(&p->body, number); fs_sb_text(&p->body, ": ");
        cetta_sb_append_n(&p->body, line.buf, fs_rstrip_size(&line));
    }
    if (ferror(f)) fs_grep_error(p, path, errno ? errno : EIO);
    fclose(f); cetta_sb_free(&line);
}
static void fs_grep_walk(FsGrepPage *p, const char *path) {
    size_t count; int error; FsEntry *entries = fs_entries(path, &count, &error, false);
    if (error) fs_grep_error(p, path, error);
    /* os.walk scans this directory's files before visiting sorted children. */
    for (size_t i=0; i<count && !p->more && !p->match_error; i++) if (!entries[i].directory) {
        char *next=fs_join_path(path, entries[i].name); fs_grep_file(p, next); free(next);
    }
    for (size_t i=0; i<count && !p->more && !p->match_error; i++) if (entries[i].directory && !fs_skipped(p->skips, entries[i].name)) {
        char *next=fs_join_path(path, entries[i].name); struct stat st;
        if (lstat(next, &st) == 0 && !S_ISLNK(st.st_mode)) fs_grep_walk(p, next);
        free(next);
    }
    fs_entries_free(entries, count);
}
#endif
static Atom *fs_grep_page(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
#ifndef CETTA_FS_PCRE2
    (void)head; (void)args; (void)nargs;
    return fs_tools_message(a, "native regex needs PCRE2 headers at build time and libpcre2-8 at runtime");
#else
    const char *pattern, *path; size_t limit, offset;
    if (nargs != 5 || !(pattern = library_text_arg(args[0])) || !(path = library_text_arg(args[1])) ||
        !fs_byte_limit(args[2], &limit) || !fs_byte_limit(args[3], &offset) || args[4]->kind != ATOM_EXPR)
        return library_signature_error(a, head, args, nargs, "expected pattern, path, page length, offset and skipped directories");
    FsGrepPage p = {.api=fs_regex_api(), .limit=limit ? limit : 1, .offset=offset, .skips=args[4], .literal=pattern, .literal_len=strlen(pattern)};
    if (!p.api.library) return fs_tools_message(a, "native regex needs libpcre2-8 at runtime");
    int error; PCRE2_SIZE where;
    /* Python's end-of-string anchor is strict. Keep escaped backslashes intact. */
    CettaStringBuf converted; cetta_sb_init(&converted);
    for (size_t i=0; pattern[i]; i++) {
        if (pattern[i]=='\\' && pattern[i+1]) { fs_sb_text(&converted, "\\"); i++; char c=pattern[i]=='Z' ? 'z' : pattern[i]; cetta_sb_append_n(&converted, &c, 1); }
        else cetta_sb_append_n(&converted, pattern+i, 1);
    }
    p.regex=p.api.compile((PCRE2_SPTR)(converted.buf ? converted.buf : ""), converted.len, PCRE2_UTF | PCRE2_UCP, &error, &where, NULL);
    cetta_sb_free(&converted);
    if (p.regex) p.match=p.api.data_create(p.regex, NULL);
    if (p.regex && !p.match) { p.api.code_free(p.regex); dlclose(p.api.library); return fs_tools_message(a, "regex match allocation failed"); }
    cetta_sb_init(&p.body); cetta_sb_init(&p.errors);
    struct stat st; if (stat(path, &st)==0 && S_ISREG(st.st_mode)) fs_grep_file(&p, path); else fs_grep_walk(&p, path);
    Atom *items[] = {atom_string(a, "matches"), fs_sb_atom(a, &p.body), atom_bool(a, p.more), atom_int(a, offset+p.limit), atom_int(a, p.scanned), fs_sb_atom(a, &p.errors)};
    Atom *r;
    if (p.match_error) { char text[80]; snprintf(text, sizeof(text), "regex matching failed (PCRE2 %d)", p.match_error); r=fs_tools_message(a, text); }
    else r=atom_expr(a, items, 6);
    if (p.match) p.api.data_free(p.match);
    if (p.regex) p.api.code_free(p.regex);
    dlclose(p.api.library);
    cetta_sb_free(&p.body); cetta_sb_free(&p.errors); return r;
#endif
}

static char *fs_absolute_user_path(const char *path) {
    char *expanded=NULL;
    if (path[0]=='~') {
        const char *slash=strchr(path, '/'); size_t n=slash ? (size_t)(slash-path-1) : strlen(path)-1;
        const char *home=NULL;
        if (!n) { home=getenv("HOME"); if (!home) { struct passwd *pw=getpwuid(getuid()); if (pw) home=pw->pw_dir; } }
        else { char *user=strndup(path+1, n); struct passwd *pw=getpwnam(user); if (pw) home=pw->pw_dir; free(user); }
        if (!home) return NULL;
        expanded=slash ? fs_join_path(home, slash+1) : strdup(home); path=expanded;
    }
    char *cwd=NULL, *absolute;
    if (path[0]=='/') absolute=strdup(path);
    else { cwd=getcwd(NULL,0); if (!cwd) { free(expanded); return NULL; } absolute=fs_join_path(cwd,path); }
    free(cwd); free(expanded); return absolute;
}
static bool fs_mkdir_parents(char *path) {
    for (char *p=path+1; ; p++) if (*p=='/' || *p==0) {
        char c=*p; *p=0; int rc=mkdir(path,0777), e=errno; *p=c;
        if (rc && e!=EEXIST) { errno=e; return false; } if (!c) return true;
    }
}
static bool fs_write_bytes_fd(int fd, const char *data, size_t len) {
    while (len) { ssize_t n=write(fd,data,len); if (n<0 && errno==EINTR) continue; if (n<=0) { if (!n) errno=EIO; return false; } data+=n; len-=(size_t)n; }
    return true;
}
static bool fs_load_bytes(const char *path, CettaStringBuf *out) {
    FILE *f=fopen(path,"rb"); if (!f) return false;
    struct stat st;
    if (fstat(fileno(f),&st)!=0) { int e=errno; fclose(f); errno=e; return false; }
    if (S_ISDIR(st.st_mode)) { fclose(f); errno=EISDIR; return false; }
    if (S_ISREG(st.st_mode) && st.st_size>0 && (uint64_t)st.st_size<SIZE_MAX) cetta_sb_ensure(out,(size_t)st.st_size);
    char buf[65536]; size_t n; while ((n=fread(buf,1,sizeof(buf),f))) cetta_sb_append_n(out,buf,n);
    int e=ferror(f) ? (errno ? errno : EIO) : 0; fclose(f); errno=e; return !e;
}
static bool fs_valid_utf8(CettaStringBuf *text) {
    for (size_t i=0; i<text->len;) { uint32_t c,w; if (!cetta_utf8_decode_one((uint8_t *)text->buf,text->len,i,&c,&w)) return false; i+=w; } return true;
}
static void fs_normalize_newlines(CettaStringBuf *text) {
    size_t to=0; for (size_t i=0;i<text->len;i++) {
        if (text->buf[i]=='\r') { text->buf[to++]='\n'; if (i+1<text->len && text->buf[i+1]=='\n') i++; }
        else text->buf[to++]=text->buf[i];
    } text->len=to;
}
/* Atomic replacement with a durable, collision-free backup. The caller chooses
 * the backup directory; this library does not impose an agent state policy. */
static Atom *fs_update_text(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path, *op, *backup_root; const char *first, *second; size_t first_len, second_len;
    if (nargs!=5 || !(path=library_text_arg(args[0])) || !(op=library_text_arg(args[1])) ||
        !(backup_root=library_text_arg(args[4]))) return library_signature_error(a,head,args,nargs,"expected path, operation, text/old, new and backup directory");
    first=args[2]->kind == ATOM_GROUNDED && args[2]->ground.gkind == GV_STRING ? args[2]->ground.sval : NULL;
    second=args[3]->kind == ATOM_GROUNDED && args[3]->ground.gkind == GV_STRING ? args[3]->ground.sval : NULL;
    if (!first) first=library_text_arg(args[2]);
    if (!second) second=library_text_arg(args[3]);
    if (!first || !second || (strcmp(op,"write") && strcmp(op,"append") && strcmp(op,"edit")))
        return library_signature_error(a,head,args,nargs,"expected write, append or edit with text arguments");
    first_len=args[2]->kind == ATOM_GROUNDED && args[2]->ground.gkind == GV_STRING ? atom_string_len(args[2]) : strlen(first);
    second_len=args[3]->kind == ATOM_GROUNDED && args[3]->ground.gkind == GV_STRING ? atom_string_len(args[3]) : strlen(second);
    char *target=fs_absolute_user_path(path); if (!target) return fs_tools_message(a,"cannot resolve file path");
    CettaStringBuf previous, data; cetta_sb_init(&previous); cetta_sb_init(&data);
    char *backup=NULL, *temporary=NULL, *parent=NULL; Atom *result=NULL; struct stat st;
    if (!strcmp(op,"edit") && !first_len) { result=fs_tools_message(a,"old-string is empty"); goto done; }
    bool exists=stat(target,&st)==0 && S_ISREG(st.st_mode);
    if (exists || strcmp(op,"write")) if (!fs_load_bytes(target,&previous)) { result=fs_tools_error(a,target,errno); goto done; }
    if (!strcmp(op,"edit")) {
        CettaStringBuf text; cetta_sb_init(&text); cetta_sb_append_n(&text,previous.buf ? previous.buf : "",previous.len);
        if (!fs_valid_utf8(&text)) { cetta_sb_free(&text); result=fs_tools_message(a,"file contains invalid UTF-8"); goto done; }
        fs_normalize_newlines(&text); size_t count=0, position=0, cursor=0;
        while (cursor<=text.len) { const char *hit=memmem(text.buf ? text.buf+cursor : "",text.len-cursor,first,first_len); if (!hit) break;
            position=(size_t)(hit-text.buf); count++; cursor=position+first_len; }
        if (count!=1) { result=atom_expr3(a,atom_string(a,"count"),atom_int(a,count),atom_string(a,target)); cetta_sb_free(&text); goto done; }
        cetta_sb_append_n(&data,text.buf,position); cetta_sb_append_n(&data,second,second_len);
        cetta_sb_append_n(&data,text.buf+position+first_len,text.len-position-first_len); cetta_sb_free(&text);
    } else {
        if (!strcmp(op,"append")) cetta_sb_append_n(&data,previous.buf ? previous.buf : "",previous.len);
        cetta_sb_append_n(&data,first,first_len); if (!strcmp(op,"append")) fs_sb_text(&data,"\n");
    }
    if (exists) {
        char *directory=fs_absolute_user_path(backup_root); if (!directory) { result=fs_tools_message(a,"cannot resolve backup directory"); goto done; }
        if (!fs_mkdir_parents(directory)) { result=fs_tools_error(a,directory,errno); free(directory); goto done; }
        char hash[65], stamp[32]; cetta_native_sha256_hex((uint8_t *)(previous.buf ? previous.buf : ""),previous.len,hash);
        time_t now=time(NULL); struct tm tm; localtime_r(&now,&tm); strftime(stamp,sizeof(stamp),"%Y%m%dT%H%M%S",&tm);
        CettaStringBuf name; cetta_sb_init(&name); fs_sb_text(&name,stamp); fs_sb_text(&name,"-"); cetta_sb_append_n(&name,hash,12); fs_sb_text(&name,"-");
        const char *base=strrchr(target,'/'); fs_sb_text(&name,base ? base+1 : target); fs_sb_text(&name,"-XXXXXX");
        backup=fs_join_path(directory,name.buf); cetta_sb_free(&name); free(directory);
        int fd=mkstemp(backup); if (fd<0) { result=fs_tools_error(a,backup,errno); goto done; }
        bool ok=fs_write_bytes_fd(fd,previous.buf,previous.len) && fsync(fd)==0; int e=errno; if (close(fd)!=0 && ok) { ok=false; e=errno; }
        if (!ok) { unlink(backup); result=fs_tools_error(a,backup,e); goto done; }
    }
    parent=strdup(target); char *slash=strrchr(parent,'/'); if (slash==parent) slash[1]=0; else *slash=0;
    const char *base=strrchr(target,'/'); CettaStringBuf name; cetta_sb_init(&name); fs_sb_text(&name,"."); fs_sb_text(&name,base+1); fs_sb_text(&name,".XXXXXX");
    temporary=fs_join_path(parent,name.buf); cetta_sb_free(&name);
    int fd=mkstemp(temporary); if (fd<0) { result=fs_tools_error(a,target,errno); goto done; }
    bool ok=fs_write_bytes_fd(fd,data.buf,data.len) && fsync(fd)==0;
    if (ok && exists) ok=fchmod(fd,st.st_mode & 07777)==0;
    int error=errno; if (close(fd)!=0 && ok) { ok=false; error=errno; }
    if (!ok || rename(temporary,target)!=0) { if (ok) error=errno; unlink(temporary); result=fs_tools_error(a,target,error); goto done; }
    fd=open(parent,O_RDONLY | O_DIRECTORY); if (fd>=0) { (void)fsync(fd); close(fd); }
    char hash[65]; cetta_native_sha256_hex((uint8_t *)(data.buf ? data.buf : ""),data.len,hash); hash[16]=0;
    Atom *items[]={atom_string(a,"written"),atom_string(a,target),atom_int(a,data.len),atom_string(a,hash),atom_string(a,backup ? backup : "")}; result=atom_expr(a,items,5);
done:
    free(target); free(backup); free(temporary); free(parent); cetta_sb_free(&data); cetta_sb_free(&previous); return result;
}
static Atom *fs_is_file(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *path; if (nargs!=1 || !(path=library_text_arg(args[0]))) return library_signature_error(a,head,args,nargs,"expected filename");
    struct stat st; return atom_bool(a, stat(path,&st)==0 && S_ISREG(st.st_mode));
}

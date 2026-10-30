#define _XOPEN_SOURCE 700
#include "pp.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Translation phase 4 as the configured compiler performs it: directives,
   conditional inclusion, macro replacement and _Pragma. The compiler is the
   authority on its own facts (predefined macros, include directories and
   __has_* answers); Cesta asks it for them and keeps every other step. Every
   token keeps where it was spelled and the expansion that produced it. */

/* ---- Tokens ---------------------------------------------------------------- */

enum { PP_PLACEMARKER = 100, PP_PASTE, PP_PRAGMA };
enum { T_SPACE = 1, T_NOEXPAND = 2, T_PASTED = 4 };

typedef struct Macro Macro;
typedef struct Hideset { const Macro *macro; const struct Hideset *next; } Hideset;
typedef struct File File;

typedef struct {
    unsigned kind, flags;
    CeText text;
    const Hideset *hide;
    const File *file;            /* where it was spelled */
    unsigned line, column;
    unsigned expansion_line;     /* the line __LINE__ reports for it */
    const CePPExpansion *expansion;  /* the replacement that produced it */
    const CePPExpansion *argument;   /* the expansion it was substituted into as an argument */
} Token;

typedef struct { Token *data; size_t len, capacity; } TokVec;

static void tpush(TokVec *v, Token t) {
    if (v->len == v->capacity) {
        v->capacity = v->capacity ? v->capacity*2 : 16;
        v->data = ce_resize(v->data,v->capacity,sizeof(*v->data));
    }
    v->data[v->len++] = t;
}

static bool is(const Token *t, unsigned kind, const char *text) {
    return t->kind == kind && ce_is(t->text,text);
}
static bool punct(const Token *t, const char *text) {
    return t->kind == CE_PP_PUNCTUATOR && (ce_is(t->text,text) ||
        (!strcmp(text,"#") && ce_is(t->text,"%:")) || (!strcmp(text,"##") && ce_is(t->text,"%:%:")));
}

/* ---- Macros ---------------------------------------------------------------- */

enum { B_NONE, B_FILE, B_LINE, B_COUNTER, B_INCLUDE_LEVEL, B_BASE_FILE, B_FILE_NAME,
       B_DATE, B_TIME, B_TIMESTAMP, B_PRAGMA };

struct Macro {
    CeText name;
    unsigned id;
    bool function, variadic;
    unsigned builtin;
    CeText *params;
    size_t nparams;               /* the variadic parameter, if any, is last */
    Token *body;
    size_t nbody;
    const File *file;
    unsigned line;
};

typedef struct Pushed { Macro *macro; struct Pushed *next; } Pushed;
typedef struct { CeText name; Macro *macro; Pushed *pushed; bool used; } Slot;

/* ---- Files ----------------------------------------------------------------- */

struct File {
    char *path;          /* as __FILE__ spells it */
    char *dir;           /* the directory quote includes search first, with its separator */
    char *real;          /* identity for #pragma once */
    int found_in;        /* search directory index, or -1 */
    CeSourceFile src;
    CeArena arena;
    bool once, system;
    File *next;
};

enum { C_TAKING, C_WAITING, C_DONE };   /* group taken; none yet; one already taken */
typedef struct { unsigned char state; bool otherwise; } Cond;

typedef struct {
    File *file;
    size_t pos;
    char *presumed;      /* __FILE__ after #line */
    long delta;          /* presumed line minus physical line */
    Cond *conds;
    size_t nconds, capacity;
    size_t base;         /* conds at entry, for the end-of-file check */
} Frame;

typedef struct { char *text; long value; } Answer;
/* A GCC assertion: pred(answer) asserted or withdrawn; an empty answer
   withdraws every answer of the predicate. */
typedef struct { CeText predicate, answer; bool asserted; } Assertion;

typedef struct {
    CeArena arena;
    const CeGrammar *grammar;
    CeGrammar identifier_grammar;
    bool identifier_grammar_ready;
    bool clang, c11, markers, char_unsigned;
    CeText predefined;
    char *compiler;
    char **probe;
    size_t nprobe;
    char **quote;
    size_t nquote;
    char **dirs;         /* angle-bracket chain */
    char **embed_dirs;   /* --embed-dir, for #embed and __has_embed */
    size_t nembed_dirs;
    bool *system_dir;
    size_t ndirs;
    Slot *table;
    size_t nslots, nmacros;
    unsigned next_id;
    Frame *frames;
    size_t depth, nframes;
    File *files;
    const char *supplied_identity;
    CeText supplied_source;
    TokVec pushback;
    size_t stop_depth;   /* the end of a file at this depth ends the input */
    char *base_file;
    unsigned counter, include_depth_limit;
    time_t epoch;
    Answer *answers;
    size_t nanswers;
    Assertion *assertions;  /* GCC #assert and #unassert, in order */
    size_t nassertions;
    /* output */
    FILE *out;
    size_t out_bytes;
    CePPOrigin *origins;
    size_t norigins, corigins;
    const File *out_file;
    unsigned out_line;
    bool out_bol, out_any, quiet;
    Token last;
    CeError *error;
    bool failed;
    unsigned errors, warnings;
} PP;

static bool fail(PP *pp, const File *file, unsigned line, const char *format, ...) {
    char message[400];
    va_list args; va_start(args,format); vsnprintf(message,sizeof(message),format,args); va_end(args);
    if (!pp->failed) {
        if (file) ce_error(pp->error,0,"%s:%u: %s",file->path,line,message);
        else ce_error(pp->error,0,"%s",message);
    }
    pp->failed = true; ++pp->errors;
    return false;
}

/* An error after which the compilers go on preprocessing: the output is
   complete, and the exit status reports it. */
static void report(PP *pp, const File *file, unsigned line, const char *format, ...) {
    char message[400];
    va_list args; va_start(args,format); vsnprintf(message,sizeof(message),format,args); va_end(args);
    fprintf(stderr,"%s:%u: error: %s\n",file ? file->path : "<command line>",line,message);
    ++pp->errors;
}

static void warn(PP *pp, const File *file, unsigned line, const char *format, ...) {
    char message[400];
    va_list args; va_start(args,format); vsnprintf(message,sizeof(message),format,args); va_end(args);
    if (!pp->quiet) fprintf(stderr,"%s:%u: warning: %s\n",file ? file->path : "<command line>",line,message);
    ++pp->warnings;
}

static char *dup_text(PP *pp, CeText t) {
    char *s = ce_alloc(&pp->arena,t.len+1);
    if (t.len) memcpy(s,t.data,t.len);
    s[t.len] = 0; return s;
}
static char *dup_str(PP *pp, const char *s) { return dup_text(pp,ce_cstr(&pp->arena,s)); }
static CeText text_of(PP *pp, const char *s) { return ce_cstr(&pp->arena,s); }

/* The lossless scanner can retain malformed identifiers. Active phase-4
   tokens must also satisfy the C identifier grammar, including Annex D's
   initial-character restriction. Do not duplicate those Unicode ranges. */
static bool check_identifier(PP *pp, CeText text, const File *file, unsigned line) {
    bool extended = false;
    for (size_t i = 0; i < text.len; ++i) extended |= text.data[i] >= 0x80 || text.data[i] == '\\';
    if (!extended) return true;
    for (size_t i = 0; i+1 < text.len; ++i) if (text.data[i] == '\\') {
        size_t digits = text.data[i+1] == 'u' ? 4 : text.data[i+1] == 'U' ? 8 : 0;
        uint32_t scalar = 0;
        if (!digits || i+2+digits > text.len) return fail(pp,file,line,"invalid universal character name");
        for (size_t j = 0; j < digits; ++j) {
            unsigned c = text.data[i+2+j];
            unsigned h = c <= '9' ? c-'0' : c <= 'F' ? c-'A'+10 : c-'a'+10;
            scalar = scalar*16+h;
        }
        if (scalar < 0xa0 && !(scalar == '$' && !pp->clang))
            return fail(pp,file,line,"universal character name cannot name a basic identifier character");
        if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
            return fail(pp,file,line,"universal character name is not a Unicode scalar value");
        i += 1+digits;
    }
    if (!pp->identifier_grammar_ready) {
        if (!ce_builtin_language(&pp->identifier_grammar,true,pp->error)) { pp->failed = true; return false; }
        pp->identifier_grammar_ready = true;
    }
    CeArena scratch = {0}; CeText key = ce_name_key(&scratch,text); bool accepted = false;
    for (size_t i = 0; i < pp->identifier_grammar.nlex && !accepted; ++i) {
        const CeLexRule *rule = &pp->identifier_grammar.lex[i]; size_t length = 0;
        if (ce_is(rule->name,"CIdent") &&
            ce_regex_match(&pp->identifier_grammar,rule->start,rule->end,key,0,&length) && length == key.len)
            accepted = true;
    }
    ce_arena_free(&scratch);
    return accepted || fail(pp,file,line,"extended character is not valid at this identifier position");
}

/* ---- The macro table ------------------------------------------------------- */

static uint64_t hash_text(CeText t) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < t.len; ++i) { h ^= t.data[i]; h *= 1099511628211ULL; }
    return h;
}

static Slot *slot_key(PP *pp, CeText name, bool create) {
    if (!pp->nslots) {
        pp->nslots = 4096;
        pp->table = calloc(pp->nslots,sizeof(*pp->table));
        if (!pp->table) { fputs("cesta: out of memory\n",stderr); exit(2); }
    }
    size_t mask = pp->nslots-1, i = hash_text(name) & mask;
    for (;; i = (i+1) & mask) {
        Slot *s = &pp->table[i];
        if (!s->used) {
            if (!create) return NULL;
            if ((pp->nmacros+1)*2 > pp->nslots) {
                Slot *old = pp->table; size_t n = pp->nslots;
                pp->nslots *= 2; pp->nmacros = 0;
                pp->table = calloc(pp->nslots,sizeof(*pp->table));
                if (!pp->table) { fputs("cesta: out of memory\n",stderr); exit(2); }
                for (size_t k = 0; k < n; ++k) if (old[k].used) { Slot *t = slot_key(pp,old[k].name,true); *t = old[k]; }
                free(old);
                return slot_key(pp,name,true);
            }
            s->used = true; s->name = ce_text(&pp->arena,name.data,name.len); ++pp->nmacros;
            return s;
        }
        if (ce_equal(s->name,name)) return s;
    }
}

/* Identity is shared with C scope and binding; replacement tokens keep their
   written spelling because stringification observes it. */
static Slot *slot(PP *pp, CeText name, bool create) {
    CeArena scratch = {0};
    Slot *result = slot_key(pp,ce_name_key(&scratch,name),create);
    ce_arena_free(&scratch);
    return result;
}

static Macro *lookup(PP *pp, CeText name) {
    Slot *s = slot(pp,name,false);
    return s ? s->macro : NULL;
}

static bool hidden(const Hideset *h, const Macro *m) {
    for (; h; h = h->next) if (h->macro == m) return true;
    return false;
}
static const Hideset *hide_add(PP *pp, const Hideset *h, const Macro *m) {
    if (hidden(h,m)) return h;
    Hideset *n = ce_alloc(&pp->arena,sizeof(*n)); n->macro = m; n->next = h; return n;
}
static const Hideset *hide_union(PP *pp, const Hideset *a, const Hideset *b) {
    for (; b; b = b->next) a = hide_add(pp,a,b->macro);
    return a;
}
static const Hideset *hide_intersect(PP *pp, const Hideset *a, const Hideset *b) {
    const Hideset *out = NULL;
    for (; a; a = a->next) if (hidden(b,a->macro)) out = hide_add(pp,out,a->macro);
    return out;
}

/* ---- Reading files --------------------------------------------------------- */

static bool exists(const char *path) {
    struct stat st; return stat(path,&st) == 0 && S_ISREG(st.st_mode);
}

static File *load_source(PP *pp, const char *path, CeText source);

static File *load(PP *pp, const char *path, const char *spelled, int found_in, bool system) {
    char *real = realpath(path,NULL);
    if (!real) return NULL;
    if (pp->supplied_identity && !strcmp(real,pp->supplied_identity)) {
        free(real);
        File *file = load_source(pp,spelled,pp->supplied_source);
        if (file) { file->found_in = found_in; file->system = system; }
        return file;
    }
    File *f = calloc(1,sizeof(*f));
    if (!f) { free(real); return NULL; }
    f->real = dup_str(pp,real); free(real);
    f->path = dup_str(pp,spelled); f->found_in = found_in; f->system = system;
    const char *slash = strrchr(spelled,'/');
    size_t dir = slash ? (size_t)(slash-spelled)+1 : 0;
    f->dir = ce_alloc(&pp->arena,dir+1); memcpy(f->dir,spelled,dir); f->dir[dir] = 0;
    CeText bytes; CeError error = {{0},0};
    if (!ce_read_file(&f->arena,path,&bytes,&error) ||
        !ce_source_scan(pp->grammar,&f->arena,bytes,pp->c11,&f->src,&error)) {
        fail(pp,NULL,0,"%s: %s",spelled,error.message);
        ce_source_free(&f->src); ce_arena_free(&f->arena); free(f); return NULL;
    }
    f->next = pp->files; pp->files = f;
    return f;
}

static File *load_source(PP *pp, const char *path, CeText source) {
    File *file = calloc(1,sizeof(*file));
    if (!file) return NULL;
    file->path = dup_str(pp,path); file->found_in = -1;
    char *real = realpath(path,NULL);
    file->real = dup_str(pp,real ? real : path); free(real);
    const char *slash = strrchr(path,'/'); size_t n = slash ? (size_t)(slash-path)+1 : 0;
    file->dir = ce_alloc(&pp->arena,n+1); memcpy(file->dir,path,n); file->dir[n] = 0;
    CeText bytes = ce_text(&file->arena,source.data,source.len);
    CeError error = {{0},0};
    if (!ce_source_scan(pp->grammar,&file->arena,bytes,pp->c11,&file->src,&error)) {
        fail(pp,NULL,0,"%s: %s",path,error.message);
        ce_source_free(&file->src); ce_arena_free(&file->arena); free(file); return NULL;
    }
    file->next = pp->files; pp->files = file;
    return file;
}

static bool once_seen(PP *pp, const char *path) {
    char *real = realpath(path,NULL);
    if (!real) return false;
    bool seen = false;
    for (File *f = pp->files; f && !seen; f = f->next) seen = f->once && !strcmp(f->real,real);
    free(real);
    return seen;
}

/* A file's __FILE__ spelling: GCC joins the directory and the name as
   written; Clang names the main file's own directory "." and removes "./"
   components from the name it appends. */
static char *spell_path(PP *pp, const char *dir, const char *name) {
    if (pp->clang) {
        const char *base = *dir ? dir : "./";
        while (name[0] == '.' && name[1] == '/') name += 2;
        size_t n = strlen(base), m = strlen(name);
        char *s = ce_alloc(&pp->arena,n+m+2);
        memcpy(s,base,n);
        if (n && s[n-1] != '/') s[n++] = '/';
        memcpy(s+n,name,m+1);
        return s;
    }
    size_t n = strlen(dir), m = strlen(name);
    char *s = ce_alloc(&pp->arena,n+m+2);
    memcpy(s,dir,n);
    if (n && s[n-1] != '/') s[n++] = '/';
    memcpy(s+n,name,m+1);
    return s;
}

/* ---- Output ---------------------------------------------------------------- */

static unsigned presumed_line(const Frame *f, unsigned physical) { return (unsigned)((long)physical+f->delta); }

/* Every byte of the output passes here, so offsets stay exact. */
static void emit_bytes(PP *pp, const void *data, size_t len) {
    if (len) fwrite(data,1,len,pp->out);
    pp->out_bytes += len;
}
static void emit_char(PP *pp, char c) { fputc(c,pp->out); ++pp->out_bytes; }

static void marker(PP *pp, const char *path, unsigned line, const char *flags) {
    if (!pp->markers || pp->quiet) return;
    if (!pp->out_bol) emit_char(pp,'\n');
    char head[32]; int n = snprintf(head,sizeof(head),"# %u \"",line);
    emit_bytes(pp,head,(size_t)n);
    for (const char *p = path; *p; ++p) { if (*p == '"' || *p == '\\') emit_char(pp,'\\'); emit_char(pp,*p); }
    emit_char(pp,'"'); emit_bytes(pp,flags,strlen(flags)); emit_char(pp,'\n');
    pp->out_bol = true; pp->out_line = line;
}

/* Whether two adjacent tokens could read as one: GCC's avoid-paste rule,
   decided here by lexing their spellings together. */
static bool would_paste(PP *pp, const Token *a, const Token *b) {
    if (a->kind == CE_PP_IDENTIFIER || a->kind == CE_PP_NUMBER)
        if (b->kind == CE_PP_IDENTIFIER || b->kind == CE_PP_NUMBER || b->kind == CE_PP_CHARACTER || b->kind == CE_PP_STRING ||
            (a->kind == CE_PP_NUMBER && (punct(b,".") || punct(b,"+") || punct(b,"-"))))
            return true;
    if (a->kind != CE_PP_PUNCTUATOR) return a->kind == CE_PP_OTHER;
    if (punct(a,"/") && (punct(b,"/") || punct(b,"*") || punct(b,"/=") || punct(b,"*="))) return true;
    if (punct(a,".") && (punct(b,".") || punct(b,"..."))) return true;
    if (b->kind != CE_PP_PUNCTUATOR && !(punct(a,".") && b->kind == CE_PP_NUMBER)) return false;
    char buffer[16];
    if (a->text.len+b->text.len >= sizeof(buffer)) return true;
    memcpy(buffer,a->text.data,a->text.len); memcpy(buffer+a->text.len,b->text.data,b->text.len);
    CeArena scratch = {0};
    unsigned kind = ce_source_token_kind(&scratch,(CeText){(unsigned char *)buffer,a->text.len+b->text.len},pp->c11);
    ce_arena_free(&scratch);
    if (kind) return true;
    /* a longer punctuator could still start inside the pair */
    for (size_t n = a->text.len+1; n < a->text.len+b->text.len; ++n) {
        CeArena s2 = {0};
        unsigned k = ce_source_token_kind(&s2,(CeText){(unsigned char *)buffer,n},pp->c11);
        ce_arena_free(&s2);
        if (k == CE_PP_PUNCTUATOR) return true;
    }
    return false;
}

/* Output follows expansion points: a token is written on the line of the
   source it was expanded at, in the file being read. */
static void write_line_to(PP *pp, const Token *t) {
    if (pp->quiet) return;
    unsigned line = t->expansion_line;
    if (line > pp->out_line) {
        const char *path = pp->depth ? pp->frames[pp->depth-1].presumed : "";
        if (pp->markers && line-pp->out_line > 8) marker(pp,path,line,"");
        else { while (pp->out_line < line) { emit_char(pp,'\n'); ++pp->out_line; } pp->out_bol = true; }
    }
}

/* Clang emits UTF-8 identifiers; GCC emits each extended character as an
   eight-digit UCN. This runs only at output, after # and ## have observed
   the replacement token's spelling. */
static CeText identifier_output(PP *pp, CeArena *scratch, CeText spelling) {
    CeText key = ce_name_key(scratch,spelling);
    if (pp->clang) return key;
    bool extended = false;
    for (size_t i = 0; i < key.len; ++i) extended |= key.data[i] >= 0x80;
    if (!extended) return key;
    unsigned char *out = ce_alloc(scratch,10*key.len+1); size_t at = 0;
    for (size_t i = 0; i < key.len;) {
        unsigned c = key.data[i++];
        if (c < 0x80) { out[at++] = (unsigned char)c; continue; }
        unsigned following = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        uint32_t scalar = c & (following == 3 ? 7 : following == 2 ? 15 : 31);
        if (!following || i+following > key.len) return spelling;
        for (unsigned j = 0; j < following; ++j) {
            unsigned next = key.data[i++];
            if ((next & 0xc0) != 0x80) return spelling;
            scalar = (scalar << 6) | (next & 63);
        }
        int n = snprintf((char *)out+at,11,"\\U%08x",scalar);
        at += (size_t)n;
    }
    return (CeText){out,at};
}

static void write_token(PP *pp, const Token *t) {
    if (pp->quiet || t->kind == PP_PLACEMARKER) return;
    if (t->kind == PP_PRAGMA) {
        if (!pp->out_bol) { emit_char(pp,'\n'); ++pp->out_line; }
        emit_bytes(pp,t->text.data,t->text.len); emit_char(pp,'\n'); ++pp->out_line;
        pp->out_bol = true; pp->last.kind = 0;
        return;
    }
    write_line_to(pp,t);
    if (!pp->out_bol && ((t->flags & T_SPACE) || (pp->last.kind && would_paste(pp,&pp->last,t)))) emit_char(pp,' ');
    if (pp->norigins == pp->corigins) {
        pp->corigins = pp->corigins ? pp->corigins*2 : 4096;
        pp->origins = ce_resize(pp->origins,pp->corigins,sizeof(*pp->origins));
    }
    CeArena scratch = {0};
    CeText text = t->kind == CE_PP_IDENTIFIER ? identifier_output(pp,&scratch,t->text) : t->text;
    pp->origins[pp->norigins++] = (CePPOrigin){pp->out_bytes,text.len,t->file ? t->file->path : "",
                                               t->line,t->column,t->expansion,t->argument,(t->flags & T_PASTED) != 0};
    emit_bytes(pp,text.data,text.len);
    ce_arena_free(&scratch);
    pp->out_bol = false; pp->last = *t;
}

static void write_directive_line(PP *pp, const char *text, size_t len) {
    if (pp->quiet) return;
    Token t = {.kind = PP_PRAGMA, .text = {(unsigned char *)text,len}};
    write_token(pp,&t);
}

/* ---- The input ------------------------------------------------------------- */

static Token from_source(PP *pp, Frame *f, const CeSourceToken *s, bool space) {
    if (s->kind == CE_PP_IDENTIFIER) check_identifier(pp,s->value,f->file,s->line);
    Token t = {.kind = s->kind, .flags = (s->space_before || space) ? T_SPACE : 0, .text = s->value,
               .file = f->file, .line = s->line, .column = s->column};
    t.expansion_line = presumed_line(f,s->line);
    return t;
}

static bool directive(PP *pp, Frame *f);

/* The next token: pushed-back tokens, then the files, where directive
   lines are executed. Inside a macro invocation the current file's end is
   the end of input (an invocation does not continue past it). */
enum { READ_ANY, READ_IN_FILE, READ_STOP_AT_DIRECTIVE };
static bool next_token(PP *pp, Token *out, unsigned mode) {
    if (pp->pushback.len) { *out = pp->pushback.data[--pp->pushback.len]; return true; }
    bool space = false;
    while (pp->depth && !pp->failed) {
        Frame *f = &pp->frames[pp->depth-1];
        CeSourceFile *s = &f->file->src;
        if (f->pos >= s->ntokens) {
            if (mode != READ_ANY) return false;
            if (f->nconds != f->base) return fail(pp,f->file,s->lines,"unterminated conditional directive");
            const File *done = f->file;
            free(f->conds);
            --pp->depth;
            if (!pp->depth) return false;
            bool stop = pp->depth == pp->stop_depth;
            Frame *back = &pp->frames[pp->depth-1];
            unsigned line = back->pos < back->file->src.ntokens ? presumed_line(back,back->file->src.tokens[back->pos].line) : 1;
            if (done) marker(pp,back->presumed,line," 2");
            pp->out_file = back->file; pp->out_line = line;
            if (stop) return false;
            continue;
        }
        const CeSourceToken *t = &s->tokens[f->pos];
        if (t->kind == CE_PP_MARKER) { ++f->pos; continue; }
        if (t->kind == CE_PP_NEWLINE) { ++f->pos; space = true; continue; }
        if (t->directive) {
            if (mode == READ_STOP_AT_DIRECTIVE) return false;
            if (!directive(pp,f)) return false;
            space = true;
            /* a directive's own output (#embed) comes before the next line */
            if (pp->pushback.len) { *out = pp->pushback.data[--pp->pushback.len]; return true; }
            continue;
        }
        ++f->pos;
        *out = from_source(pp,f,t,space);
        return true;
    }
    return false;
}

/* ---- Macro replacement ----------------------------------------------------- */

/* Reading from an isolated token list (an argument, an #if line) or from
   the program's input (input == NULL). */
static bool input_next(PP *pp, TokVec *input, Token *out, unsigned mode) {
    if (input) {
        if (!input->len) return false;
        *out = input->data[--input->len];
        return true;
    }
    return next_token(pp,out,mode);
}
static void input_unread(PP *pp, TokVec *input, const Token *t) {
    tpush(input ? input : &pp->pushback,*t);
}

static Token make_token(PP *pp, unsigned kind, const char *text, const Token *at) {
    Token t = *at;
    t.kind = kind; t.text = text_of(pp,text); t.flags &= T_SPACE; t.hide = NULL;
    return t;
}

static int expand(PP *pp, Token *t, TokVec *input);

/* Fully replace an argument on its own (6.10.3.1). */
static TokVec expand_isolated(PP *pp, const TokVec *in) {
    TokVec stack = {0}, out = {0};
    for (size_t i = in->len; i; --i) tpush(&stack,in->data[i-1]);
    Token t;
    while (!pp->failed && input_next(pp,&stack,&t,READ_ANY)) {
        if (t.kind == CE_PP_IDENTIFIER) {
            int r = expand(pp,&t,&stack);
            if (r < 0) break;
            if (r > 0) continue;
        }
        tpush(&out,t);
    }
    free(stack.data);
    return out;
}

static CeText stringize(PP *pp, const TokVec *arg) {
    size_t length = 2;
    for (size_t i = 0; i < arg->len; ++i) length += 2*arg->data[i].text.len+1;
    char *s = ce_alloc(&pp->arena,length+1); size_t n = 0;
    s[n++] = '"';
    for (size_t i = 0; i < arg->len; ++i) {
        const Token *t = &arg->data[i];
        if (t->kind == PP_PLACEMARKER) continue;
        if (n > 1 && (t->flags & T_SPACE)) s[n++] = ' ';
        bool literal = t->kind == CE_PP_STRING || t->kind == CE_PP_CHARACTER || t->kind == CE_PP_RAW_STRING ||
                       t->kind == CE_PP_UNTERMINATED;
        for (size_t k = 0; k < t->text.len; ++k) {
            unsigned char c = t->text.data[k];
            if (literal && (c == '"' || c == '\\')) s[n++] = '\\';
            s[n++] = (char)c;
        }
    }
    s[n++] = '"';
    return (CeText){(unsigned char *)s,n};
}

static int param_index(const Macro *m, const Token *t) {
    if (t->kind != CE_PP_IDENTIFIER || !m->function) return -1;
    for (size_t i = 0; i < m->nparams; ++i) if (ce_name_equal(m->params[i],t->text)) return (int)i;
    return -1;
}

typedef struct {
    TokVec *raw, *expanded;
    bool *have_expanded;
    size_t n;
    bool variadic_absent;
} Args;

static const TokVec *expanded_arg(PP *pp, Args *a, size_t i) {
    if (!a->have_expanded[i]) { a->expanded[i] = expand_isolated(pp,&a->raw[i]); a->have_expanded[i] = true; }
    return &a->expanded[i];
}

static bool nonempty(const TokVec *v) {
    for (size_t i = 0; i < v->len; ++i) if (v->data[i].kind != PP_PLACEMARKER) return true;
    return false;
}

/* Paste a ## b into one token, as 6.10.3.3 requires; when the spellings do
   not form one token, GCC reports it and keeps both. */
static bool paste(PP *pp, Token *a, const Token *b, TokVec *out) {
    if (a->kind == PP_PLACEMARKER) { Token c = *b; c.flags = (c.flags & ~T_SPACE) | (a->flags & T_SPACE); *a = c; return true; }
    if (b->kind == PP_PLACEMARKER) return true;
    size_t n = a->text.len+b->text.len;
    unsigned char *s = ce_alloc(&pp->arena,n+1);
    memcpy(s,a->text.data,a->text.len); memcpy(s+a->text.len,b->text.data,b->text.len); s[n] = 0;
    CeText text = {s,n};
    unsigned kind = ce_source_token_kind(&pp->arena,text,pp->c11);
    if (!kind) {
        report(pp,a->file,a->line,"pasting \"%.*s\" and \"%.*s\" does not give a valid preprocessing token",
             (int)a->text.len,a->text.data,(int)b->text.len,b->text.data);
        tpush(out,*a); *a = *b; return false;
    }
    /* A new token (Prosser's glue): the hideset both operands share, and
       neither operand's non-replacement mark. */
    a->kind = kind; a->text = text; a->hide = hide_intersect(pp,a->hide,b->hide);
    if (kind == CE_PP_IDENTIFIER && !check_identifier(pp,text,a->file,a->line)) return false;
    a->flags = (a->flags & T_SPACE) | T_PASTED;
    return true;
}

static CePPExpansion *expansion_of(PP *pp, const Macro *m, const Token *name) {
    CePPExpansion *e = ce_alloc(&pp->arena,sizeof(*e));
    e->macro = m->name; e->definition_file = m->file ? m->file->path : "<built-in>"; e->definition_line = m->line;
    /* a macro name that was itself substituted as an argument expands within
       that outer expansion */
    e->file = name->file ? name->file->path : ""; e->line = name->line; e->column = name->column;
    e->parent = name->expansion ? name->expansion : name->argument;
    return e;
}

/* Substitute the arguments into the replacement list (6.10.3.1-3). */
static TokVec substitute(PP *pp, const Macro *m, Args *a, const Token *name, const Token *close, const CePPExpansion *e) {
    TokVec items = {0};
    unsigned line = pp->clang ? close->expansion_line : name->expansion_line;
    size_t variadic = m->variadic ? m->nparams-1 : SIZE_MAX;
    for (size_t i = 0; i < m->nbody; ++i) {
        Token t = m->body[i];
        t.expansion_line = line; t.expansion = e; t.file = m->file;
        if (m->function && punct(&t,"#") && i+1 < m->nbody && param_index(m,&m->body[i+1]) >= 0) {
            int p = param_index(m,&m->body[i+1]);
            Token s = t; s.kind = CE_PP_STRING; s.text = stringize(pp,&a->raw[p]); s.flags = t.flags & T_SPACE;
            tpush(&items,s); ++i; continue;
        }
        if (m->function && is(&t,CE_PP_IDENTIFIER,"__VA_OPT__") && m->variadic && i+1 < m->nbody && punct(&m->body[i+1],"(")) {
            size_t depth = 0, k = i+1, close_at = i+1;
            for (; k < m->nbody; ++k) {
                if (punct(&m->body[k],"(")) ++depth;
                else if (punct(&m->body[k],")") && !--depth) { close_at = k; break; }
            }
            Macro inner = *m; inner.body = m->body+i+2; inner.nbody = close_at-i-2;
            bool present = nonempty(expanded_arg(pp,a,variadic));
            if (present) {
                TokVec sub = substitute(pp,&inner,a,name,close,e);
                if (sub.len) sub.data[0].flags = (sub.data[0].flags & ~T_SPACE) | (t.flags & T_SPACE);
                for (size_t j = 0; j < sub.len; ++j) tpush(&items,sub.data[j]);
                free(sub.data);
            } else {
                Token pm = t; pm.kind = PP_PLACEMARKER; pm.text = text_of(pp,""); tpush(&items,pm);
            }
            i = close_at; continue;
        }
        if (punct(&t,"##") && m->nbody > 1) { t.kind = PP_PASTE; tpush(&items,t); continue; }
        int p = param_index(m,&t);
        if (p < 0) { t.hide = NULL; tpush(&items,t); continue; }
        bool before = i > 0 && punct(&m->body[i-1],"##");
        bool after = i+1 < m->nbody && punct(&m->body[i+1],"##");
        /* GNU: , ## __VA_ARGS__ drops the comma when the variable arguments
           are absent, or (in the GNU dialects) when they are the macro's only
           parameter and were written with no tokens; otherwise it inserts
           them macro-replaced. */
        if ((size_t)p == variadic && before && i >= 2 && punct(&m->body[i-2],",")) {
            if (a->variadic_absent || (!pp->c11 && m->nparams == 1 && !nonempty(&a->raw[p]))) {
                while (items.len && items.data[items.len-1].kind == PP_PASTE) --items.len;
                if (items.len && punct(&items.data[items.len-1],",")) --items.len;
                continue;
            }
            while (items.len && items.data[items.len-1].kind == PP_PASTE) --items.len;
            const TokVec *arg = expanded_arg(pp,a,(size_t)p);
            for (size_t j = 0; j < arg->len; ++j) {
                Token x = arg->data[j];
                if (!j) x.flags = (x.flags & ~T_SPACE) | (t.flags & T_SPACE);
                x.argument = e;
                tpush(&items,x);
            }
            continue;
        }
        const TokVec *arg = (before || after) ? &a->raw[p] : expanded_arg(pp,a,(size_t)p);
        if (!nonempty(arg)) {
            Token pm = t; pm.kind = PP_PLACEMARKER; pm.text = text_of(pp,""); tpush(&items,pm); continue;
        }
        for (size_t j = 0; j < arg->len; ++j) {
            Token x = arg->data[j];
            if (!j) x.flags = (x.flags & ~T_SPACE) | (t.flags & T_SPACE);
            x.argument = e;
            tpush(&items,x);
        }
    }
    /* ## from left to right (6.10.3.3p3). */
    TokVec out = {0};
    for (size_t i = 0; i < items.len; ++i) {
        Token t = items.data[i];
        if (t.kind == PP_PASTE) continue;
        while (i+2 < items.len && items.data[i+1].kind == PP_PASTE) {
            (void)paste(pp,&t,&items.data[i+2],&out);
            i += 2;
        }
        tpush(&out,t);
    }
    free(items.data);
    TokVec result = {0};
    for (size_t i = 0; i < out.len; ++i) if (out.data[i].kind != PP_PLACEMARKER) tpush(&result,out.data[i]);
    free(out.data);
    return result;
}

/* Collect a function-like macro's arguments (6.10.3p10-12). */
static bool collect(PP *pp, const Macro *m, TokVec *input, const Token *name, Args *a, Token *close) {
    size_t capacity = m->nparams ? m->nparams : 1;
    a->raw = calloc(capacity,sizeof(*a->raw)); a->expanded = calloc(capacity,sizeof(*a->expanded));
    a->have_expanded = calloc(capacity,sizeof(*a->have_expanded)); a->n = 1; a->variadic_absent = false;
    if (!a->raw || !a->expanded || !a->have_expanded) { fputs("cesta: out of memory\n",stderr); exit(2); }
    size_t depth = 0;
    for (;;) {
        Token t;
        if (!input_next(pp,input,&t,READ_IN_FILE)) {
            if (!pp->failed) fail(pp,name->file,name->line,"unterminated argument list invoking macro \"%.*s\"",(int)m->name.len,m->name.data);
            return false;
        }
        if (punct(&t,"(")) ++depth;
        else if (punct(&t,")")) {
            if (!depth) { *close = t; break; }
            --depth;
        } else if (punct(&t,",") && !depth && !(m->variadic && a->n == m->nparams)) {
            if (a->n == capacity) {
                capacity *= 2;
                a->raw = ce_resize(a->raw,capacity,sizeof(*a->raw));
                a->expanded = ce_resize(a->expanded,capacity,sizeof(*a->expanded));
                a->have_expanded = ce_resize(a->have_expanded,capacity,sizeof(*a->have_expanded));
                for (size_t k = a->n; k < capacity; ++k) { a->raw[k] = (TokVec){0}; a->expanded[k] = (TokVec){0}; a->have_expanded[k] = false; }
            }
            ++a->n; continue;
        }
        if (!a->raw[a->n-1].len) t.flags &= ~T_SPACE;
        tpush(&a->raw[a->n-1],t);
    }
    if (m->nparams == 0 && a->n == 1 && !a->raw[0].len) return true;
    if (m->variadic && a->n == m->nparams-1) { a->variadic_absent = true; ++a->n; return true; }
    if (a->n != m->nparams) {
        return fail(pp,name->file,name->line,"macro \"%.*s\" %s %zu arguments, but %s %zu given",(int)m->name.len,m->name.data,
                    a->n < m->nparams ? "requires" : "passed",a->n < m->nparams ? m->nparams : a->n,
                    a->n < m->nparams ? "only" : "takes just",a->n < m->nparams ? a->n : m->nparams);
    }
    return true;
}

static void free_args(Args *a) {
    if (!a->raw) return;
    for (size_t i = 0; i < a->n; ++i) { free(a->raw[i].data); free(a->expanded[i].data); }
    free(a->raw); free(a->expanded); free(a->have_expanded);
}

static void month_day(PP *pp, Token *t, unsigned which) {
    struct tm tm; time_t when = pp->epoch;
    gmtime_r(&when,&tm);
    static const char *const months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char buffer[32];
    if (which == B_DATE) snprintf(buffer,sizeof(buffer),"\"%s %2d %d\"",months[tm.tm_mon],tm.tm_mday,tm.tm_year+1900);
    else snprintf(buffer,sizeof(buffer),"\"%02d:%02d:%02d\"",tm.tm_hour,tm.tm_min,tm.tm_sec);
    t->text = text_of(pp,buffer);
}

static CeText quoted_path(PP *pp, const char *path) {
    size_t n = strlen(path);
    char *s = ce_alloc(&pp->arena,2*n+3); size_t k = 0;
    s[k++] = '"';
    for (size_t i = 0; i < n; ++i) { if (path[i] == '"' || path[i] == '\\') s[k++] = '\\'; s[k++] = path[i]; }
    s[k++] = '"'; s[k] = 0;
    return (CeText){(unsigned char *)s,k};
}

static const Frame *current_frame(PP *pp) { return pp->depth ? &pp->frames[pp->depth-1] : NULL; }

static bool do_pragma(PP *pp, Frame *f, const Token *tokens, size_t n, const Token *at, bool operator);

/* Replace the macro that t names, when it is one and is not hidden: the
   result is pushed onto the input for rescanning. 1 when replaced, 0 when
   t stays, -1 on an error. */
static int expand(PP *pp, Token *t, TokVec *input) {
    Macro *m = lookup(pp,t->text);
    if (!m || (t->flags & T_NOEXPAND)) return 0;
    if (hidden(t->hide,m)) { t->flags |= T_NOEXPAND; return 0; }
    if (m->builtin) {
        Token r = make_token(pp,CE_PP_NUMBER,"0",t);
        r.hide = hide_add(pp,t->hide,m);
        const Frame *f = current_frame(pp);
        char buffer[64];
        switch (m->builtin) {
        case B_LINE: snprintf(buffer,sizeof(buffer),"%u",t->expansion_line); r.text = text_of(pp,buffer); break;
        case B_FILE: r.kind = CE_PP_STRING; r.text = quoted_path(pp,f ? f->presumed : ""); break;
        case B_BASE_FILE: r.kind = CE_PP_STRING; r.text = quoted_path(pp,pp->base_file ? pp->base_file : ""); break;
        case B_FILE_NAME: {
            const char *p = f ? f->presumed : ""; const char *slash = strrchr(p,'/');
            r.kind = CE_PP_STRING; r.text = quoted_path(pp,slash ? slash+1 : p); break;
        }
        case B_COUNTER: snprintf(buffer,sizeof(buffer),"%u",pp->counter++); r.text = text_of(pp,buffer); break;
        case B_INCLUDE_LEVEL: snprintf(buffer,sizeof(buffer),"%zu",pp->depth ? pp->depth-1 : 0); r.text = text_of(pp,buffer); break;
        case B_DATE: case B_TIME: r.kind = CE_PP_STRING; month_day(pp,&r,m->builtin); break;
        case B_TIMESTAMP: r.kind = CE_PP_STRING; r.text = text_of(pp,"\"??? ??? ?? ??:??:?? ????\""); break;
        case B_PRAGMA: {
            /* _Pragma ( string-literal ) */
            Token open, s, close;
            if (!input_next(pp,input,&open,READ_IN_FILE) || !punct(&open,"(") ||
                !input_next(pp,input,&s,READ_IN_FILE) || s.kind != CE_PP_STRING ||
                !input_next(pp,input,&close,READ_IN_FILE) || !punct(&close,")"))
                return fail(pp,t->file,t->line,"_Pragma takes a parenthesized string literal") ? -1 : -1;
            /* destringize (6.10.9): drop the quotes and the L prefix, unescape \" and \\ */
            CeText v = s.text; size_t b = 0;
            while (b < v.len && v.data[b] != '"') ++b;
            char *text = ce_alloc(&pp->arena,v.len+1); size_t k = 0;
            for (size_t i = b+1; i+1 < v.len; ++i) {
                if (v.data[i] == '\\' && (v.data[i+1] == '"' || v.data[i+1] == '\\')) ++i;
                text[k++] = (char)v.data[i];
            }
            text[k] = 0;
            CeArena scratch = {0}; CeSourceFile line; CeError error = {{0},0};
            CeText bytes = {(unsigned char *)text,k};
            if (!ce_source_scan(pp->grammar,&scratch,bytes,pp->c11,&line,&error)) { ce_arena_free(&scratch); return 0; }
            TokVec toks = {0};
            Frame fake = {.file = (File *)t->file};
            for (size_t i = 0; i < line.ntokens; ++i)
                if (line.tokens[i].kind != CE_PP_MARKER && line.tokens[i].kind != CE_PP_NEWLINE) {
                    Token x = from_source(pp,&fake,&line.tokens[i],false);
                    x.text = ce_text(&pp->arena,x.text.data,x.text.len);
                    x.expansion_line = t->expansion_line; x.file = t->file;
                    tpush(&toks,x);
                }
            ce_source_free(&line); ce_arena_free(&scratch);
            bool ok = do_pragma(pp,NULL,toks.data,toks.len,t,true);
            free(toks.data);
            return ok ? 1 : -1;
        }
        }
        r.expansion_line = t->expansion_line;
        tpush(input ? input : &pp->pushback,r);
        return 1;
    }
    Args args = {0}; Token close = *t;
    if (m->function) {
        Token p;
        if (!input_next(pp,input,&p,input ? READ_ANY : READ_STOP_AT_DIRECTIVE)) return 0;
        if (!punct(&p,"(")) { input_unread(pp,input,&p); return 0; }
        if (!collect(pp,m,input,t,&args,&close)) { free_args(&args); return -1; }
    }
    /* An argument's tokens belong to this invocation: macros expanded in
       them while they are replaced first expand within it too. */
    CePPExpansion *e = expansion_of(pp,m,t);
    for (size_t i = 0; m->function && i < args.n; ++i)
        for (size_t j = 0; j < args.raw[i].len; ++j) args.raw[i].data[j].argument = e;
    TokVec body = substitute(pp,m,&args,t,&close,e);
    const Hideset *hs = hide_add(pp,m->function ? hide_intersect(pp,t->hide,close.hide) : t->hide,m);
    for (size_t i = 0; i < body.len; ++i) body.data[i].hide = hide_union(pp,body.data[i].hide,hs);
    if (body.len) body.data[0].flags = (body.data[0].flags & ~T_SPACE) | (t->flags & T_SPACE);
    else if (t->flags & T_SPACE) {
        /* An empty replacement keeps the separation its name had. */
        Token pm = *t; pm.kind = PP_PLACEMARKER; pm.text = text_of(pp,""); tpush(&body,pm);
    }
    TokVec *target = input ? input : &pp->pushback;
    for (size_t i = body.len; i; --i) tpush(target,body.data[i-1]);
    free(body.data); free_args(&args);
    return 1;
}

/* ---- Definitions ----------------------------------------------------------- */

static Token *copy_tokens(PP *pp, const Token *t, size_t n) {
    Token *c = ce_alloc(&pp->arena,(n ? n : 1)*sizeof(*c));
    if (n) memcpy(c,t,n*sizeof(*c));
    return c;
}

static bool same_definition(const Macro *a, const Macro *b) {
    if (a->function != b->function || a->variadic != b->variadic || a->nparams != b->nparams || a->nbody != b->nbody) return false;
    for (size_t i = 0; i < a->nparams; ++i) if (!ce_name_equal(a->params[i],b->params[i])) return false;
    for (size_t i = 0; i < a->nbody; ++i)
        if (!(a->body[i].kind == CE_PP_IDENTIFIER && b->body[i].kind == CE_PP_IDENTIFIER ?
              ce_name_equal(a->body[i].text,b->body[i].text) : ce_equal(a->body[i].text,b->body[i].text)) ||
            (i && (a->body[i].flags & T_SPACE) != (b->body[i].flags & T_SPACE))) return false;
    return true;
}

static bool define(PP *pp, const Token *t, size_t n, const File *file, unsigned line) {
    if (!n || t[0].kind != CE_PP_IDENTIFIER) return fail(pp,file,line,"macro names must be identifiers");
    if (ce_is(t[0].text,"defined")) return fail(pp,file,line,"\"defined\" cannot be used as a macro name");
    Macro *m = ce_alloc(&pp->arena,sizeof(*m));
    m->name = ce_text(&pp->arena,t[0].text.data,t[0].text.len); m->id = pp->next_id++;
    m->file = file; m->line = line;
    size_t i = 1;
    if (n > 1 && punct(&t[1],"(") && !(t[1].flags & T_SPACE)) {
        m->function = true; i = 2;
        /* Each parameter consumes at least one input token. The extra slot
           allows the unnamed variadic parameter without a fixed-size limit. */
        CeText *params = ce_alloc(&pp->arena,(n+1)*sizeof(*params)); size_t np = 0;
        if (i < n && punct(&t[i],")")) ++i;
        else for (;;) {
            if (i < n && punct(&t[i],"...")) {
                params[np++] = text_of(pp,"__VA_ARGS__"); m->variadic = true; ++i;
                if (i >= n || !punct(&t[i],")")) return fail(pp,file,line,"missing ')' in macro parameter list");
                ++i; break;
            }
            if (i >= n || t[i].kind != CE_PP_IDENTIFIER) return fail(pp,file,line,"expected parameter name");
            for (size_t k = 0; k < np; ++k) if (ce_name_equal(params[k],t[i].text)) return fail(pp,file,line,"duplicate macro parameter \"%.*s\"",(int)t[i].text.len,t[i].text.data);
            params[np++] = t[i].text; ++i;
            if (i < n && punct(&t[i],"...")) { m->variadic = true; ++i; if (i >= n || !punct(&t[i],")")) return fail(pp,file,line,"missing ')' in macro parameter list"); ++i; break; }
            if (i < n && punct(&t[i],")")) { ++i; break; }
            if (i < n && punct(&t[i],",")) { ++i; continue; }
            return fail(pp,file,line,"expected ',' or ')' in macro parameter list");
        }
        m->params = ce_alloc(&pp->arena,(np ? np : 1)*sizeof(*m->params));
        memcpy(m->params,params,np*sizeof(*params)); m->nparams = np;
    }
    m->body = copy_tokens(pp,t+i,n-i); m->nbody = n-i;
    for (size_t k = 0; k < m->nbody; ++k) {
        m->body[k].hide = NULL;
        if (!k) m->body[k].flags &= ~T_SPACE;
    }
    if (m->nbody && (punct(&m->body[0],"##") || punct(&m->body[m->nbody-1],"##")))
        return fail(pp,file,line,"'##' cannot appear at either end of a macro expansion");
    Slot *s = slot(pp,m->name,true);
    if (s->macro && s->macro->builtin) { warn(pp,file,line,"redefining builtin macro \"%.*s\"",(int)m->name.len,m->name.data); }
    else if (s->macro && !same_definition(s->macro,m))
        warn(pp,file,line,"\"%.*s\" redefined",(int)m->name.len,m->name.data);
    s->macro = m;
    return true;
}

static void undefine(PP *pp, CeText name) {
    Slot *s = slot(pp,name,false);
    if (s) s->macro = NULL;
}

static void builtin(PP *pp, const char *name, unsigned kind) {
    Macro *m = ce_alloc(&pp->arena,sizeof(*m));
    m->name = text_of(pp,name); m->id = pp->next_id++; m->builtin = kind;
    slot(pp,m->name,true)->macro = m;
}

/* ---- #if expressions ------------------------------------------------------- */

typedef struct { uintmax_t v; bool unsigned_; } Value;
typedef struct {
    PP *pp;
    Token *t;
    size_t n, i;
    const File *file;
    unsigned line;
    bool ok;
} Expr;

static bool expr_fail(Expr *e, const char *message) {
    if (e->ok) fail(e->pp,e->file,e->line,"%s",message);
    e->ok = false; return false;
}

static bool integer_constant(CeText s, Value *out) {
    size_t i = 0; unsigned base = 10; uintmax_t v = 0; bool overflow = false;
    if (s.len > 1 && s.data[0] == '0' && (s.data[1] == 'x' || s.data[1] == 'X')) { base = 16; i = 2; }
    else if (s.len > 1 && s.data[0] == '0' && (s.data[1] == 'b' || s.data[1] == 'B')) { base = 2; i = 2; }
    else if (s.len > 0 && s.data[0] == '0') base = 8;
    size_t digits = 0;
    for (; i < s.len; ++i, ++digits) {
        unsigned c = s.data[i], d;
        if (c >= '0' && c <= '9') d = c-'0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c-'a'+10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c-'A'+10;
        else break;
        if (d >= base) return false;
        if (v > (UINTMAX_MAX-d)/base) overflow = true;
        v = v*base+d;
    }
    if ((base == 16 || base == 2) && !digits) return false;
    bool u = false; size_t l = 0;
    for (; i < s.len; ++i) {
        unsigned c = s.data[i];
        if ((c == 'u' || c == 'U') && !u) u = true;
        else if ((c == 'l' || c == 'L') && l < 2 && (l == 0 || s.data[i-1] == c)) ++l;
        else return false;
    }
    (void)overflow;
    out->v = v; out->unsigned_ = u || v > INTMAX_MAX;
    return true;
}

static bool character_constant(Expr *e, CeText s, Value *out) {
    size_t i = 0; bool wide = false;
    if (i < s.len && (s.data[i] == 'L' || s.data[i] == 'u' || s.data[i] == 'U')) { wide = true; ++i; }
    if (i+1 >= s.len || s.data[i] != '\'') return expr_fail(e,"invalid character constant in #if");
    ++i;
    intmax_t value = 0; unsigned count = 0; bool unsigned_char = e->pp->char_unsigned;
    while (i < s.len && s.data[i] != '\'') {
        uintmax_t c = s.data[i++];
        if (c == '\\' && i < s.len) {
            unsigned d = s.data[i++];
            switch (d) {
            case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break;
            case 'a': c = 7; break; case 'b': c = 8; break; case 'f': c = 12; break; case 'v': c = 11; break;
            case 'e': case 'E': c = 27; break;
            case 'x': c = 0; while (i < s.len && strchr("0123456789abcdefABCDEF",s.data[i]) && s.data[i] != '\'') {
                          unsigned h = s.data[i++]; c = c*16+(h <= '9' ? h-'0' : (h|32)-'a'+10); } break;
            case 'u': case 'U': { unsigned n = d == 'u' ? 4 : 8; c = 0;
                          for (unsigned k = 0; k < n && i < s.len; ++k) { unsigned h = s.data[i++]; c = c*16+(h <= '9' ? h-'0' : (h|32)-'a'+10); } break; }
            default:
                if (d >= '0' && d <= '7') {
                    c = d-'0';
                    for (unsigned k = 0; k < 2 && i < s.len && s.data[i] >= '0' && s.data[i] <= '7'; ++k) c = c*8+(s.data[i++]-'0');
                } else c = d;
            }
        }
        if (wide) value = (intmax_t)c;
        else {
            c &= 0xff;
            value = count ? (intmax_t)(((uintmax_t)value << 8) | c) : (intmax_t)c;
        }
        ++count;
    }
    if (!wide && count == 1 && !unsigned_char && value > 127) value -= 256;
    if (!wide && count > 1) value = (intmax_t)(int)(unsigned)value;
    out->v = (uintmax_t)value; out->unsigned_ = false;
    return true;
}

static bool cond_expr(Expr *e, Value *out, bool skip);

static bool primary(Expr *e, Value *out, bool skip) {
    if (e->i >= e->n) return expr_fail(e,"#if with no expression");
    Token *t = &e->t[e->i++];
    if (punct(t,"(")) {
        if (!cond_expr(e,out,skip)) return false;
        if (e->i >= e->n || !punct(&e->t[e->i],")")) return expr_fail(e,"missing ')' in expression");
        ++e->i; return true;
    }
    if (punct(t,"-") || punct(t,"+") || punct(t,"~") || punct(t,"!")) {
        Value v; if (!primary(e,&v,skip)) return false;
        if (punct(t,"-")) v.v = -v.v;
        else if (punct(t,"~")) v.v = ~v.v;
        else if (punct(t,"!")) { v.v = !v.v; v.unsigned_ = false; }
        *out = v; return true;
    }
    if (t->kind == CE_PP_NUMBER) {
        if (!integer_constant(t->text,out)) return skip ? (out->v = 0, true) :
            expr_fail(e,"invalid integer constant in #if");
        return true;
    }
    if (t->kind == CE_PP_CHARACTER) return character_constant(e,t->text,out);
    if (t->kind == CE_PP_IDENTIFIER) { out->v = 0; out->unsigned_ = false; return true; }
    return expr_fail(e,"token is not valid in preprocessor expressions");
}

static int precedence(const Token *t) {
    static const char *const ops[][4] = {
        {"*","/","%",NULL}, {"+","-",NULL}, {"<<",">>",NULL}, {"<",">","<=",">="},
        {"==","!=",NULL}, {"&",NULL}, {"^",NULL}, {"|",NULL}, {"&&",NULL}, {"||",NULL}
    };
    if (t->kind != CE_PP_PUNCTUATOR) return -1;
    for (int level = 0; level < 10; ++level)
        for (int k = 0; k < 4 && ops[level][k]; ++k) if (ce_is(t->text,ops[level][k])) return 10-level;
    return -1;
}

static bool binary(Expr *e, Value *out, int min, bool skip) {
    if (!primary(e,out,skip)) return false;
    for (;;) {
        if (e->i >= e->n) return true;
        Token *op = &e->t[e->i];
        int p = precedence(op);
        if (p < 0 || p < min) return true;
        ++e->i;
        bool and_ = ce_is(op->text,"&&"), or_ = ce_is(op->text,"||");
        Value r;
        bool rskip = skip || (and_ && !out->v) || (or_ && out->v);
        if (!binary(e,&r,p+1,rskip)) return false;
        if (and_) { out->v = out->v && r.v; out->unsigned_ = false; continue; }
        if (or_) { out->v = out->v || r.v; out->unsigned_ = false; continue; }
        bool u = out->unsigned_ || r.unsigned_;
        uintmax_t a = out->v, b = r.v; intmax_t sa = (intmax_t)a, sb = (intmax_t)b;
        const char *s = (const char *)op->text.data; size_t len = op->text.len;
        Value v = {0,u};
        if (len == 1 && s[0] == '*') v.v = u ? a*b : (uintmax_t)(sa*sb);
        else if (len == 1 && (s[0] == '/' || s[0] == '%')) {
            if (!b) { if (!rskip && !skip) return expr_fail(e,"division by zero in #if"); v.v = 0; }
            else if (u) v.v = s[0] == '/' ? a/b : a%b;
            else if (sa == INTMAX_MIN && sb == -1) v.v = s[0] == '/' ? (uintmax_t)INTMAX_MIN : 0;
            else v.v = (uintmax_t)(s[0] == '/' ? sa/sb : sa%sb);
        }
        else if (len == 1 && s[0] == '+') v.v = a+b;
        else if (len == 1 && s[0] == '-') v.v = a-b;
        else if (len == 2 && s[0] == '<' && s[1] == '<') { v.unsigned_ = out->unsigned_; v.v = b >= 64 ? 0 : a << b; }
        else if (len == 2 && s[0] == '>' && s[1] == '>') {
            v.unsigned_ = out->unsigned_;
            v.v = b >= 64 ? (out->unsigned_ || sa >= 0 ? 0 : (uintmax_t)-1) : out->unsigned_ ? a >> b : (uintmax_t)(sa >> b);
        }
        else if (len == 1 && s[0] == '<') { v.v = u ? a < b : sa < sb; v.unsigned_ = false; }
        else if (len == 1 && s[0] == '>') { v.v = u ? a > b : sa > sb; v.unsigned_ = false; }
        else if (len == 2 && s[0] == '<') { v.v = u ? a <= b : sa <= sb; v.unsigned_ = false; }
        else if (len == 2 && s[0] == '>') { v.v = u ? a >= b : sa >= sb; v.unsigned_ = false; }
        else if (len == 2 && s[0] == '=') { v.v = a == b; v.unsigned_ = false; }
        else if (len == 2 && s[0] == '!') { v.v = a != b; v.unsigned_ = false; }
        else if (len == 1 && s[0] == '&') v.v = a & b;
        else if (len == 1 && s[0] == '^') v.v = a ^ b;
        else if (len == 1 && s[0] == '|') v.v = a | b;
        *out = v;
    }
}

static bool cond_expr(Expr *e, Value *out, bool skip) {
    if (!binary(e,out,1,skip)) return false;
    if (e->i < e->n && punct(&e->t[e->i],"?")) {
        ++e->i;
        Value a, b; bool which = out->v != 0;
        if (!cond_expr(e,&a,skip || !which)) return false;
        if (e->i >= e->n || !punct(&e->t[e->i],":")) return expr_fail(e,"'?' without following ':'");
        ++e->i;
        if (!cond_expr(e,&b,skip || which)) return false;
        bool u = a.unsigned_ || b.unsigned_;
        *out = which ? a : b; out->unsigned_ = u;
    }
    if (e->i < e->n && punct(&e->t[e->i],",")) {
        ++e->i;
        return cond_expr(e,out,skip);
    }
    return true;
}

static bool resolve(PP *pp, const char *name, bool angled, bool next, const File *from, char **path, char **spelled,
                    int *found_in, bool *system);

/* The compiler's answer to a __has_* question, asked once per spelling. */
static long ask(PP *pp, const char *question) {
    for (size_t i = 0; i < pp->nanswers; ++i) if (!strcmp(pp->answers[i].text,question)) return pp->answers[i].value;
    long value = 0;
    char path[] = "/tmp/cesta-pp-question-XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) {
        FILE *f = fdopen(fd,"w");
        fprintf(f,"#if %s\n1\n#else\n0\n#endif\n",question); fclose(f);
        char **argv = calloc(pp->nprobe+8,sizeof(*argv)); size_t n = 0;
        argv[n++] = pp->compiler;
        for (size_t i = 0; i < pp->nprobe; ++i) argv[n++] = pp->probe[i];
        argv[n++] = "-E"; argv[n++] = "-P"; argv[n++] = "-x"; argv[n++] = "c"; argv[n++] = path; argv[n] = NULL;
        CeArena scratch = {0}; CeText output = {0}; CeError error = {{0},0};
        if (ce_execute_stdout(argv,&scratch,&output,&error) == 0)
            for (size_t i = 0; i < output.len; ++i) if (output.data[i] >= '0' && output.data[i] <= '9') { value = output.data[i]-'0'; break; }
        ce_arena_free(&scratch); free(argv); unlink(path);
    }
    pp->answers = ce_resize(pp->answers,pp->nanswers+1,sizeof(*pp->answers));
    pp->answers[pp->nanswers++] = (Answer){strdup(question),value};
    return value;
}

static bool header_operand(PP *pp, TokVec *in, Token *first, char **name, bool *angled, const File *file, unsigned line);

/* defined X: a macro, or one of the compiler's own operators when the
   compiler says it is defined. */
static bool is_defined(PP *pp, CeText name) {
    if (lookup(pp,name)) return true;
    if ((name.len > 6 && !memcmp(name.data,"__has_",6)) || (name.len > 5 && !memcmp(name.data,"__is_",5))) {
        char question[160];
        snprintf(question,sizeof(question),"defined(%.*s)",(int)(name.len < 140 ? name.len : 140),name.data);
        return ask(pp,question) != 0;
    }
    return false;
}

/* The spelling of tokens, one space where white space separated them. */
static CeText spelling_of(PP *pp, const Token *t, size_t n) {
    size_t length = 1;
    for (size_t i = 0; i < n; ++i) length += t[i].text.len+1;
    char *s = ce_alloc(&pp->arena,length); size_t k = 0;
    for (size_t i = 0; i < n; ++i) {
        if (i && (t[i].flags & T_SPACE)) s[k++] = ' ';
        memcpy(s+k,t[i].text.data,t[i].text.len); k += t[i].text.len;
    }
    s[k] = 0;
    return (CeText){(unsigned char *)s,k};
}

/* pred or pred(answer): the predicate, and the answer when given. */
static bool assertion_operand(PP *pp, const Token *t, size_t n, size_t *used, CeText *predicate, CeText *answer) {
    if (!n || t[0].kind != CE_PP_IDENTIFIER) return false;
    *predicate = t[0].text; *answer = (CeText){NULL,0}; *used = 1;
    if (n > 1 && punct(&t[1],"(")) {
        size_t depth = 1, k = 2;
        for (; k < n; ++k) {
            if (punct(&t[k],"(")) ++depth;
            else if (punct(&t[k],")") && !--depth) break;
        }
        if (k >= n) return false;
        *answer = spelling_of(pp,t+2,k-2); *used = k+1;
        if (!answer->len) return false;
    }
    return true;
}

/* #if #pred(answer): this file's #assert and #unassert, else the compiler's
   built-in assertions, which the compiler reports. */
static bool asserted(PP *pp, CeText predicate, CeText answer) {
    for (size_t i = pp->nassertions; i; --i) {
        const Assertion *a = &pp->assertions[i-1];
        if (!ce_equal(a->predicate,predicate)) continue;
        if (!a->answer.len && !a->asserted) return false;
        if (!answer.len || ce_equal(a->answer,answer)) { if (a->asserted || answer.len) return a->asserted; }
    }
    char question[512];
    if (answer.len) snprintf(question,sizeof(question),"#%.*s(%.*s)",(int)predicate.len,predicate.data,(int)answer.len,answer.data);
    else snprintf(question,sizeof(question),"#%.*s",(int)predicate.len,predicate.data);
    return ask(pp,question) != 0;
}

static bool try_path(PP *pp, const char *dir, const char *name, char **path);

typedef struct {
    bool found;
    unsigned char *data;
    size_t len;
    const Token *prefix, *suffix, *if_empty;
    size_t nprefix, nsuffix, nif_empty;
} Embedded;

/* The resource of #embed or __has_embed and its parameters (C23 6.10.4,
   with GCC's and Clang's offset): limit, prefix, suffix, if_empty. */
static bool embedded(PP *pp, Frame *f, Token *t, size_t n, unsigned line, Embedded *out) {
    memset(out,0,sizeof(*out));
    TokVec in = {0};
    for (size_t i = n; i > 1; --i) tpush(&in,t[i-1]);
    Token first = t[0]; char *name; bool angled;
    bool ok = header_operand(pp,&in,&first,&name,&angled,f->file,line);
    if (!ok) { free(in.data); return false; }
    TokVec rest = {0};
    while (in.len) tpush(&rest,in.data[--in.len]);
    free(in.data);
    unsigned long long limit = ULLONG_MAX, offset = 0;
    for (size_t i = 0; i < rest.len && ok;) {
        const Token *p = &rest.data[i];
        if (p->kind != CE_PP_IDENTIFIER) { ok = fail(pp,f->file,line,"invalid #embed parameter"); break; }
        CeText vendor = {NULL,0}, key = p->text; size_t at = i+1;
        if (at+2 < rest.len+1 && at < rest.len && punct(&rest.data[at],":") && at+1 < rest.len && punct(&rest.data[at+1],":") &&
            at+2 < rest.len && rest.data[at+2].kind == CE_PP_IDENTIFIER) { vendor = key; key = rest.data[at+2].text; at += 3; }
        size_t open = at, close = at;
        if (open < rest.len && punct(&rest.data[open],"(")) {
            size_t depth = 1;
            for (close = open+1; close < rest.len; ++close) {
                if (punct(&rest.data[close],"(")) ++depth;
                else if (punct(&rest.data[close],")") && !--depth) break;
            }
            if (close >= rest.len) { ok = fail(pp,f->file,line,"unterminated #embed parameter"); break; }
        } else { ok = fail(pp,f->file,line,"#embed parameter requires parentheses"); break; }
        const Token *a = rest.data+open+1; size_t na = close-open-1;
        bool std_ = !vendor.len;
        bool gnu = !pp->clang && (ce_is(vendor,"gnu") || ce_is(vendor,"__gnu__"));
        bool clang_ = pp->clang && (ce_is(vendor,"clang") || ce_is(vendor,"__clang__"));
        if ((std_ && (ce_is(key,"limit") || ce_is(key,"__limit__"))) || ((gnu || clang_) && (ce_is(key,"offset") || ce_is(key,"__offset__")))) {
            Value v; Expr e = {pp,(Token *)a,na,0,f->file,line,true};
            if (!cond_expr(&e,&v,false)) { ok = false; break; }
            if (ce_is(key,"limit") || ce_is(key,"__limit__")) limit = v.v; else offset = v.v;
        } else if (std_ && (ce_is(key,"prefix") || ce_is(key,"__prefix__"))) { out->prefix = a; out->nprefix = na; }
        else if (std_ && (ce_is(key,"suffix") || ce_is(key,"__suffix__"))) { out->suffix = a; out->nsuffix = na; }
        else if (std_ && (ce_is(key,"if_empty") || ce_is(key,"__if_empty__"))) { out->if_empty = a; out->nif_empty = na; }
        else { ok = fail(pp,f->file,line,"unsupported #embed parameter %.*s",(int)key.len,key.data); break; }
        i = close+1;
    }
    if (!ok) { free(rest.data); return false; }
    /* the parameters' tokens stay in the arena with the rest */
    Token *kept = copy_tokens(pp,rest.data,rest.len);
    if (out->prefix) out->prefix = kept+(out->prefix-rest.data);
    if (out->suffix) out->suffix = kept+(out->suffix-rest.data);
    if (out->if_empty) out->if_empty = kept+(out->if_empty-rest.data);
    free(rest.data);
    /* C23 6.10.4: a quoted resource is looked for beside the current file and
       then in the --embed-dir directories, an angled one only in those. */
    char *path = NULL;
    if (name[0] == '/') { if (exists(name)) path = name; }
    else {
        if (!angled && !try_path(pp,f->file->dir,name,&path)) path = NULL;
        for (size_t i = 0; !path && i < pp->nembed_dirs; ++i) if (!try_path(pp,pp->embed_dirs[i],name,&path)) path = NULL;
    }
    if (!path) return true;
    FILE *file = fopen(path,"rb");
    if (!file) return true;
    out->found = true;
    if (offset) fseek(file,(long)offset,SEEK_SET);
    size_t capacity = 4096, len = 0; unsigned char *data = malloc(capacity);
    int c;
    while (len < limit && (c = fgetc(file)) != EOF) {
        if (len == capacity) { capacity *= 2; data = realloc(data,capacity); }
        data[len++] = (unsigned char)c;
    }
    fclose(file);
    out->data = ce_alloc(&pp->arena,len ? len : 1); if (len) memcpy(out->data,data,len); out->len = len;
    free(data);
    return true;
}

static bool embed(PP *pp, Frame *f, Token *t, size_t n, unsigned line) {
    if (!n) return fail(pp,f->file,line,"#embed expects \"FILENAME\" or <FILENAME>");
    Embedded e;
    if (!embedded(pp,f,t,n,line,&e)) return false;
    if (!e.found) return fail(pp,f->file,line,"%s: No such file or directory","#embed resource");
    TokVec out = {0};
    Token at = t[0]; at.flags = T_SPACE; at.hide = NULL; at.expansion = NULL; at.argument = NULL;
    if (!e.len) {
        for (size_t i = 0; i < e.nif_empty; ++i) tpush(&out,e.if_empty[i]);
    } else {
        for (size_t i = 0; i < e.nprefix; ++i) tpush(&out,e.prefix[i]);
        /* GCC writes a long resource as its first byte, a base64 #embed of the
           middle, and its last byte; Clang and short resources are integers. */
        bool base64 = !pp->clang && e.len >= 64;
        for (size_t i = 0; i < e.len; ++i) {
            if (base64 && i == 1) {
                static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                size_t middle = e.len-2, encoded = (middle+2)/3*4;
                char *s = ce_alloc(&pp->arena,encoded+encoded/76*6+64), *w = s; size_t column = 0;
                w += sprintf(w,"#embed \".\" __gnu__::__base64__( \\\n\"");
                for (size_t k = 0; k < middle; k += 3) {
                    unsigned v = (unsigned)e.data[1+k] << 16 | (k+1 < middle ? (unsigned)e.data[2+k] << 8 : 0) | (k+2 < middle ? e.data[3+k] : 0);
                    char quad[4] = {alphabet[v >> 18 & 63], alphabet[v >> 12 & 63], k+1 < middle ? alphabet[v >> 6 & 63] : '=', k+2 < middle ? alphabet[v & 63] : '='};
                    for (int q = 0; q < 4; ++q) {
                        if (column == 76) { w += sprintf(w,"\" \\\n\""); column = 0; }
                        *w++ = quad[q]; ++column;
                    }
                }
                w += sprintf(w,"\")");
                Token d = at; d.kind = PP_PRAGMA; d.text = (CeText){(unsigned char *)s,(size_t)(w-s)};
                tpush(&out,d);
                i = e.len-1;
                Token comma = make_token(pp,CE_PP_PUNCTUATOR,",",&at); comma.flags = 0; tpush(&out,comma);
            }
            char number[8]; snprintf(number,sizeof(number),"%u",e.data[i]);
            Token v = make_token(pp,CE_PP_NUMBER,number,&at); v.flags = i ? 0 : T_SPACE;
            tpush(&out,v);
            if (i+1 < e.len && !(base64 && i == 0 && false)) { Token comma = make_token(pp,CE_PP_PUNCTUATOR,",",&at); comma.flags = 0; tpush(&out,comma); }
        }
        for (size_t i = 0; i < e.nsuffix; ++i) tpush(&out,e.suffix[i]);
    }
    for (size_t i = out.len; i; --i) tpush(&pp->pushback,out.data[i-1]);
    free(out.data);
    return true;
}

/* Prepare an #if line: `defined` and the __has_* operators read their
   operands unreplaced, everything else is macro-replaced, and the
   operators may also come from a replacement (as GCC and Clang allow). */
static bool evaluate(PP *pp, Frame *f, const Token *t, size_t n, unsigned line, bool *value) {
    TokVec stack = {0}, out = {0};
    for (size_t i = n; i; --i) tpush(&stack,t[i-1]);
    Token x;
    while (!pp->failed && input_next(pp,&stack,&x,READ_ANY)) {
        if (is(&x,CE_PP_IDENTIFIER,"defined")) {
            Token a; bool paren = false;
            if (!input_next(pp,&stack,&a,READ_ANY)) { fail(pp,f->file,line,"operator \"defined\" requires an identifier"); break; }
            if (punct(&a,"(")) { paren = true; if (!input_next(pp,&stack,&a,READ_ANY)) { fail(pp,f->file,line,"operator \"defined\" requires an identifier"); break; } }
            if (a.kind != CE_PP_IDENTIFIER) { fail(pp,f->file,line,"operator \"defined\" requires an identifier"); break; }
            if (paren) { Token c; if (!input_next(pp,&stack,&c,READ_ANY) || !punct(&c,")")) { fail(pp,f->file,line,"missing ')' after \"defined\""); break; } }
            Token r = make_token(pp,CE_PP_NUMBER,is_defined(pp,a.text) ? "1" : "0",&x);
            tpush(&out,r); continue;
        }
        if (is(&x,CE_PP_IDENTIFIER,"__has_embed")) {
            Token open;
            if (!input_next(pp,&stack,&open,READ_ANY) || !punct(&open,"(")) { fail(pp,f->file,line,"missing '(' after __has_embed"); break; }
            TokVec operand = {0}; size_t depth = 1; Token y;
            while (input_next(pp,&stack,&y,READ_ANY)) {
                if (punct(&y,"(")) ++depth;
                if (punct(&y,")") && !--depth) break;
                tpush(&operand,y);
            }
            Embedded e; bool okay = operand.len && embedded(pp,f,operand.data,operand.len,line,&e);
            free(operand.data);
            if (!okay) { if (!pp->failed) fail(pp,f->file,line,"invalid __has_embed operand"); break; }
            tpush(&out,make_token(pp,CE_PP_NUMBER,!e.found ? "0" : e.len ? "1" : "2",&x)); continue;
        }
        if (punct(&x,"#") && !pp->clang && stack.len && stack.data[stack.len-1].kind == CE_PP_IDENTIFIER) {
            /* GCC's #pred(answer) */
            Token tokens[512]; size_t k = 0;
            while (k < 512 && stack.len) {
                tokens[k] = stack.data[stack.len-1];
                if (k == 1 && !punct(&tokens[k],"(")) break;
                --stack.len; ++k;
                if (k > 1 && punct(&tokens[k-1],")")) break;
            }
            size_t used; CeText predicate, answer;
            if (!assertion_operand(pp,tokens,k,&used,&predicate,&answer)) { fail(pp,f->file,line,"assertion without predicate"); break; }
            tpush(&out,make_token(pp,CE_PP_NUMBER,asserted(pp,predicate,answer) ? "1" : "0",&x)); continue;
        }
        if (is(&x,CE_PP_IDENTIFIER,"__has_include") || is(&x,CE_PP_IDENTIFIER,"__has_include_next")) {
            bool next = is(&x,CE_PP_IDENTIFIER,"__has_include_next");
            Token open;
            if (!input_next(pp,&stack,&open,READ_ANY) || !punct(&open,"(")) { fail(pp,f->file,line,"missing '(' after __has_include"); break; }
            Token first; char *name; bool angled;
            if (!input_next(pp,&stack,&first,READ_ANY) || !header_operand(pp,&stack,&first,&name,&angled,f->file,line)) { if (!pp->failed) fail(pp,f->file,line,"invalid __has_include operand"); break; }
            Token close;
            if (!input_next(pp,&stack,&close,READ_ANY) || !punct(&close,")")) { fail(pp,f->file,line,"missing ')' after __has_include operand"); break; }
            char *path, *spelled; int found; bool system;
            bool has = resolve(pp,name,angled,next,f->file,&path,&spelled,&found,&system);
            tpush(&out,make_token(pp,CE_PP_NUMBER,has ? "1" : "0",&x)); continue;
        }
        if (x.kind == CE_PP_IDENTIFIER && x.text.len > 6 && !memcmp(x.text.data,"__has_",6) && !lookup(pp,x.text)) {
            /* Every other __has_* operator: the compiler answers. */
            Token open;
            if (!input_next(pp,&stack,&open,READ_ANY) || !punct(&open,"(")) { input_unread(pp,&stack,&open); tpush(&out,make_token(pp,CE_PP_NUMBER,"0",&x)); continue; }
            size_t depth = 1, length = x.text.len+1; TokVec operand = {0}; Token y;
            while (depth && input_next(pp,&stack,&y,READ_ANY)) {
                if (punct(&y,"(")) ++depth;
                if (punct(&y,")") && !--depth) break;
                tpush(&operand,y); length += y.text.len+1;
            }
            char *question = ce_alloc(&pp->arena,length+2); size_t k = 0;
            memcpy(question,x.text.data,x.text.len); k = x.text.len; question[k++] = '(';
            for (size_t i = 0; i < operand.len; ++i) {
                if (i && (operand.data[i].flags & T_SPACE)) question[k++] = ' ';
                memcpy(question+k,operand.data[i].text.data,operand.data[i].text.len); k += operand.data[i].text.len;
            }
            question[k++] = ')'; question[k] = 0;
            free(operand.data);
            char buffer[32]; snprintf(buffer,sizeof(buffer),"%ld",ask(pp,question));
            tpush(&out,make_token(pp,CE_PP_NUMBER,buffer,&x)); continue;
        }
        if (x.kind == CE_PP_IDENTIFIER) {
            int r = expand(pp,&x,&stack);
            if (r < 0) break;
            if (r > 0) continue;
            /* A call of one of the compiler's own operators (Clang's
               __building_module, __is_identifier, ...): the compiler answers. */
            if (x.text.len > 2 && x.text.data[0] == '_' && x.text.data[1] == '_' && stack.len &&
                punct(&stack.data[stack.len-1],"(")) {
                Token open; input_next(pp,&stack,&open,READ_ANY);
                size_t depth = 1, length = x.text.len+1; TokVec operand = {0}; Token y;
                while (depth && input_next(pp,&stack,&y,READ_ANY)) {
                    if (punct(&y,"(")) ++depth;
                    if (punct(&y,")") && !--depth) break;
                    tpush(&operand,y); length += y.text.len+1;
                }
                char *question = ce_alloc(&pp->arena,length+2); size_t k = 0;
                memcpy(question,x.text.data,x.text.len); k = x.text.len; question[k++] = '(';
                for (size_t i = 0; i < operand.len; ++i) {
                    if (i && (operand.data[i].flags & T_SPACE)) question[k++] = ' ';
                    memcpy(question+k,operand.data[i].text.data,operand.data[i].text.len); k += operand.data[i].text.len;
                }
                question[k++] = ')'; question[k] = 0;
                free(operand.data);
                char buffer[32]; snprintf(buffer,sizeof(buffer),"%ld",ask(pp,question));
                tpush(&out,make_token(pp,CE_PP_NUMBER,buffer,&x)); continue;
            }
        }
        if (x.kind == PP_PLACEMARKER) continue;
        tpush(&out,x);
    }
    free(stack.data);
    if (pp->failed) { free(out.data); return false; }
    Expr e = {pp,out.data,out.len,0,f->file,line,true};
    Value v = {0,false};
    bool ok = cond_expr(&e,&v,false);
    if (ok && e.i < e.n) ok = expr_fail(&e,"missing binary operator before token");
    free(out.data);
    *value = v.v != 0;
    return ok;
}

/* ---- Includes -------------------------------------------------------------- */

static bool try_path(PP *pp, const char *dir, const char *name, char **path) {
    size_t n = strlen(dir), m = strlen(name);
    char *p = ce_alloc(&pp->arena,n+m+2);
    memcpy(p,dir,n);
    if (n && p[n-1] != '/') p[n++] = '/';
    memcpy(p+n,name,m+1);
    if (!exists(p)) return false;
    *path = p; return true;
}

/* 6.10.2 with GCC's search order: the including file's directory for
   quoted names, the -iquote directories, then the angle-bracket chain;
   #include_next continues after the directory the current file came from. */
static bool resolve(PP *pp, const char *name, bool angled, bool next, const File *from, char **path, char **spelled,
                    int *found_in, bool *system) {
    *system = false;
    if (name[0] == '/') {
        if (!exists(name)) return false;
        *path = dup_str(pp,name); *spelled = dup_str(pp,name); *found_in = -1; return true;
    }
    size_t start = 0;
    if (next && from && from->found_in >= 0) start = (size_t)from->found_in+1;
    if (!angled && !(next && from && from->found_in >= 0)) {
        const char *dir = from ? from->dir : "";
        if (try_path(pp,dir,name,path)) { *spelled = spell_path(pp,dir,name); *found_in = -1; return true; }
        for (size_t i = 0; i < pp->nquote; ++i)
            if (try_path(pp,pp->quote[i],name,path)) { *spelled = spell_path(pp,pp->quote[i],name); *found_in = -1; return true; }
    }
    for (size_t i = start; i < pp->ndirs; ++i)
        if (try_path(pp,pp->dirs[i],name,path)) {
            *spelled = spell_path(pp,pp->dirs[i],name); *found_in = (int)i; *system = pp->system_dir[i]; return true;
        }
    return false;
}

/* The operand of #include or __has_include: a header-name, or tokens that
   macro replacement turns into one (6.10.2p4). */
static bool header_operand(PP *pp, TokVec *in, Token *first, char **name, bool *angled, const File *file, unsigned line) {
    if (first->kind == CE_PP_HEADER_NAME) {
        *angled = first->text.data[0] == '<';
        *name = dup_text(pp,(CeText){first->text.data+1,first->text.len-2});
        return true;
    }
    TokVec expanded = {0};
    Token t = *first; bool have = true;
    /* replace macros in the operand until it is complete */
    for (;;) {
        if (!have && !input_next(pp,in,&t,READ_ANY)) break;
        have = false;
        if (t.kind == CE_PP_IDENTIFIER) {
            int r = expand(pp,&t,in);
            if (r < 0) { free(expanded.data); return false; }
            if (r > 0) continue;
        }
        if (t.kind == PP_PLACEMARKER) continue;
        tpush(&expanded,t);
        if (expanded.data[0].kind == CE_PP_STRING) break;
        if (punct(&expanded.data[0],"<") && expanded.len > 1 && punct(&t,">")) break;
        if (!punct(&expanded.data[0],"<")) break;
    }
    bool ok = false;
    if (expanded.len && expanded.data[0].kind == CE_PP_STRING && expanded.data[0].text.len >= 2 && expanded.data[0].text.data[0] == '"') {
        *angled = false; *name = dup_text(pp,(CeText){expanded.data[0].text.data+1,expanded.data[0].text.len-2}); ok = true;
    } else if (expanded.len >= 2 && punct(&expanded.data[0],"<") && punct(&expanded.data[expanded.len-1],">")) {
        size_t length = 0;
        for (size_t i = 1; i+1 < expanded.len; ++i) length += expanded.data[i].text.len+1;
        char *s = ce_alloc(&pp->arena,length+1); size_t k = 0;
        for (size_t i = 1; i+1 < expanded.len; ++i) {
            if (i > 1 && (expanded.data[i].flags & T_SPACE)) s[k++] = ' ';
            memcpy(s+k,expanded.data[i].text.data,expanded.data[i].text.len); k += expanded.data[i].text.len;
        }
        s[k] = 0; *angled = true; *name = s; ok = true;
    }
    free(expanded.data);
    if (!ok) fail(pp,file,line,"#include expects \"FILENAME\" or <FILENAME>");
    return ok;
}

static bool push_file(PP *pp, File *file, const char *presumed) {
    if (pp->depth == 200) return fail(pp,file,1,"#include nested depth 200 exceeds maximum");
    if (pp->depth == pp->nframes) { pp->nframes = pp->nframes ? pp->nframes*2 : 16; pp->frames = ce_resize(pp->frames,pp->nframes,sizeof(*pp->frames)); }
    Frame *f = &pp->frames[pp->depth++];
    memset(f,0,sizeof(*f));
    f->file = file; f->presumed = dup_str(pp,presumed);
    if (pp->depth > 1) marker(pp,f->presumed,1,file->system ? " 1 3" : " 1");
    pp->out_file = file; pp->out_line = 1;
    return true;
}

static bool include(PP *pp, Frame *f, const Token *t, size_t n, unsigned line, bool next, bool import) {
    if (!n) return fail(pp,f->file,line,"#include expects \"FILENAME\" or <FILENAME>");
    TokVec in = {0};
    for (size_t i = n; i > 1; --i) tpush(&in,t[i-1]);
    Token first = t[0]; char *name; bool angled;
    bool ok = header_operand(pp,&in,&first,&name,&angled,f->file,line);
    free(in.data);
    if (!ok) return false;
    if (next && f->file->found_in < 0 && pp->depth == 1)
        warn(pp,f->file,line,"#include_next in primary source file");
    char *path, *spelled; int found; bool system;
    if (!resolve(pp,name,angled,next,f->file,&path,&spelled,&found,&system))
        return fail(pp,f->file,line,"%s: No such file or directory",name);
    if (once_seen(pp,path)) return true;
    if (import) {
        char *real = realpath(path,NULL);
        bool seen = false;
        for (File *g = pp->files; g && real && !seen; g = g->next) seen = !strcmp(g->real,real);
        free(real);
        if (seen) return true;
    }
    File *file = load(pp,path,spelled,found,system || f->file->system);
    if (!file) return false;
    if (import) file->once = true;
    return push_file(pp,file,spelled);
}

/* ---- Directives ------------------------------------------------------------ */

static bool skip_group(PP *pp, Frame *f);

static unsigned directive_end_line;
static void line_tokens(PP *pp, Frame *f, TokVec *out, const CeSourceToken **hash, const CeSourceToken **name) {
    CeSourceFile *s = &f->file->src;
    *hash = &s->tokens[f->pos++];
    *name = NULL;
    bool first = true;
    while (f->pos < s->ntokens && s->tokens[f->pos].kind != CE_PP_NEWLINE) {
        const CeSourceToken *t = &s->tokens[f->pos++];
        if (t->kind == CE_PP_MARKER) continue;
        unsigned d = (*hash)->directive;
        if (first && d != CE_DIRECTIVE_NULL && d != CE_DIRECTIVE_NON && d != CE_DIRECTIVE_LINE_MARKER) { *name = t; first = false; continue; }
        first = false;
        tpush(out,from_source(pp,f,t,false));
    }
    directive_end_line = f->pos < s->ntokens ? s->tokens[f->pos].line : (*hash)->line;
    if (f->pos < s->ntokens) ++f->pos;   /* the new-line */
}

static Cond *top_cond(Frame *f) { return f->nconds > f->base ? &f->conds[f->nconds-1] : NULL; }
static void push_cond(Frame *f, unsigned char state) {
    if (f->nconds == f->capacity) { f->capacity = f->capacity ? f->capacity*2 : 16; f->conds = ce_resize(f->conds,f->capacity,sizeof(*f->conds)); }
    f->conds[f->nconds++] = (Cond){state,false};
}

static bool do_pragma(PP *pp, Frame *f, const Token *t, size_t n, const Token *at, bool operator) {
    const File *file = f ? f->file : at->file; unsigned line = at->line;
    (void)operator;
    if (n >= 1 && is(&t[0],CE_PP_IDENTIFIER,"once")) {
        if (f) f->file->once = true;
        return true;
    }
    if (n >= 1 && (is(&t[0],CE_PP_IDENTIFIER,"push_macro") || is(&t[0],CE_PP_IDENTIFIER,"pop_macro"))) {
        if (n < 4 || !punct(&t[1],"(") || t[2].kind != CE_PP_STRING || !punct(&t[3],")")) {
            warn(pp,file,line,"invalid #pragma %.*s",(int)t[0].text.len,t[0].text.data); return true;
        }
        CeText s = t[2].text; size_t b = 0;
        while (b < s.len && s.data[b] != '"') ++b;
        CeText name = {s.data+b+1,s.len-b-2};
        Slot *slot_ = slot(pp,name,true);
        if (is(&t[0],CE_PP_IDENTIFIER,"push_macro")) {
            Pushed *p = ce_alloc(&pp->arena,sizeof(*p)); p->macro = slot_->macro; p->next = slot_->pushed; slot_->pushed = p;
        } else if (slot_->pushed) { slot_->macro = slot_->pushed->macro; slot_->pushed = slot_->pushed->next; }
        return true;
    }
    if (n >= 2 && is(&t[0],CE_PP_IDENTIFIER,"GCC") &&
        (is(&t[1],CE_PP_IDENTIFIER,"poison") || is(&t[1],CE_PP_IDENTIFIER,"system_header") ||
         is(&t[1],CE_PP_IDENTIFIER,"dependency") || is(&t[1],CE_PP_IDENTIFIER,"warning") || is(&t[1],CE_PP_IDENTIFIER,"error"))) {
        if (is(&t[1],CE_PP_IDENTIFIER,"system_header") && f && pp->depth > 1) f->file->system = true;
        if (is(&t[1],CE_PP_IDENTIFIER,"error")) return fail(pp,file,line,"#pragma GCC error");
        return true;
    }
    if (n >= 1 && is(&t[0],CE_PP_IDENTIFIER,"poison")) return true;
    /* every other pragma is the compiler's: it passes through as written */
    size_t length = 8;
    for (size_t i = 0; i < n; ++i) length += t[i].text.len+1;
    char *s = ce_alloc(&pp->arena,length+1); size_t k = 0;
    memcpy(s,"#pragma ",8); k = 8;
    for (size_t i = 0; i < n; ++i) {
        if (i && (t[i].flags & T_SPACE)) s[k++] = ' ';
        memcpy(s+k,t[i].text.data,t[i].text.len); k += t[i].text.len;
    }
    s[k] = 0;
    if (!pp->quiet) {
        Token where = *at; where.kind = CE_PP_OTHER;
        write_line_to(pp,&where);
        write_directive_line(pp,s,k);
    }
    return true;
}

static bool directive(PP *pp, Frame *f) {
    TokVec ops = {0};
    const CeSourceToken *hash, *name;
    unsigned d = f->file->src.tokens[f->pos].directive;
    bool lenient = f->file->src.tokens[f->pos].lenient;
    line_tokens(pp,f,&ops,&hash,&name);
    unsigned line = presumed_line(f,hash->line);
    Token *t = ops.data; size_t n = ops.len;
    bool ok = true, value = false;
    switch (d) {
    case CE_DIRECTIVE_IF: case CE_DIRECTIVE_IFDEF: case CE_DIRECTIVE_IFNDEF:
        if (d == CE_DIRECTIVE_IF) {
            if (!n) { ok = fail(pp,f->file,line,"#if with no expression"); break; }
            ok = evaluate(pp,f,t,n,line,&value);
        } else {
            if (!n || t[0].kind != CE_PP_IDENTIFIER) { ok = fail(pp,f->file,line,"no macro name given in #%s directive",d == CE_DIRECTIVE_IFDEF ? "ifdef" : "ifndef"); break; }
            if (n > 1) warn(pp,f->file,line,"extra tokens at end of #%s directive",d == CE_DIRECTIVE_IFDEF ? "ifdef" : "ifndef");
            value = is_defined(pp,t[0].text) == (d == CE_DIRECTIVE_IFDEF);
        }
        if (!ok) break;
        push_cond(f,value ? C_TAKING : C_WAITING);
        if (!value) ok = skip_group(pp,f);
        break;
    case CE_DIRECTIVE_ELIF: case CE_DIRECTIVE_ELIFDEF: case CE_DIRECTIVE_ELIFNDEF: {
        Cond *c = top_cond(f);
        if (!c) { ok = fail(pp,f->file,line,"#elif without #if"); break; }
        if (c->otherwise) { ok = fail(pp,f->file,line,"#elif after #else"); break; }
        if (c->state != C_WAITING) { c->state = C_DONE; ok = skip_group(pp,f); break; }
        if (d == CE_DIRECTIVE_ELIF) {
            if (!n) { ok = fail(pp,f->file,line,"#elif with no expression"); break; }
            ok = evaluate(pp,f,t,n,line,&value);
        } else {
            if (!n || t[0].kind != CE_PP_IDENTIFIER) { ok = fail(pp,f->file,line,"no macro name given in #elifdef directive"); break; }
            value = is_defined(pp,t[0].text) == (d == CE_DIRECTIVE_ELIFDEF);
        }
        if (!ok) break;
        if (value) c->state = C_TAKING;
        else ok = skip_group(pp,f);
        break;
    }
    case CE_DIRECTIVE_ELSE: {
        Cond *c = top_cond(f);
        if (!c) { ok = fail(pp,f->file,line,"#else without #if"); break; }
        if (c->otherwise) { ok = fail(pp,f->file,line,"#else after #else"); break; }
        if (n) warn(pp,f->file,line,"extra tokens at end of #else directive");
        c->otherwise = true;
        if (c->state == C_WAITING) c->state = C_TAKING;
        else { c->state = C_DONE; ok = skip_group(pp,f); }
        break;
    }
    case CE_DIRECTIVE_ENDIF:
        if (!top_cond(f)) { ok = fail(pp,f->file,line,"#endif without #if"); break; }
        if (n) warn(pp,f->file,line,"extra tokens at end of #endif directive");
        --f->nconds;
        break;
    case CE_DIRECTIVE_DEFINE:
        ok = define(pp,t,n,f->file,hash->line);
        break;
    case CE_DIRECTIVE_UNDEF:
        if (!n || t[0].kind != CE_PP_IDENTIFIER) { ok = fail(pp,f->file,line,"macro names must be identifiers"); break; }
        if (n > 1) warn(pp,f->file,line,"extra tokens at end of #undef directive");
        undefine(pp,t[0].text);
        break;
    case CE_DIRECTIVE_INCLUDE: case CE_DIRECTIVE_INCLUDE_NEXT: case CE_DIRECTIVE_IMPORT:
        ok = include(pp,f,t,n,line,d == CE_DIRECTIVE_INCLUDE_NEXT,d == CE_DIRECTIVE_IMPORT);
        break;
    case CE_DIRECTIVE_LINE: case CE_DIRECTIVE_LINE_MARKER: {
        TokVec in = {t,n,n};
        TokVec x = expand_isolated(pp,&in);
        unsigned long number = 0;
        if (!x.len || x.data[0].kind != CE_PP_NUMBER) { free(x.data); ok = fail(pp,f->file,line,"#line directive requires a positive integer argument"); break; }
        for (size_t i = 0; i < x.data[0].text.len; ++i) number = number*10+(unsigned)(x.data[0].text.data[i]-'0');
        f->delta = (long)number-(long)directive_end_line-1;
        if (x.len > 1 && x.data[1].kind == CE_PP_STRING) {
            CeText s = x.data[1].text;
            f->presumed = dup_text(pp,(CeText){s.data+1,s.len-2});
        }
        free(x.data);
        if (f->pos < f->file->src.ntokens) marker(pp,f->presumed,presumed_line(f,f->file->src.tokens[f->pos].line),"");
        break;
    }
    case CE_DIRECTIVE_ERROR:
        report(pp,f->file,line,"#error");
        break;
    case CE_DIRECTIVE_WARNING:
        warn(pp,f->file,line,"#warning");
        break;
    case CE_DIRECTIVE_PRAGMA: {
        Token at = {.file = f->file, .line = hash->line, .expansion_line = line};
        ok = do_pragma(pp,f,t,n,&at,false);
        break;
    }
    case CE_DIRECTIVE_IDENT: case CE_DIRECTIVE_SCCS:
        if (n && !pp->quiet) {
            char *s = ce_alloc(&pp->arena,t[0].text.len+8);
            memcpy(s,"#ident ",7); memcpy(s+7,t[0].text.data,t[0].text.len); s[7+t[0].text.len] = 0;
            Token at = {.kind = CE_PP_OTHER, .file = f->file, .line = hash->line, .expansion_line = line};
            write_line_to(pp,&at);
            write_directive_line(pp,s,7+t[0].text.len);
        }
        break;
    case CE_DIRECTIVE_ASSERT: case CE_DIRECTIVE_UNASSERT: {
        if (pp->clang) { ok = fail(pp,f->file,line,"invalid preprocessing directive"); break; }
        size_t used; CeText predicate, answer;
        if (!assertion_operand(pp,t,n,&used,&predicate,&answer) || (d == CE_DIRECTIVE_ASSERT && !answer.len)) {
            ok = fail(pp,f->file,line,"assertion without predicate"); break;
        }
        pp->assertions = ce_resize(pp->assertions,pp->nassertions+1,sizeof(*pp->assertions));
        pp->assertions[pp->nassertions++] = (Assertion){ce_text(&pp->arena,predicate.data,predicate.len),
            answer.len ? ce_text(&pp->arena,answer.data,answer.len) : (CeText){NULL,0}, d == CE_DIRECTIVE_ASSERT};
        break;
    }
    case CE_DIRECTIVE_EMBED:
        ok = embed(pp,f,t,n,line);
        break;
    case CE_DIRECTIVE_NULL:
        break;
    case CE_DIRECTIVE_NON:
        ok = fail(pp,f->file,line,"invalid preprocessing directive");
        break;
    }
    (void)lenient; (void)name;
    free(ops.data);
    return ok && !pp->failed;
}

/* Pass over a group not taken, stopping at the #elif, #else or #endif that
   belongs to the current conditional. */
static bool skip_group(PP *pp, Frame *f) {
    (void)pp;
    CeSourceFile *s = &f->file->src;
    size_t depth = 0;
    while (f->pos < s->ntokens) {
        const CeSourceToken *t = &s->tokens[f->pos];
        unsigned d = t->directive;
        if (d == CE_DIRECTIVE_IF || d == CE_DIRECTIVE_IFDEF || d == CE_DIRECTIVE_IFNDEF) ++depth;
        else if (d == CE_DIRECTIVE_ENDIF) { if (!depth) return true; --depth; }
        else if (!depth && (d == CE_DIRECTIVE_ELIF || d == CE_DIRECTIVE_ELIFDEF || d == CE_DIRECTIVE_ELIFNDEF || d == CE_DIRECTIVE_ELSE)) {
            if (top_cond(f)->state == C_WAITING || d == CE_DIRECTIVE_ELSE) return true;
        }
        ++f->pos;
    }
    return true;
}

/* ---- Running --------------------------------------------------------------- */

static bool run_input(PP *pp) {
    Token t; bool space = false;
    while (!pp->failed && next_token(pp,&t,READ_ANY)) {
        if (t.kind == CE_PP_IDENTIFIER) {
            int r = expand(pp,&t,NULL);
            if (r < 0) return false;
            if (r > 0) continue;
        }
        if (t.kind == PP_PLACEMARKER) { space |= (t.flags & T_SPACE) != 0; continue; }
        if (space) { t.flags |= T_SPACE; space = false; }
        write_token(pp,&t);
    }
    return !pp->failed;
}

static bool define_text(PP *pp, const char *text, const char *where) {
    CeArena scratch = {0}; CeSourceFile line; CeError error = {{0},0};
    CeText bytes = ce_cstr(&scratch,text);
    if (!ce_source_scan(pp->grammar,&scratch,bytes,pp->c11,&line,&error)) {
        ce_arena_free(&scratch); return fail(pp,NULL,0,"%s: %s",where,error.message);
    }
    File *file = calloc(1,sizeof(*file));
    file->path = dup_str(pp,where); file->dir = dup_str(pp,""); file->real = dup_str(pp,where); file->found_in = -1;
    file->src = line; file->arena = scratch;
    file->src.bytes = ce_text(&pp->arena,bytes.data,bytes.len);
    file->next = pp->files; pp->files = file;
    bool quiet = pp->quiet; pp->quiet = true;
    bool ok = push_file(pp,file,where) && run_input(pp);
    pp->quiet = quiet;
    return ok;
}

/* ---- The compiler's facts --------------------------------------------------- */

static bool probe_text(PP *pp, char **extra, size_t nextra, bool stderr_, CeText *out, CeArena *arena) {
    char **argv = calloc(pp->nprobe+nextra+2,sizeof(*argv)); size_t n = 0;
    argv[n++] = pp->compiler;
    for (size_t i = 0; i < pp->nprobe; ++i) argv[n++] = pp->probe[i];
    for (size_t i = 0; i < nextra; ++i) argv[n++] = extra[i];
    argv[n] = NULL;
    CeError error = {{0},0};
    int status;
    if (stderr_) {
        int fds[2]; if (pipe(fds)) { free(argv); return false; }
        pid_t pid = fork();
        if (pid == 0) {
            dup2(fds[1],2); close(fds[0]); close(fds[1]);
            int null = open("/dev/null",O_WRONLY); if (null >= 0) { dup2(null,1); close(null); }
            execvp(argv[0],argv); _exit(127);
        }
        close(fds[1]);
        size_t cap = 4096, len = 0; unsigned char *buf = malloc(cap); ssize_t r;
        while ((r = read(fds[0],buf+len,cap-len)) > 0) { len += (size_t)r; if (len == cap) { cap *= 2; buf = realloc(buf,cap); } }
        close(fds[0]); waitpid(pid,&status,0);
        *out = ce_text(arena,buf,len); free(buf);
        status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    } else status = ce_execute_stdout(argv,arena,out,&error);
    free(argv);
    return status == 0;
}

static bool configure(PP *pp) {
    CeArena scratch = {0}; CeText text;
    char *macros[] = {"-dM","-E","-x","c","/dev/null"};
    if (!probe_text(pp,macros,5,false,&text,&scratch)) { ce_arena_free(&scratch); return fail(pp,NULL,0,"the compiler did not report its predefined macros"); }
    pp->clang = false;
    const char *p = (const char *)text.data;
    for (size_t i = 0; i+17 <= text.len; ++i)
        if (!memcmp(p+i,"#define __clang__ ",18) || !memcmp(p+i,"#define __clang__\n",18)) { pp->clang = true; break; }
    for (size_t i = 0; i+28 <= text.len; ++i)
        if (!memcmp(p+i,"#define __CHAR_UNSIGNED__ ",26)) { pp->char_unsigned = true; break; }
    char *defines = ce_alloc(&pp->arena,text.len+1); memcpy(defines,text.data,text.len); defines[text.len] = 0;
    pp->predefined = (CeText){(unsigned char *)defines,text.len};
    char *verbose[] = {"-E","-v","-x","c","/dev/null"};
    CeText paths;
    bool ok = probe_text(pp,verbose,5,true,&paths,&scratch);
    if (ok) {
        char *s = ce_alloc(&pp->arena,paths.len+1); memcpy(s,paths.data,paths.len); s[paths.len] = 0;
        int section = 0;
        for (char *line = strtok(s,"\n"); line; line = strtok(NULL,"\n")) {
            if (strstr(line,"#include \"...\" search starts here")) { section = 1; continue; }
            if (strstr(line,"#include <...> search starts here")) { section = 2; continue; }
            if (!strncmp(line,"End of search list",18)) break;
            if (!section || line[0] != ' ') continue;
            char *dir = line+1;
            char *framework = strstr(dir," (framework directory)");
            if (framework) continue;
            if (section == 1) { pp->quote = ce_resize(pp->quote,pp->nquote+1,sizeof(*pp->quote)); pp->quote[pp->nquote++] = dup_str(pp,dir); }
            else {
                pp->dirs = ce_resize(pp->dirs,pp->ndirs+1,sizeof(*pp->dirs));
                pp->system_dir = ce_resize(pp->system_dir,pp->ndirs+1,sizeof(*pp->system_dir));
                pp->system_dir[pp->ndirs] = dir[0] == '/' && (strstr(dir,"/include") != NULL);
                pp->dirs[pp->ndirs++] = dup_str(pp,dir);
            }
        }
    }
    ce_arena_free(&scratch);
    if (!ok) return fail(pp,NULL,0,"the compiler did not report its include directories");
    static const struct { const char *name; unsigned kind; } builtins[] = {
        {"__FILE__",B_FILE}, {"__LINE__",B_LINE}, {"__COUNTER__",B_COUNTER}, {"__INCLUDE_LEVEL__",B_INCLUDE_LEVEL},
        {"__BASE_FILE__",B_BASE_FILE}, {"__FILE_NAME__",B_FILE_NAME}, {"__DATE__",B_DATE}, {"__TIME__",B_TIME},
        {"__TIMESTAMP__",B_TIMESTAMP}, {"_Pragma",B_PRAGMA}
    };
    for (size_t i = 0; i < sizeof(builtins)/sizeof(*builtins); ++i) builtin(pp,builtins[i].name,builtins[i].kind);
    return define_text(pp,defines,"<built-in>");
}

/* ---- The command ----------------------------------------------------------- */

static bool takes_argument(const char *a) {
    static const char *const options[] = {"-I","-D","-U","-include","-imacros","-isystem","-iquote","-idirafter",
        "-o","-MF","-MT","-MQ","-x","-iprefix","-iwithprefix","-iwithprefixbefore","-isysroot","--sysroot","-target",
        "-dumpbase","-dumpbase-ext","-dumpdir",NULL};
    for (size_t i = 0; options[i]; ++i) if (!strcmp(a,options[i])) return true;
    return false;
}

/* Translation phase 4 into memory: the text, and each token's origin. A
   compiler error (one GCC and Clang report and go on from) leaves the text
   complete; error->message holds the first, and the result is false. */
static bool preprocess(CeText cf, int argc, char **argv, const CeText *supplied,
                        bool markers, CePPResult *out, CeError *error) {
    memset(out,0,sizeof(*out));
    PP pp = {0}; CeGrammar g = {0};
    pp.error = error; pp.markers = markers; pp.compiler = "cc";
    const char *epoch = getenv("SOURCE_DATE_EPOCH");
    pp.epoch = epoch ? (time_t)strtoll(epoch,NULL,10) : time(NULL);
    char **defines = calloc((size_t)argc+1,sizeof(*defines)); size_t ndefines = 0;
    char **includes = calloc((size_t)argc+1,sizeof(*includes)); size_t nincludes = 0;
    pp.probe = calloc((size_t)argc+1,sizeof(*pp.probe));
    const char *source = NULL; bool ok = false;
    char *text = NULL; size_t text_len = 0;
    pp.out = open_memstream(&text,&text_len);
    if (!pp.out || !defines || !includes || !pp.probe) { ce_error(error,0,"out of memory"); goto done; }
    for (int i = 0; i < argc; ++i) {
        char *a = argv[i];
        if (!strcmp(a,"--cc") && i+1 < argc) { pp.compiler = argv[++i]; continue; }
        if (!strcmp(a,"-E") || !strcmp(a,"-c") || !strcmp(a,"-S") || !strcmp(a,"-P")) continue;
        if (!strcmp(a,"-o") && i+1 < argc) { ++i; continue; }
        if (a[0] != '-' && !source) { source = a; continue; }
        if ((!strncmp(a,"-D",2) || !strncmp(a,"-U",2)) && !strcmp(a+2,"") && i+1 < argc) {
            char *both = ce_alloc(&pp.arena,strlen(argv[i+1])+3); both[0] = '-'; both[1] = a[1]; strcpy(both+2,argv[i+1]);
            defines[ndefines++] = both; ++i; continue;
        }
        if (!strncmp(a,"-D",2) || !strncmp(a,"-U",2)) { defines[ndefines++] = a; continue; }
        if ((!strcmp(a,"-include") || !strcmp(a,"-imacros")) && i+1 < argc) {
            includes[nincludes++] = a; includes[nincludes++] = argv[++i]; continue;
        }
        if (!strncmp(a,"-std=",5)) pp.c11 = !strncmp(a+5,"c",1) || !strncmp(a+5,"iso",3);
        if (!strncmp(a,"-M",2)) { if (takes_argument(a) && i+1 < argc) ++i; continue; }
        if (!strncmp(a,"--embed-dir=",12) || !strncmp(a,"-embed-dir=",11)) {
            pp.embed_dirs = ce_resize(pp.embed_dirs,pp.nembed_dirs+1,sizeof(*pp.embed_dirs));
            pp.embed_dirs[pp.nembed_dirs++] = strchr(a,'=')+1;
        }
        pp.probe[pp.nprobe++] = a;
        if (takes_argument(a) && i+1 < argc) pp.probe[pp.nprobe++] = argv[++i];
    }
    if (!source) { ce_error(error,0,"no source file"); goto done; }
    if (!ce_source_grammar(&g,cf,error)) goto done;
    pp.grammar = &g;
    if (!configure(&pp)) goto done;
    for (size_t i = 0; i < ndefines; ++i) {
        const char *d = defines[i]+2; char *line;
        if (defines[i][1] == 'U') { line = ce_alloc(&pp.arena,strlen(d)+10); sprintf(line,"#undef %s\n",d); }
        else {
            const char *eq = strchr(d,'=');
            line = ce_alloc(&pp.arena,strlen(d)+16);
            if (eq) sprintf(line,"#define %.*s %s\n",(int)(eq-d),d,eq+1);
            else sprintf(line,"#define %s 1\n",d);
        }
        if (!define_text(&pp,line,"<command-line>")) goto done;
    }
    pp.base_file = dup_str(&pp,source);
    File *main_file = supplied ? load_source(&pp,source,*supplied) : load(&pp,source,source,-1,false);
    if (!main_file) { if (!error->message[0]) ce_error(error,0,"%s: %s",source,strerror(errno)); goto done; }
    if (supplied) { pp.supplied_identity = main_file->real; pp.supplied_source = *supplied; }
    if (pp.clang && !strchr(source,'/')) main_file->dir = dup_str(&pp,"");
    pp.out_bol = true;
    if (!push_file(&pp,main_file,source)) goto done;
    marker(&pp,source,1,"");
    pp.out_line = 1;
    /* -imacros and -include files: as if included before the first line,
       searched from the working directory first (GCC's rule). */
    for (size_t i = 0; i < nincludes; i += 2) {
        File *f = NULL; char *path, *spelled; int found; bool system;
        File here = {.dir = "", .found_in = -1};
        if (!resolve(&pp,includes[i+1],false,false,&here,&path,&spelled,&found,&system)) { fail(&pp,NULL,0,"%s: No such file or directory",includes[i+1]); goto done; }
        f = load(&pp,path,spelled,found,system);
        if (!f) goto done;
        bool quiet = pp.quiet; if (!strcmp(includes[i],"-imacros")) pp.quiet = true;
        pp.stop_depth = 1;
        if (!push_file(&pp,f,spelled) || !run_input(&pp)) { if (pp.failed) goto done; }
        pp.stop_depth = 0;
        pp.quiet = quiet;
    }
    if (!run_input(&pp)) goto done;
    if (!pp.out_bol) emit_char(&pp,'\n');
    if (pp.errors && !error->message[0]) ce_error(error,0,"%u preprocessing error%s",pp.errors,pp.errors == 1 ? "" : "s");
    ok = !pp.failed && !pp.errors;
done:
    if (pp.out) fclose(pp.out);
    for (File *f = pp.files; f; f = f->next) if (f->real && f->path[0] != '<') {
        const char *dependency = f->path;
        while (dependency[0] == '.' && dependency[1] == '/') dependency += 2;
        bool duplicate = false;
        for (size_t i = 0; i < out->ndependencies; ++i)
            if (!strcmp(out->dependencies[i].file,dependency)) {
                out->dependencies[i].system &= f->system; duplicate = true; break;
            }
        if (!duplicate) {
            out->dependencies = ce_resize(out->dependencies,out->ndependencies+1,sizeof(*out->dependencies));
            out->dependencies[out->ndependencies++] = (CePPDependency){dependency,f->system};
        }
    }
    for (File *f = pp.files, *next; f; f = next) { next = f->next; ce_source_free(&f->src); ce_arena_free(&f->arena); free(f); }
    for (size_t i = 0; i < pp.nanswers; ++i) free(pp.answers[i].text);
    free(pp.embed_dirs); free(pp.assertions); free(pp.answers); free(pp.table); free(pp.frames); free(pp.pushback.data); free(pp.quote); free(pp.dirs); free(pp.system_dir);
    free(defines); free(includes); free(pp.probe);
    ce_grammar_free(&g);
    ce_grammar_free(&pp.identifier_grammar);
    /* The result keeps the arena that holds the file and macro names. */
    out->arena = pp.arena;
    out->text = ce_text(&out->arena,text ? (unsigned char *)text : (unsigned char *)"",text ? text_len : 0);
    free(text);
    out->origins = pp.origins; out->len = pp.norigins; out->clang = pp.clang;
    out->predefined = pp.predefined;
    return ok;
}

bool ce_pp_run(CeText cf, int argc, char **argv, bool markers, CePPResult *out, CeError *error) {
    return preprocess(cf,argc,argv,NULL,markers,out,error);
}

bool ce_pp_run_source(CeText cf, int argc, char **argv, CeText source,
                      bool markers, CePPResult *out, CeError *error) {
    return preprocess(cf,argc,argv,&source,markers,out,error);
}

void ce_pp_result_free(CePPResult *r) {
    free(r->dependencies); free(r->origins); ce_arena_free(&r->arena); memset(r,0,sizeof(*r));
}

const CePPOrigin *ce_pp_origin(const CePPResult *r, size_t offset) {
    size_t lo = 0, hi = r->len;
    while (lo < hi) {
        size_t mid = lo+(hi-lo)/2;
        if (r->origins[mid].offset+r->origins[mid].length <= offset) lo = mid+1;
        else hi = mid;
    }
    return lo < r->len && r->origins[lo].offset <= offset ? &r->origins[lo] : NULL;
}

bool ce_pp_diagnostic(FILE *file, const CePPResult *r, const CeError *error) {
    const CePPOrigin *o = ce_pp_origin(r,error->offset);
    if (!o) {
        size_t lo = 0, hi = r->len;
        while (lo < hi) {
            size_t mid = lo+(hi-lo)/2;
            if (r->origins[mid].offset <= error->offset) lo = mid+1;
            else hi = mid;
        }
        if (lo) o = &r->origins[lo-1];
    }
    if (!o) return false;
    fprintf(file,"%s:%u:%u: %s\n",o->file,o->line,o->column,error->message);
    const CePPExpansion *chain = o->expansion ? o->expansion : o->argument;
    for (const CePPExpansion *x = chain; x; x = x->parent)
        fprintf(file,"%s:%u:%u: note: in expansion of %.*s (defined at %s:%u)\n",
                x->file,x->line,x->column,(int)x->macro.len,x->macro.data,x->definition_file,x->definition_line);
    return true;
}

/* One line per token of the output: its offset and length there, where it
   was spelled, and the expansions that produced it, innermost first. */
static bool write_provenance(const CePPResult *r, const char *path) {
    FILE *f = fopen(path,"w");
    if (!f) return false;
    for (size_t i = 0; i < r->len; ++i) {
        const CePPOrigin *o = &r->origins[i];
        fprintf(f,"%zu %zu ",o->offset,o->length);
        ce_quote(f,(CeText){(unsigned char *)o->file,strlen(o->file)});
        fprintf(f," %u %u",o->line,o->column);
        if (o->pasted) fputs(" ##",f);
        for (const CePPExpansion *e = o->expansion; e; e = e->parent) {
            fputc(' ',f); ce_quote(f,e->macro); fputc(' ',f);
            ce_quote(f,(CeText){(unsigned char *)e->file,strlen(e->file)});
            fprintf(f," %u %u",e->line,e->column);
        }
        if (o->argument) {
            fputs(" ;",f);
            for (const CePPExpansion *e = o->argument; e; e = e->parent) {
                fputc(' ',f); ce_quote(f,e->macro); fputc(' ',f);
                ce_quote(f,(CeText){(unsigned char *)e->file,strlen(e->file)});
                fprintf(f," %u %u",e->line,e->column);
            }
        }
        fputc('\n',f);
    }
    return fclose(f) == 0;
}

int ce_pp_command(int argc, char **argv, CeText cf) {
    const char *output = NULL, *provenance = NULL; bool markers = true;
    char **flags = calloc((size_t)argc+1,sizeof(*flags)); int n = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i],"-P")) { markers = false; continue; }
        if (!strcmp(argv[i],"-o") && i+1 < argc) { output = argv[++i]; continue; }
        if (!strcmp(argv[i],"--provenance") && i+1 < argc) { provenance = argv[++i]; continue; }
        flags[n++] = argv[i];
    }
    CePPResult result; CeError error = {{0},0};
    bool ok = ce_pp_run(cf,n,flags,markers,&result,&error);
    int status = ok ? 0 : 1;
    if (!result.text.len && !ok) {
        fprintf(stderr,"cesta pp: %s\n",error.message[0] ? error.message : "preprocessing failed");
    } else {
        FILE *out = output ? fopen(output,"w") : stdout;
        if (!out) { fprintf(stderr,"cesta pp: %s: %s\n",output,strerror(errno)); status = 1; }
        else {
            fwrite(result.text.data,1,result.text.len,out);
            if (out != stdout) fclose(out);
        }
        if (!ok && error.message[0]) fprintf(stderr,"cesta pp: %s\n",error.message);
        if (provenance && !write_provenance(&result,provenance)) {
            fprintf(stderr,"cesta pp: %s: %s\n",provenance,strerror(errno)); status = 1;
        }
    }
    ce_pp_result_free(&result); free(flags);
    return status;
}

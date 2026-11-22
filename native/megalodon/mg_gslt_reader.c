/*
 * The reader around Megalodon's syntax presentations compiled to C (see
 * mg_gslt_reader.h).  Included by the reader's instance (mg_reader_instance.c)
 * after the rule runtime, the atoms component and the generated module, whose
 * values (T, rulec's atoms representation of sort Term) and views it reads.
 * The source is the run's input of the atoms representation: a file is read
 * through cursors over its bytes, and a cursor's position is where the
 * reading is.
 */
#include "mg_gslt_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a value of the module's one data sort, Term (sort 0), through its views */
static inline int tag(const T *x) { return RC_TAG_atoms_0((T *)x); }
static inline T *KID(const T *x, int i) { return RC_KID_atoms_0((T *)x, i); }
static void read_kids(const T *x, T **k) {
    for (int i = 0; i < rc_ctor_arity[tag(x)] && i < 8; i++)
        k[i] = KID(x, i);
}

/* the program's constructor named as an atom spells it: OP applied to ARITY arguments, or the
 * symbol OP; -1 when the program has none */
static int op_of(const char *name, uint32_t arity) {
    char spelled[256];
    if (arity) snprintf(spelled, sizeof spelled, "(%s %u)", name, arity);
    else snprintf(spelled, sizeof spelled, "%s", name);
    for (int c = 0; c < rc_nctors; c++)
        if (!rc_ctor_judgment[c] && strcmp(rc_ctor_name[c], spelled) == 0)
            return c;
    return -1;
}
static int judgment_of(const char *mode) {
    for (int c = 0; c < rc_nctors; c++)
        if (rc_ctor_judgment[c] && strcmp(rc_ctor_name[c], mode) == 0)
            return c;
    return -1;
}

/* V an application of OP (a constructor of N children; -1 when the program has none) */
static bool is(const T *v, int op, uint32_t n) { return op >= 0 && tag(v) == op && rc_ctor_arity[op] == n; }

/* the reader's work, counted in a build with RC_COUNT */
#ifdef RC_COUNT
static RC_HEAP_LOCAL uint64_t rule_counts[RC_NRULES], fail_counts[RC_NCTORS];
RC_HEAP_LOCAL uint64_t *rc_rule_count, *rc_fail_count;
static void count_init(void) { rc_rule_count = rule_counts; rc_fail_count = fail_counts; }
uint64_t mg_gslt_reader_work(void) {
    uint64_t n = 0;
    for (int i = 0; i < RC_NRULES; i++) n += rule_counts[i];
    for (int i = 0; i < RC_NCTORS; i++) n += fail_counts[i];
    return n;
}
#else
static void count_init(void) {}
uint64_t mg_gslt_reader_work(void) { return 0; }
#endif

/* the trace: each step's derivation into PREFIX.R:BITS */
static RC_HEAP_LOCAL char trace_prefix[1024];
static RC_HEAP_LOCAL FILE *trace_file;
static void reader_trace_sink(const uint16_t *ev, size_t n, void *ctx) {
    (void)ctx;
    if (trace_file) fwrite(ev, sizeof *ev, n, trace_file);
}
bool mg_gslt_reader_trace(const char *prefix) {
    snprintf(trace_prefix, sizeof trace_prefix, "%s", prefix ? prefix : "");
    return true;
}

static bool ready;
static void reader_init(void) {
    if (ready) return;
    rc_components_init();
    ready = true;
}

/* One step of the presentations: JUDGMENT (a semidet entry) on ARGS, its answer or NULL.  Traced
 * into the entry's file when a trace is asked. */
static T *step(int judgment, T **args) {
    count_init();
    if (!trace_prefix[0])
        return rc_call(judgment, args);
    char path[1200];
    snprintf(path, sizeof path, "%s.%s", trace_prefix, rc_ctor_name[judgment]);
    trace_file = fopen(path, "ab");
    if (!trace_file)
        return rc_call(judgment, args);
    rc_trace_set_sink(reader_trace_sink, NULL);
    rc_trace_start(1u << 16);
    T *r = rc_call(judgment, args);
    rc_trace_stop();
    fclose(trace_file);
    trace_file = NULL;
    return r;
}

/* ── readings handed to callbacks ── */

void mg_gslt_node(const MgGsltValue *value, MgGsltNode *out) {
    const T *v = (const T *)value;
    int64_t i;
    *out = (MgGsltNode){0};
    if (rc_atoms_is_cursor(v)) {   /* read as its first cell, (cons (cp N) rest) */
        out->kind = MG_GSLT_UNREAD;
        out->name = "cons";
        out->len = 4;
        out->arity = 2;
        return;
    }
    if (rc_atoms_int_of(v, (uint16_t)op_of("other integer", 0), &i)) { out->kind = MG_GSLT_INT; out->integer = i; return; }
    if (RC_IS_HEAP(v) && (v->flags & RC_FLAG_BYTES)) {
        out->kind = v->tag == op_of("other string", 0) ? MG_GSLT_STRING : MG_GSLT_SYMBOL;
        out->name = (const char *)rc_bytes(v);
        out->len = v->n;
        return;
    }
    const char *name = rc_ctor_name[v->tag];
    if (v->tag == op_of("other expression", 0)) {
        out->kind = MG_GSLT_APPLY;
        out->name = "";
        out->arity = v->n;
        return;
    }
    if (name[0] != '(') {
        out->kind = MG_GSLT_SYMBOL;
        out->name = name;
        out->len = strlen(name);
        return;
    }
    out->kind = MG_GSLT_APPLY;
    out->name = name + 1;
    out->len = (size_t)(strrchr(name, ' ') - name - 1);
    out->arity = rc_ctor_arity[v->tag];
}

const MgGsltValue *mg_gslt_child(const MgGsltValue *value, size_t i) {
    const T *v = (const T *)value;
    return (const MgGsltValue *)(v->tag == op_of("other expression", 0) ? v->k[i] : KID(v, (int)i));
}

char *mg_gslt_file_bytes(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    size_t cap = 1u << 16, n = 0;
    char *bytes = malloc(cap + 1u);
    for (size_t got; bytes && (got = fread(bytes + n, 1, cap - n, f)) > 0;) {
        n += got;
        if (n == cap) {
            char *grown = realloc(bytes, 2u * cap + 1u);
            if (!grown) {
                free(bytes);
                bytes = NULL;
                break;
            }
            bytes = grown;
            cap *= 2u;
        }
    }
    bool failed = ferror(f) != 0;
    fclose(f);
    if (!bytes || failed) {
        free(bytes);
        return NULL;
    }
    bytes[n] = '\0';
    *len = n;
    return bytes;
}

MgGsltReadStatus mg_gslt_read_tokens(const char *bytes, size_t len, MgGsltEmit emit, void *ctx) {
    reader_init();
    int end = op_of("MgTokensEndV1", 0), read = op_of("MgTokenReadV1", 2), scal = op_of("MgScalarsV1", 1),
        newline = op_of("MgAfterNewlineV1", 2), comment = op_of("MgAfterCommentV1", 3), lex_fail = op_of("MgLexFailV1", 1),
        next = judgment_of("MgNextTokenV1:10");
    RcHeap heap = {0};
    rc_heap_swap(&heap);
    RcAtomsSource outer = rc_atoms_source;
    rc_atoms_set_source((const uint8_t *)bytes, len);
    MgGsltReadStatus status;
    T *cur = rc_atoms_cursor(0);
    for (;;) {
        T *r = step(next, &cur);
        if (!r) { status = MG_GSLT_READ_REFUSED; break; }
        /* the newlines and doc comments before the token count only for positions */
        while (tag(r) == newline || tag(r) == comment)
            r = KID(r, tag(r) == newline ? 1 : 2);
        if (tag(r) == end) { status = MG_GSLT_READ_END; break; }
        if (tag(r) == lex_fail) { status = MG_GSLT_READ_ERROR; break; }
        if (tag(r) != read || tag(KID(r, 1)) != scal) { status = MG_GSLT_READ_MALFORMED; break; }
        emit(ctx, (const MgGsltValue *)KID(r, 0));
        /* the step's values end here; the rest of the input is a position */
        T *rest = KID(KID(r, 1), 0);
        if (!rc_atoms_is_cursor(rest) && tag(rest) != op_of("nil", 0)) { status = MG_GSLT_READ_MALFORMED; break; }
        cur = rest;
        rc_roots[0] = cur;
        rc_safepoint(1);
        cur = rc_roots[0];
    }
    rc_atoms_source = outer;
    rc_heap_swap(&heap);
    rc_heap_free(&heap);
    return status;
}

/* ── Megalodon documents for the evaluator (mg_source.h) ──────────────────
 * The presentations' items made into the evaluator's parse trees, with
 * Megalodon's positions.  lexer.mll counts the bytes of the current line; a
 * newline starts the next line; a doc comment is counted by update_pos
 * (each newline in it starts a line, and the char after it is the comment's
 * length from its last newline on).  A read's stream keeps the newlines and
 * doc comments it passed (MgEvNewlineV1, MgEvCommentV1, newest first); each
 * item folds them in, and the kept state starts afresh. */

typedef struct MgReaderChunk MgReaderChunk;
struct MgReaderChunk {
    MgReaderChunk *next;
    size_t used, size;
    max_align_t data[];
};

enum { CAT_NONE = 0, CAT_L, CAT_DOC, CAT_TAC };

typedef struct {
    const char *name;
    uint32_t arity;
    uint8_t cat;
    int kind;
} MgOpKind;

static const MgOpKind op_kinds[] = {
    {"MgByteLV1", 1, CAT_L, MG_L_BYTE}, {"MgStringLV1", 1, CAT_L, MG_L_STRING},
    {"MgQStringLV1", 1, CAT_L, MG_L_QSTRING}, {"MgNaLV1", 1, CAT_L, MG_L_NAME},
    {"MgNuLV1", 4, CAT_L, MG_L_NUM}, {"MgLeLV1", 4, CAT_L, MG_L_LET},
    {"MgLeMLV1", 4, CAT_L, MG_L_LETM}, {"MgBiLV1", 4, CAT_L, MG_L_BINDER},
    {"MgPreoLV1", 2, CAT_L, MG_L_PREFIX}, {"MgPostoLV1", 2, CAT_L, MG_L_POSTFIX},
    {"MgInfoLV1", 3, CAT_L, MG_L_INFIX}, {"MgImplopLV1", 2, CAT_L, MG_L_IMPLICIT},
    {"MgSepLV1", 4, CAT_L, MG_L_SEP}, {"MgRepLV1", 4, CAT_L, MG_L_REP},
    {"MgSepRepLV1", 5, CAT_L, MG_L_SEPREP}, {"MgSetEnumLV1", 1, CAT_L, MG_L_SETENUM},
    {"MgMTupleLV1", 2, CAT_L, MG_L_MTUPLE}, {"MgParenLV1", 2, CAT_L, MG_L_PAREN},
    {"MgIfThenElseLV1", 3, CAT_L, MG_L_IF},
    {"MgAuthorV1", 2, CAT_DOC, MG_DOC_AUTHOR}, {"MgTitleV1", 1, CAT_DOC, MG_DOC_TITLE},
    {"MgSaltV1", 1, CAT_DOC, MG_DOC_SALT}, {"MgOpaqueV1", 1, CAT_DOC, MG_DOC_OPAQUE},
    {"MgTransparentV1", 1, CAT_DOC, MG_DOC_TRANSPARENT}, {"MgTreasureV1", 1, CAT_DOC, MG_DOC_TREASURE},
    {"MgShowProofTermsV1", 1, CAT_DOC, MG_DOC_SHOWPROOFTERMS}, {"MgSectionV1", 1, CAT_DOC, MG_DOC_SECTION},
    {"MgEndV1", 1, CAT_DOC, MG_DOC_END}, {"MgVarDeclV1", 3, CAT_DOC, MG_DOC_VAR},
    {"MgLetDeclV1", 3, CAT_DOC, MG_DOC_LET}, {"MgHypDeclV1", 2, CAT_DOC, MG_DOC_HYP},
    {"MgPostInfixDeclV1", 4, CAT_DOC, MG_DOC_POSTINFIX}, {"MgPrefixDeclV1", 3, CAT_DOC, MG_DOC_PREFIX},
    {"MgBinderDeclV1", 5, CAT_DOC, MG_DOC_BINDER}, {"MgNotationDeclV1", 2, CAT_DOC, MG_DOC_NOTATION},
    {"MgUnicodeDeclV1", 2, CAT_DOC, MG_DOC_UNICODE}, {"MgSubscriptDeclV1", 1, CAT_DOC, MG_DOC_SUBSCRIPT},
    {"MgSuperscriptDeclV1", 1, CAT_DOC, MG_DOC_SUPERSCRIPT}, {"MgParamDeclV1", 2, CAT_DOC, MG_DOC_PARAM},
    {"MgDefDeclV1", 3, CAT_DOC, MG_DOC_DEF}, {"MgParamHashV1", 3, CAT_DOC, MG_DOC_PARAMHASH},
    {"MgAxDeclV1", 2, CAT_DOC, MG_DOC_AXIOM}, {"MgThmDeclV1", 3, CAT_DOC, MG_DOC_THEOREM},
    {"MgPfStructV1", 1, CAT_TAC, MG_TAC_STRUCT}, {"MgExactV1", 1, CAT_TAC, MG_TAC_EXACT},
    {"MgLetTacV1", 2, CAT_TAC, MG_TAC_LET}, {"MgAssumeTacV1", 2, CAT_TAC, MG_TAC_ASSUME},
    {"MgSetTacV1", 3, CAT_TAC, MG_TAC_SET}, {"MgApplyTacV1", 1, CAT_TAC, MG_TAC_APPLY},
    {"MgClaimTacV1", 2, CAT_TAC, MG_TAC_CLAIM}, {"MgProveTacV1", 2, CAT_TAC, MG_TAC_PROVE},
    {"MgCasesTacV1", 2, CAT_TAC, MG_TAC_CASES}, {"MgWitnessTacV1", 1, CAT_TAC, MG_TAC_WITNESS},
    {"MgRewriteTacV1", 3, CAT_TAC, MG_TAC_REWRITE}, {"MgSpecialTacV1", 2, CAT_TAC, MG_TAC_SPECIAL},
    {"MgQedV1", 0, CAT_TAC, MG_TAC_QED}, {"MgAdmittedV1", 0, CAT_TAC, MG_TAC_ADMITTED},
    {"MgAdmitV1", 0, CAT_TAC, MG_TAC_ADMIT}, {"MgAbyV1", 1, CAT_TAC, MG_TAC_ABY},
};

/* The other constructors an item is made of. */
#define MG_READER_OPS(X) \
    X(cp_cons, "MgCpConsV1", 2) X(cp_nil, "MgCpNilV1", 0) X(list_cons, "MgListConsV1", 2) \
    X(list_nil, "MgListNilV1", 0) X(some, "MgSomeV1", 1) X(none, "MgNoneV1", 0) X(yes, "MgTrueV1", 0) \
    X(no, "MgFalseV1", 0) X(pair, "MgPairV1", 2) X(num, "MgIntV1", 1) X(doc, "MgDocV1", 1) \
    X(pftac, "MgPfTacV1", 1) X(item, "MgItemV1", 2) X(reader_end, "MgReaderEndV1", 2) \
    X(reader, "MgReaderV1", 3) X(stream, "MgStreamV1", 2) X(pushed, "MgPushedV1", 2) \
    X(ev_nil, "MgEvNilV1", 0) X(ev_newline, "MgEvNewlineV1", 2) X(ev_comment, "MgEvCommentV1", 3) \
    X(reader_error, "MgReaderErrorV1", 1) X(syntax_error, "MgSyntaxErrorV1", 3) X(failure, "MgFailureV1", 2) \
    X(item_start, "MgItemStartV1", 0) X(lex_fail_at, "MgLexFailAtV1", 2) \
    X(inf_nam, "MgInfNamV1", 1) X(inf_set, "MgInfSetV1", 1) X(inf_mem, "MgInfMemV1", 0) \
    X(inf_subq, "MgInfSubqV1", 0) X(asc_tp, "MgAscTpV1", 0) X(asc_set, "MgAscSetV1", 0) \
    X(asc_subeq, "MgAscSubeqV1", 0) X(postfix, "MgPostfixV1", 0) X(infix_none, "MgInfixNoneV1", 0) \
    X(infix_left, "MgInfixLeftV1", 0) X(infix_right, "MgInfixRightV1", 0) X(cp, "cp", 1) \
    X(integer, "other integer", 0) X(nil, "nil", 0)

typedef struct {
#define MG_READER_FIELD(f, name, arity) int f;
    MG_READER_OPS(MG_READER_FIELD)
#undef MG_READER_FIELD
} MgReaderOps;

struct MgSource {
    RcHeap heap;                /* the reading's values */
    MgReaderOps op;
    int start, next_file, read_item;   /* the entries */
    uint16_t *kind_of;          /* constructor id -> op_kinds index + 1 */
    const uint8_t *bytes;       /* the current file */
    size_t len;
    T *state;                   /* the kept reader state (MgReaderV1), or */
    bool ended;                 /* the kept MgReaderEndV1 */
    bool finished;              /* refused, or out of memory */
    int lineno, charno;         /* lexer.mll's position, at byte pos */
    size_t pos;
    MgReaderChunk *arena;       /* the current item's trees */
    bool oom;
    MgGsltReadStatus status;    /* why the reading finished */
    MgSourceItem item;
    char error[512];
};

static void *ralloc(MgSource *s, size_t size) {
    size = (size + sizeof(max_align_t) - 1u) & ~(sizeof(max_align_t) - 1u);
    MgReaderChunk *c = s->arena;
    if (!c || c->size - c->used < size) {
        size_t bytes = size > 65536u ? size : 65536u;
        c = malloc(sizeof *c + bytes);
        if (!c) {
            s->oom = true;
            return NULL;
        }
        c->next = s->arena;
        c->used = 0u;
        c->size = bytes;
        s->arena = c;
    }
    void *out = (char *)c->data + c->used;
    c->used += size;
    memset(out, 0, size);
    return out;
}

static void arena_reset(MgSource *s) {
    while (s->arena) {
        MgReaderChunk *next = s->arena->next;
        free(s->arena);
        s->arena = next;
    }
}

/* lexer.mll's position at byte pos */
typedef struct {
    int lineno, charno;
    size_t pos;
} MgPos;

/* parser.ml update_pos over the bytes [from, to) */
static void update_pos(const MgSource *s, MgPos *p, size_t from, size_t to) {
    size_t last = SIZE_MAX;
    int newlines = 0;
    for (size_t i = from; i < to; i++)
        if (s->bytes[i] == '\n') {
            ++newlines;
            last = i - from;
        }
    if (!newlines) {
        p->charno += (int)(to - from);
        return;
    }
    p->lineno += newlines;
    p->charno = (int)(to - from - last);
}

/* where a value of the source list is: a cursor's position, the end for nil */
static size_t cursor_pos(const MgSource *s, const T *v) {
    if (rc_atoms_is_cursor(v)) return rc_atoms_cursor_pos(v);
    return tag(v) == s->op.nil ? s->len : SIZE_MAX;
}

/* P moved by EVENTS (newest first), each newline and doc comment in order;
 * the bytes between them count on their line.  False outside the contract. */
static bool fold_events(const MgSource *s, MgPos *p, const T *events, size_t end) {
    const T *ev[64], **list = ev;
    size_t n = 0, cap = 64;
    bool heap = false, ok = true;
    for (const T *e = events; ok && tag(e) != s->op.ev_nil;) {
        if (n == cap) {
            const T **grown = malloc(2u * cap * sizeof *grown);
            if (!grown) {
                ok = false;
                break;
            }
            memcpy(grown, list, n * sizeof *grown);
            if (heap)
                free(list);
            list = grown;
            heap = true;
            cap *= 2u;
        }
        list[n++] = e;
        if (is(e, s->op.ev_newline, 2u))
            e = KID(e, 1);
        else if (is(e, s->op.ev_comment, 3u))
            e = KID(e, 2);
        else
            ok = false;
    }
    for (size_t k = n; ok && k-- > 0;) {
        const T *e = list[k];
        if (tag(e) == s->op.ev_newline) {
            size_t after = cursor_pos(s, KID(e, 0));   /* just after the newline */
            if (after == SIZE_MAX || after < p->pos + 1u || after > end) {
                ok = false;
                break;
            }
            p->lineno += 1;
            p->charno = 0;
            p->pos = after;
        } else {
            size_t open = cursor_pos(s, KID(e, 0)), after = cursor_pos(s, KID(e, 1));   /* just after "(*" */
            if (open == SIZE_MAX || after == SIZE_MAX || open < p->pos + 2u || after < open || after > end) {
                ok = false;
                break;
            }
            p->charno += (int)(open - 2u - p->pos);
            update_pos(s, p, open - 2u, after);
            p->pos = after;
        }
    }
    if (heap)
        free(list);
    if (!ok || end < p->pos)
        return false;
    p->charno += (int)(end - p->pos);
    p->pos = end;
    return true;
}

/* P moved to the end of STREAM: its events, then the bytes up to its cursor. */
static bool fold_stream(const MgSource *s, MgPos *p, const T *stream) {
    while (is(stream, s->op.pushed, 2u))
        stream = KID(stream, 1);
    if (!is(stream, s->op.stream, 2u))
        return false;
    size_t end = cursor_pos(s, KID(stream, 0));
    if (end == SIZE_MAX || end > s->len)
        return false;
    return fold_events(s, p, KID(stream, 1), end);
}

/* P moved to a position an error names: where the read began
 * (MgItemStartV1, P itself), a stream, or a failed lexeme's start. */
static bool fold_place(const MgSource *s, MgPos *p, const T *place) {
    if (is(place, s->op.item_start, 0u))
        return true;
    if (is(place, s->op.lex_fail_at, 2u)) {
        /* just after the lexeme's first scalar: its start is the scalar
         * boundary before, found from where the read began */
        size_t at = cursor_pos(s, KID(place, 0)), start = p->pos;
        if (at == SIZE_MAX || at > s->len || at <= start)
            return false;
        for (uint32_t c;;) {
            size_t w = rc_atoms_scalar_at(start, &c);
            if (start + w >= at) {
                if (start + w != at)
                    return false;
                break;
            }
            start += w;
        }
        return fold_events(s, p, KID(place, 1), start);
    }
    return fold_stream(s, p, place);
}

/* The reader state with its stream's events dropped (they are folded), or
 * NULL outside the contract. */
static T *fresh_state(MgSource *s, T *state) {
    T *st = KID(state, 1);
    T *chain[64];
    size_t n = 0;
    while (is(st, s->op.pushed, 2u) && n < 64u) {
        chain[n++] = st;
        st = KID(st, 1);
    }
    if (!is(st, s->op.stream, 2u))
        return NULL;
    T *base = rc_mk2((uint16_t)s->op.stream, KID(st, 0), RC_NUL_atoms_0(s->op.ev_nil));
    while (n-- > 0)
        base = rc_mk2((uint16_t)s->op.pushed, KID(chain[n], 0), base);
    return rc_mk3((uint16_t)s->op.reader, KID(state, 0), base, KID(state, 2));
}

/* ── the adapter ── */

static int kind_cat(const MgSource *s, const T *v, uint8_t cat) {
    int c = tag(v);
    uint16_t k = s->kind_of[c];
    if (!k || op_kinds[k - 1u].cat != cat)
        return -1;
    return op_kinds[k - 1u].kind;
}

/* A list of (cp N) as UTF-8, a byte for each escaped byte. */
static const char *ad_str(MgSource *s, const T *v, bool *bad) {
    size_t n = 0;
    const T *t = v;
    for (; is(t, s->op.cp_cons, 2u); t = KID(t, 1))
        n += 4u;
    char *out = ralloc(s, n + 1u);
    if (!out)
        return "";
    size_t k = 0;
    t = v;
    for (; is(t, s->op.cp_cons, 2u); t = KID(t, 1)) {
        const T *cp = KID(t, 0);
        int64_t c64;
        if (!is(cp, s->op.cp, 1u) || !rc_atoms_int_of(KID(cp, 0), (uint16_t)s->op.integer, &c64) || c64 < 0 || c64 > 0x10FFFF) {
            *bad = true;
            break;
        }
        uint32_t c = (uint32_t)c64;
        if (c >= 0xDC80u && c <= 0xDCFFu) {
            out[k++] = (char)(c - 0xDC00u);
        } else if (c < 0x80u) {
            out[k++] = (char)c;
        } else if (c < 0x800u) {
            out[k++] = (char)(0xC0u | c >> 6);
            out[k++] = (char)(0x80u | (c & 0x3Fu));
        } else if (c < 0x10000u) {
            out[k++] = (char)(0xE0u | c >> 12);
            out[k++] = (char)(0x80u | (c >> 6 & 0x3Fu));
            out[k++] = (char)(0x80u | (c & 0x3Fu));
        } else {
            out[k++] = (char)(0xF0u | c >> 18);
            out[k++] = (char)(0x80u | (c >> 12 & 0x3Fu));
            out[k++] = (char)(0x80u | (c >> 6 & 0x3Fu));
            out[k++] = (char)(0x80u | (c & 0x3Fu));
        }
    }
    if (!*bad && !is(t, s->op.cp_nil, 0u))
        *bad = true;
    out[k] = '\0';
    return out;
}

static const char *ad_optstr(MgSource *s, const T *v, bool *bad) {
    if (is(v, s->op.none, 0u))
        return NULL;
    if (!is(v, s->op.some, 1u)) {
        *bad = true;
        return NULL;
    }
    return ad_str(s, KID(v, 0), bad);
}

static int64_t ad_int(MgSource *s, const T *v, bool *bad) {
    if (!is(v, s->op.num, 1u)) {
        *bad = true;
        return 0;
    }
    const char *t = ad_str(s, KID(v, 0), bad);
    char *end;
    long long x = strtoll(t, &end, 10);
    if (!*t || *end)
        *bad = true;
    return (int64_t)x;
}

static bool ad_bool(MgSource *s, const T *v, bool *bad) {
    if (is(v, s->op.yes, 0u))
        return true;
    if (!is(v, s->op.no, 0u))
        *bad = true;
    return false;
}

static uint32_t list_len(const MgSource *s, const T *v, bool *bad) {
    uint32_t n = 0;
    for (; is(v, s->op.list_cons, 2u); v = KID(v, 1))
        ++n;
    if (!is(v, s->op.list_nil, 0u))
        *bad = true;
    return n;
}

static const char **ad_names(MgSource *s, const T *v, uint32_t *len, bool *bad) {
    uint32_t n = list_len(s, v, bad);
    const char **out = ralloc(s, sizeof *out * (n ? n : 1u));
    if (!out)
        return NULL;
    uint32_t k = 0;
    for (; k < n; v = KID(v, 1))
        out[k++] = ad_str(s, KID(v, 0), bad);
    *len = n;
    return out;
}

static MgAscKind ad_asckind(MgSource *s, const T *v, bool *bad) {
    if (is(v, s->op.asc_tp, 0u))
        return MG_ASC_TP;
    if (is(v, s->op.asc_set, 0u))
        return MG_ASC_SET;
    if (!is(v, s->op.asc_subeq, 0u))
        *bad = true;
    return MG_ASC_SUBEQ;
}

static MgSetInfix ad_setinfix(MgSource *s, const T *v, bool *bad) {
    if (is(v, s->op.inf_mem, 0u))
        return MG_SET_MEM;
    if (!is(v, s->op.inf_subq, 0u))
        *bad = true;
    return MG_SET_SUBQ;
}

static MgL *ad_l(MgSource *s, const T *v, bool *bad);

static MgL *ad_optl(MgSource *s, const T *v, bool *bad) {
    if (is(v, s->op.none, 0u))
        return NULL;
    if (!is(v, s->op.some, 1u)) {
        *bad = true;
        return NULL;
    }
    return ad_l(s, KID(v, 0), bad);
}

/* None, or (Some (Pair KIND TYPE)) */
static const MgLAsc *ad_optasc(MgSource *s, const T *v, bool *bad) {
    if (is(v, s->op.none, 0u))
        return NULL;
    if (!is(v, s->op.some, 1u) || !is(KID(v, 0), s->op.pair, 2u)) {
        *bad = true;
        return NULL;
    }
    MgLAsc *a = ralloc(s, sizeof *a);
    if (!a)
        return NULL;
    a->kind = ad_asckind(s, KID(KID(v, 0), 0), bad);
    a->type = ad_l(s, KID(KID(v, 0), 1), bad);
    return a;
}

static MgL **ad_llist(MgSource *s, const T *v, uint32_t *len, bool *bad) {
    uint32_t n = list_len(s, v, bad);
    MgL **out = ralloc(s, sizeof *out * (n ? n : 1u));
    if (!out)
        return NULL;
    uint32_t k = 0;
    for (; k < n; v = KID(v, 1))
        out[k++] = ad_l(s, KID(v, 0), bad);
    *len = n;
    return out;
}

static MgL *ad_l(MgSource *s, const T *v, bool *bad) {
    MgL *l = ralloc(s, sizeof *l);
    int kind = kind_cat(s, v, CAT_L);
    if (!l || kind < 0) {
        *bad = true;
        return l;
    }
    l->kind = (MgLKind)kind;
    T *k[8];
    read_kids(v, k);
    switch (l->kind) {
    case MG_L_BYTE: l->byte = (int32_t)ad_int(s, k[0], bad); break;
    case MG_L_STRING: case MG_L_QSTRING: case MG_L_NAME: l->text = ad_str(s, k[0], bad); break;
    case MG_L_NUM:
        l->negative = ad_bool(s, k[0], bad);
        l->text = ad_str(s, k[1], bad);
        l->text2 = ad_optstr(s, k[2], bad);
        l->text3 = ad_optstr(s, k[3], bad);
        break;
    case MG_L_LET:
        l->text = ad_str(s, k[0], bad);
        l->asc = ad_optasc(s, k[1], bad);
        l->a = ad_l(s, k[2], bad);
        l->b = ad_l(s, k[3], bad);
        break;
    case MG_L_LETM:
        l->text = ad_str(s, k[0], bad);
        l->names = ad_names(s, k[1], &l->names_len, bad);
        l->a = ad_l(s, k[2], bad);
        l->b = ad_l(s, k[3], bad);
        break;
    case MG_L_BINDER: {
        l->text = ad_str(s, k[0], bad);
        l->text2 = ad_str(s, k[1], bad);
        uint32_t n = list_len(s, k[2], bad);
        l->groups = ralloc(s, sizeof *l->groups * (n ? n : 1u));
        l->group_count = n;
        const T *g = k[2];
        for (uint32_t i = 0; l->groups && i < n; i++, g = KID(g, 1)) {
            const T *p = KID(g, 0);
            if (!is(p, s->op.pair, 2u)) {
                *bad = true;
                break;
            }
            l->groups[i].names = ad_names(s, KID(p, 0), &l->groups[i].name_count, bad);
            l->groups[i].asc = ad_optasc(s, KID(p, 1), bad);
        }
        l->a = ad_l(s, k[3], bad);
        break;
    }
    case MG_L_PREFIX: case MG_L_POSTFIX:
        l->text = ad_str(s, k[0], bad);
        l->a = ad_l(s, k[1], bad);
        break;
    case MG_L_INFIX:
        if (is(k[0], s->op.inf_nam, 1u)) {
            l->text = ad_str(s, KID(k[0], 0), bad);
        } else if (is(k[0], s->op.inf_set, 1u)) {
            l->set_infix = true;
            l->set_op = ad_setinfix(s, KID(k[0], 0), bad);
        } else {
            *bad = true;
        }
        l->a = ad_l(s, k[1], bad);
        l->b = ad_l(s, k[2], bad);
        break;
    case MG_L_IMPLICIT:
        l->a = ad_l(s, k[0], bad);
        l->b = ad_l(s, k[1], bad);
        break;
    case MG_L_SEP: case MG_L_REP:
        l->text = ad_str(s, k[0], bad);
        l->set_op = ad_setinfix(s, k[1], bad);
        l->a = ad_l(s, k[2], bad);
        l->b = ad_l(s, k[3], bad);
        break;
    case MG_L_SEPREP:
        l->text = ad_str(s, k[0], bad);
        l->set_op = ad_setinfix(s, k[1], bad);
        l->a = ad_l(s, k[2], bad);
        l->b = ad_l(s, k[3], bad);
        l->c = ad_l(s, k[4], bad);
        break;
    case MG_L_SETENUM: l->list = ad_llist(s, k[0], &l->list_len, bad); break;
    case MG_L_MTUPLE: case MG_L_PAREN:
        l->a = ad_l(s, k[0], bad);
        l->list = ad_llist(s, k[1], &l->list_len, bad);
        break;
    case MG_L_IF:
        l->a = ad_l(s, k[0], bad);
        l->b = ad_l(s, k[1], bad);
        l->c = ad_l(s, k[2], bad);
        break;
    }
    return l;
}

static MgPicase ad_picase(MgSource *s, const T *v, bool *bad) {
    if (is(v, s->op.postfix, 0u))
        return MG_PIC_POSTFIX;
    if (is(v, s->op.infix_none, 0u))
        return MG_PIC_INFIX_NONE;
    if (is(v, s->op.infix_left, 0u))
        return MG_PIC_INFIX_LEFT;
    if (!is(v, s->op.infix_right, 0u))
        *bad = true;
    return MG_PIC_INFIX_RIGHT;
}

static void ad_doc(MgSource *s, MgSourceItem *it, const T *v, bool *bad) {
    int kind = kind_cat(s, v, CAT_DOC);
    if (kind < 0) {
        *bad = true;
        return;
    }
    it->kind = kind;
    T *k[8];
    read_kids(v, k);
    switch ((MgDocKind)kind) {
    case MG_DOC_AUTHOR:
        it->name = ad_str(s, k[0], bad);
        it->names = ad_names(s, k[1], &it->names_len, bad);
        break;
    case MG_DOC_TITLE: case MG_DOC_SALT: case MG_DOC_TREASURE: case MG_DOC_SECTION: case MG_DOC_END:
    case MG_DOC_SUBSCRIPT: case MG_DOC_SUPERSCRIPT:
        it->name = ad_str(s, k[0], bad);
        break;
    case MG_DOC_OPAQUE: case MG_DOC_TRANSPARENT: it->names = ad_names(s, k[0], &it->names_len, bad); break;
    case MG_DOC_SHOWPROOFTERMS: it->flag = ad_bool(s, k[0], bad); break;
    case MG_DOC_VAR:
        it->names = ad_names(s, k[0], &it->names_len, bad);
        it->asc_kind = ad_asckind(s, k[1], bad);
        it->a = ad_l(s, k[2], bad);
        break;
    case MG_DOC_LET:
        it->name = ad_str(s, k[0], bad);
        it->asc = ad_optasc(s, k[1], bad);
        it->a = ad_l(s, k[2], bad);
        break;
    case MG_DOC_HYP: case MG_DOC_PARAM: case MG_DOC_AXIOM:
        it->name = ad_str(s, k[0], bad);
        it->a = ad_l(s, k[1], bad);
        break;
    case MG_DOC_POSTINFIX:
        it->name = ad_str(s, k[0], bad);
        it->a = ad_l(s, k[1], bad);
        it->priority = (int32_t)ad_int(s, k[2], bad);
        it->picase = ad_picase(s, k[3], bad);
        break;
    case MG_DOC_PREFIX:
        it->name = ad_str(s, k[0], bad);
        it->a = ad_l(s, k[1], bad);
        it->priority = (int32_t)ad_int(s, k[2], bad);
        break;
    case MG_DOC_BINDER:
        it->flag = ad_bool(s, k[0], bad);
        it->flag2 = ad_bool(s, k[1], bad);
        it->name = ad_str(s, k[2], bad);
        it->a = ad_l(s, k[3], bad);
        it->b = ad_optl(s, k[4], bad);
        break;
    case MG_DOC_NOTATION: case MG_DOC_UNICODE:
        it->name = ad_str(s, k[0], bad);
        it->names = ad_names(s, k[1], &it->names_len, bad);
        break;
    case MG_DOC_DEF:
        it->name = ad_str(s, k[0], bad);
        it->a = ad_optl(s, k[1], bad);
        it->b = ad_l(s, k[2], bad);
        break;
    case MG_DOC_PARAMHASH:
        it->name = ad_str(s, k[0], bad);
        it->name2 = ad_str(s, k[1], bad);
        it->name3 = ad_optstr(s, k[2], bad);
        break;
    case MG_DOC_THEOREM:
        it->name2 = ad_str(s, k[0], bad);
        it->name = ad_str(s, k[1], bad);
        it->a = ad_l(s, k[2], bad);
        break;
    }
}

static void ad_tac(MgSource *s, MgSourceItem *it, const T *v, bool *bad) {
    int kind = kind_cat(s, v, CAT_TAC);
    if (kind < 0) {
        *bad = true;
        return;
    }
    it->kind = kind;
    T *k[8];
    read_kids(v, k);
    switch ((MgTacKind)kind) {
    case MG_TAC_STRUCT: it->depth = (int32_t)ad_int(s, k[0], bad); break;
    case MG_TAC_EXACT: case MG_TAC_APPLY: case MG_TAC_WITNESS: it->a = ad_l(s, k[0], bad); break;
    case MG_TAC_LET: case MG_TAC_ASSUME:
        it->names = ad_names(s, k[0], &it->names_len, bad);
        it->a = ad_optl(s, k[1], bad);
        break;
    case MG_TAC_SET:
        it->name = ad_str(s, k[0], bad);
        it->a = ad_optl(s, k[1], bad);
        it->b = ad_l(s, k[2], bad);
        break;
    case MG_TAC_CLAIM:
        it->name = ad_str(s, k[0], bad);
        it->a = ad_l(s, k[1], bad);
        break;
    case MG_TAC_PROVE:
        it->a = ad_l(s, k[0], bad);
        it->terms = ad_llist(s, k[1], &it->terms_len, bad);
        break;
    case MG_TAC_CASES: {
        it->a = ad_l(s, k[0], bad);
        uint32_t n = list_len(s, k[1], bad);
        it->case_groups = ralloc(s, sizeof *it->case_groups * (n ? n : 1u));
        it->case_group_count = n;
        const T *g = k[1];
        for (uint32_t i = 0; it->case_groups && i < n; i++, g = KID(g, 1)) {
            uint32_t m = list_len(s, KID(g, 0), bad);
            MgCaseGroup *cg = &it->case_groups[i];
            cg->cases = ralloc(s, sizeof *cg->cases * (m ? m : 1u));
            cg->count = m;
            const T *c = KID(g, 0);
            for (uint32_t j = 0; cg->cases && j < m; j++, c = KID(c, 1)) {
                if (!is(KID(c, 0), s->op.pair, 2u)) {
                    *bad = true;
                    break;
                }
                cg->cases[j].name = ad_str(s, KID(KID(c, 0), 0), bad);
                cg->cases[j].term = ad_l(s, KID(KID(c, 0), 1), bad);
            }
        }
        break;
    }
    case MG_TAC_REWRITE: {
        it->flag = ad_bool(s, k[0], bad);
        it->a = ad_l(s, k[1], bad);
        uint32_t n = list_len(s, k[2], bad);
        it->positions = ralloc(s, sizeof *it->positions * (n ? n : 1u));
        it->positions_len = n;
        const T *p = k[2];
        for (uint32_t i = 0; it->positions && i < n; i++, p = KID(p, 1))
            it->positions[i] = ad_int(s, KID(p, 0), bad);
        break;
    }
    case MG_TAC_SPECIAL:
        it->name = ad_str(s, k[0], bad);
        it->terms = ad_llist(s, k[1], &it->terms_len, bad);
        break;
    case MG_TAC_QED: case MG_TAC_ADMITTED: case MG_TAC_ADMIT: break;
    case MG_TAC_ABY: it->names = ad_names(s, k[0], &it->names_len, bad); break;
    }
}

/* ── the reading ── */

MgSource *mg_source_new(const char *bytes, size_t len) {
    reader_init();
    MgSource *s = calloc(1u, sizeof *s);
    if (!s)
        return NULL;
    s->kind_of = calloc((size_t)rc_nctors, sizeof *s->kind_of);
    if (!s->kind_of) {
        mg_source_free(s);
        return NULL;
    }
#define MG_READER_RESOLVE(f, name, arity) s->op.f = op_of(name, arity);
    MG_READER_OPS(MG_READER_RESOLVE)
#undef MG_READER_RESOLVE
    for (size_t i = 0; i < sizeof op_kinds / sizeof op_kinds[0]; i++) {
        int id = op_of(op_kinds[i].name, op_kinds[i].arity);
        if (id >= 0)
            s->kind_of[id] = (uint16_t)(i + 1u);
    }
    s->start = judgment_of("MgReaderStartV1:10");
    s->next_file = judgment_of("MgReaderNextFileV1:110");
    s->read_item = judgment_of("MgReadItemV1:10");
    s->ended = true;    /* no file yet: the first input starts the reader */
    s->state = NULL;
    if (bytes)
        mg_source_set_input(s, bytes, len);
    return s;
}

void mg_source_free(MgSource *s) {
    if (!s)
        return;
    arena_reset(s);
    rc_heap_free(&s->heap);
    free(s->kind_of);
    free(s);
}

/* The reading's heap and source become this thread's runtime's (enter), and are put back (leave). */
typedef struct { RcAtomsSource outer; } MgEntered;
static MgEntered enter(MgSource *s) {
    MgEntered e = {rc_atoms_source};
    rc_heap_swap(&s->heap);
    rc_atoms_set_source(s->bytes, s->len);
    return e;
}
static void leave(MgSource *s, MgEntered e) {
    rc_atoms_source = e.outer;
    rc_heap_swap(&s->heap);
}

/* The state kept past the step: what the state reaches survives the collection of the step's values. */
static void keep(MgSource *s, T *state) {
    rc_roots[0] = state;
    rc_safepoint(1);
    s->state = rc_roots[0];
}

/* Whether V holds a cursor (a position in the current input); the last child is followed in a loop */
static bool holds_cursor(const T *v) {
    for (;;) {
        if (rc_atoms_is_cursor(v)) return true;
        if (!RC_IS_HEAP(v) || (v->flags & RC_FLAG_BYTES) || v->n == 0) return false;
        for (uint32_t i = 0; i + 1 < v->n; i++)
            if (holds_cursor(v->k[i])) return true;
        v = v->k[v->n - 1];
    }
}

/* The reading ends, for STATUS; its message (Megalodon's, for an error). */
static void finish(MgSource *s, MgGsltReadStatus status, const char *message) {
    s->finished = true;
    s->status = status;
    if (message)
        snprintf(s->error, sizeof s->error, "%s", message);
}

/* The presentations have no reading here, or no more input. */
static void refuse(MgSource *s, const char *message) {
    finish(s, MG_GSLT_READ_REFUSED, NULL);
    snprintf(s->error, sizeof s->error, "parse error %d:%d-%d:%d: %s", s->lineno, s->charno, s->lineno,
             s->charno, message);
}

static const char outside_contract[] = "failure: the reader answered outside its contract";
static const char out_of_memory[] = "failure: out of memory";

/* An error's message parts (texts and integers) written out in order. */
static void message_text(MgSource *s, const T *parts, char *out, size_t cap, bool *bad) {
    size_t n = 0;
    out[0] = '\0';
    for (; is(parts, s->op.list_cons, 2u); parts = KID(parts, 1)) {
        const T *part = KID(parts, 0);
        const char *text;
        char digits[32];
        if (is(part, s->op.num, 1u)) {
            snprintf(digits, sizeof digits, "%lld", (long long)ad_int(s, part, bad));
            text = digits;
        } else {
            text = ad_str(s, part, bad);
        }
        size_t k = strlen(text);
        if (n + k >= cap)
            k = cap - 1u - n;
        memcpy(out + n, text, k);
        n += k;
        out[n] = '\0';
    }
    if (!is(parts, s->op.list_nil, 0u))
        *bad = true;
}

/* E in Megalodon's words: a syntax error between two positions, or a
 * failure at one, which becomes the reader's position. */
static void report_error(MgSource *s, const T *e) {
    char message[400];
    bool bad = false;
    MgPos start = {s->lineno, s->charno, s->pos}, end = start;
    if (is(e, s->op.syntax_error, 3u)) {
        message_text(s, KID(e, 0), message, sizeof message, &bad);
        if (!bad && fold_place(s, &start, KID(e, 1)) && fold_place(s, &end, KID(e, 2))) {
            finish(s, MG_GSLT_READ_ERROR, NULL);
            snprintf(s->error, sizeof s->error, "parse error %d:%d-%d:%d: %s", start.lineno, start.charno,
                     end.lineno, end.charno, message);
            s->lineno = end.lineno;
            s->charno = end.charno;
            return;
        }
    } else if (is(e, s->op.failure, 2u)) {
        message_text(s, KID(e, 0), message, sizeof message, &bad);
        if (!bad && fold_place(s, &end, KID(e, 1))) {
            finish(s, MG_GSLT_READ_ERROR, NULL);
            snprintf(s->error, sizeof s->error, "failure: %s", message);
            s->lineno = end.lineno;
            s->charno = end.charno;
            return;
        }
    }
    finish(s, MG_GSLT_READ_MALFORMED, outside_contract);
}

/* The next file of a run: its position starts afresh, the notation
 * environment and the mode carry over (the presentations' next file). */
void mg_source_set_input(MgSource *s, const char *bytes, size_t len) {
    s->error[0] = '\0';
    s->lineno = 1;
    s->charno = 0;
    s->pos = 0;
    if (s->finished)
        return;
    /* a file not read to its end is read through first */
    if (!s->ended && mg_source_read_items(s, NULL, NULL) != MG_GSLT_READ_END)
        return;
    MgEntered e = enter(s);
    /* the state keeps no position in the input it ends */
    if (s->state && holds_cursor(s->state)) {
        leave(s, e);
        finish(s, MG_GSLT_READ_MALFORMED, outside_contract);
        return;
    }
    s->bytes = (const uint8_t *)bytes;
    s->len = len;
    rc_atoms_set_source(s->bytes, s->len);
    T *cur = rc_atoms_cursor(0);
    T *args[2] = {s->state, cur};
    T *next = s->state ? step(s->next_file, args) : step(s->start, &cur);
    if (!next) {
        leave(s, e);
        refuse(s, "the reader cannot start");
        return;
    }
    keep(s, next);
    leave(s, e);
    s->ended = false;
}

void mg_source_position(const MgSource *s, int *line, int *charno) {
    *line = s->lineno;
    *charno = s->charno;
}

const char *mg_source_error(const MgSource *s) { return s->error; }

/* What a step does with the item it read (O, an MgItemV1), before the
 * step's values end: false when the item is outside the contract. */
typedef bool (*MgItemUse)(MgSource *s, const T *o, void *ctx);

/* One read: 1 when an item was read and USE took it, 0 at the end of the
 * input, -1 when the reading finished (s->status).  The reader's position
 * moves past the item, and the state is kept without its folded events. */
static int source_step(MgSource *s, MgItemUse use, void *ctx) {
    if (s->finished)
        return -1;
    if (s->ended)
        return 0;
    MgEntered e = enter(s);
    T *o = step(s->read_item, &s->state);
    if (!o) {
        /* every reading answers an item, the end or an error */
        leave(s, e);
        refuse(s, "the reader has no reading");
        return -1;
    }
    if (is(o, s->op.reader_end, 2u)) {
        keep(s, o);
        leave(s, e);
        s->ended = true;
        return 0;
    }
    if (is(o, s->op.reader_error, 1u)) {
        report_error(s, KID(o, 0));
        leave(s, e);
        return -1;
    }
    bool ok = is(o, s->op.item, 2u) && is(KID(o, 1), s->op.reader, 3u) &&
              (is(KID(o, 0), s->op.doc, 1u) || is(KID(o, 0), s->op.pftac, 1u));
    if (ok && use)
        ok = use(s, o, ctx);
    MgPos p = {s->lineno, s->charno, s->pos};
    ok = ok && !s->oom && fold_stream(s, &p, KID(KID(o, 1), 1));
    T *fresh = ok ? fresh_state(s, KID(o, 1)) : NULL;
    if (!fresh || s->oom) {
        leave(s, e);
        finish(s, s->oom ? MG_GSLT_READ_NOMEM : MG_GSLT_READ_MALFORMED, s->oom ? out_of_memory : outside_contract);
        return -1;
    }
    keep(s, fresh);
    leave(s, e);
    s->lineno = p.lineno;
    s->charno = p.charno;
    s->pos = p.pos;
    return 1;
}

/* The evaluator's parse tree of the item. */
static bool adapt(MgSource *s, const T *o, void *ctx) {
    (void)ctx;
    bool bad = false;
    const T *v = KID(o, 0);
    if (is(v, s->op.doc, 1u)) {
        ad_doc(s, &s->item, KID(v, 0), &bad);
    } else {
        s->item.tactic = true;
        ad_tac(s, &s->item, KID(v, 0), &bad);
    }
    return !bad;
}

int mg_source_next(MgSource *s, const MgSourceItem **out) {
    arena_reset(s);
    memset(&s->item, 0, sizeof s->item);
    s->item.line = (uint32_t)s->lineno;
    int r = source_step(s, adapt, NULL);
    if (r <= 0)
        return r;
    s->item.end_line = s->lineno;
    s->item.end_char = s->charno;
    *out = &s->item;
    return 1;
}

typedef struct {
    MgGsltEmit emit;
    void *ctx;
} MgEmitUse;

static bool emit_use(MgSource *s, const T *o, void *ctx) {
    (void)s;
    MgEmitUse *e = ctx;
    e->emit(e->ctx, (const MgGsltValue *)KID(o, 0));
    return true;
}

MgGsltReadStatus mg_source_read_items(MgSource *s, MgGsltEmit emit, void *ctx) {
    MgEmitUse e = {emit, ctx};
    int r;
    while ((r = source_step(s, emit ? emit_use : NULL, &e)) > 0)
        ;
    return r == 0 ? MG_GSLT_READ_END : s->status;
}

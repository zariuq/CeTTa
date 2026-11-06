#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_gslt_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const GnProgramV1 megalodon_reader_v1_program;
bool megalodon_reader_v1_entry_MgNextTokenV1_10(GnCtx *cx, GnVal *a0, GnVal **r0);
bool megalodon_reader_v1_entry_MgReaderStartV1_10(GnCtx *cx, GnVal *a0, GnVal **r0);
bool megalodon_reader_v1_entry_MgReaderNextFileV1_110(GnCtx *cx, GnVal *a0, GnVal *a1, GnVal **r0);
bool megalodon_reader_v1_entry_MgReadItemV1_10(GnCtx *cx, GnVal *a0, GnVal **r0);

GnCtx *mg_gslt_reader_context(void) { return gn_ctx_new(&megalodon_reader_v1_program); }

/* The id of a constructor of the compiled program (0 when absent). */
static uint32_t op_named(const char *name, uint32_t arity) {
    const GnProgramV1 *p = &megalodon_reader_v1_program;
    for (uint32_t i = 0; i < p->op_count; i++)
        if (p->ops[i].arity == arity && strcmp(p->ops[i].name, name) == 0)
            return i + GN_FIRST_OP;
    return 0u;
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

GnSourceV1 *mg_gslt_source_load(const char *path) {
    size_t n;
    char *bytes = mg_gslt_file_bytes(path, &n);
    GnSourceV1 *src = bytes ? malloc(sizeof *src) : NULL;
    if (!src) {
        free(bytes);
        return NULL;
    }
    src->bytes = (const uint8_t *)bytes;
    src->len = n;
    return src;
}

void mg_gslt_source_free(GnSourceV1 *src) {
    if (!src)
        return;
    free((void *)src->bytes);
    free(src);
}

MgGsltReadStatus mg_gslt_read_tokens(GnCtx *cx, const GnSourceV1 *src, MgGsltEmit emit, void *ctx) {
    uint32_t end = op_named("MgTokensEndV1", 0), read = op_named("MgTokenReadV1", 2),
             scal = op_named("MgScalarsV1", 1), newline = op_named("MgAfterNewlineV1", 2),
             comment = op_named("MgAfterCommentV1", 3), lex_fail = op_named("MgLexFailV1", 1);
    GnVal *cur = gn_cursor(cx, src, 0);
    for (;;) {
        GnVal *r;
        if (!megalodon_reader_v1_entry_MgNextTokenV1_10(cx, cur, &r))
            return MG_GSLT_READ_REFUSED;
        /* the newlines and doc comments before the token count only for
         * positions */
        while (r->op == newline || r->op == comment)
            r = r->kid[r->op == newline ? 1 : 2];
        if (r->op == end)
            return MG_GSLT_READ_END;
        if (r->op == lex_fail && r->n == 1u)
            return MG_GSLT_READ_ERROR;
        if (r->op != read || r->kid[1]->op != scal)
            return MG_GSLT_READ_MALFORMED;
        emit(ctx, r->kid[0]);
        /* the step's allocations end here; the rest of the input is a
         * position */
        GnVal *rest = r->kid[1]->kid[0];
        if (rest->op != GN_CURSOR)
            return MG_GSLT_READ_MALFORMED;
        size_t pos = (size_t)rest->i;
        gn_scratch_reset(cx);
        cur = gn_cursor(cx, src, pos);
    }
}

/* ── Megalodon documents for the evaluator (mg_source.h) ──────────────────
 * The presentations' items made into the evaluator's parse trees, with
 * Megalodon's positions.  lexer.mll counts the bytes of the current line; a
 * newline starts the next line; a doc comment is counted by update_pos
 * (each newline in it starts a line, and the char after it is the comment's
 * length from its last newline on).  A read's stream keeps the newlines and
 * doc comments it passed (MgEvNewlineV1, MgEvCommentV1, newest first); each
 * item folds them in, and the kept state starts afresh. */

#include "mg_source.h"

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
    X(infix_left, "MgInfixLeftV1", 0) X(infix_right, "MgInfixRightV1", 0)

typedef struct {
#define MG_READER_FIELD(f, name, arity) uint32_t f;
    MG_READER_OPS(MG_READER_FIELD)
#undef MG_READER_FIELD
} MgReaderOps;

/* An input's bytes as the runtime reads them.  Each input keeps its own,
 * for values read from it (a name's span in the notation environment) are
 * kept with the reader state past the input. */
typedef struct MgInput MgInput;
struct MgInput {
    GnSourceV1 src;             /* the file's bytes (borrowed) */
    MgInput *next;              /* the input before it */
};

struct MgSource {
    GnCtx *cx;
    MgReaderOps op;
    uint16_t *kind_of;          /* op id -> op_kinds index + 1 */
    GnSourceV1 *src;            /* the current file's, in inputs */
    MgInput *inputs;            /* every input so far, the current first */
    GnVal *state;               /* the kept reader state (MgReaderV1), or */
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

static uint32_t op_of(const char *name, uint32_t arity) { return op_named(name, arity); }

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
        if (s->src->bytes[i] == '\n') {
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

static size_t cursor_pos(const GnVal *v) { return v && v->op == GN_CURSOR ? (size_t)v->i : SIZE_MAX; }

/* P moved by EVENTS (newest first), each newline and doc comment in order;
 * the bytes between them count on their line.  False outside the contract. */
static bool fold_events(const MgSource *s, MgPos *p, const GnVal *events, size_t end) {
    const GnVal *ev[64], **list = ev;
    size_t n = 0, cap = 64;
    bool heap = false, ok = true;
    for (const GnVal *e = events; ok && e->op != s->op.ev_nil;) {
        if (n == cap) {
            const GnVal **grown = malloc(2u * cap * sizeof *grown);
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
        if (e->op == s->op.ev_newline && e->n == 2u)
            e = e->kid[1];
        else if (e->op == s->op.ev_comment && e->n == 3u)
            e = e->kid[2];
        else
            ok = false;
    }
    for (size_t k = n; ok && k-- > 0;) {
        const GnVal *e = list[k];
        if (e->op == s->op.ev_newline) {
            size_t after = cursor_pos(e->kid[0]);   /* just after the newline */
            if (after == SIZE_MAX || after < p->pos + 1u || after > end) {
                ok = false;
                break;
            }
            p->lineno += 1;
            p->charno = 0;
            p->pos = after;
        } else {
            size_t open = cursor_pos(e->kid[0]), after = cursor_pos(e->kid[1]);   /* just after "(*" */
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
static bool fold_stream(const MgSource *s, MgPos *p, const GnVal *stream) {
    while (stream->op == s->op.pushed && stream->n == 2u)
        stream = stream->kid[1];
    if (stream->op != s->op.stream || stream->n != 2u)
        return false;
    size_t end = cursor_pos(stream->kid[0]);
    if (end == SIZE_MAX || end > s->src->len)
        return false;
    return fold_events(s, p, stream->kid[1], end);
}

/* P moved to a position an error names: where the read began
 * (MgItemStartV1, P itself), a stream, or a failed lexeme's start. */
static bool fold_place(const MgSource *s, MgPos *p, const GnVal *place) {
    if (place->op == s->op.item_start && place->n == 0u)
        return true;
    if (place->op == s->op.lex_fail_at && place->n == 2u) {
        /* just after the lexeme's first scalar: its start is the scalar
         * boundary before, found from where the read began */
        size_t at = cursor_pos(place->kid[0]), start = p->pos;
        if (at == SIZE_MAX || at > s->src->len || at <= start)
            return false;
        for (uint32_t c;;) {
            size_t w = gn_utf8_next(s->src, start, &c);
            if (start + w >= at) {
                if (start + w != at)
                    return false;
                break;
            }
            start += w;
        }
        return fold_events(s, p, place->kid[1], start);
    }
    return fold_stream(s, p, place);
}

/* The reader state with its stream's events dropped (they are folded), or
 * NULL outside the contract. */
static GnVal *fresh_state(MgSource *s, GnVal *state) {
    GnVal *st = state->kid[1];
    GnVal *chain[64];
    size_t n = 0;
    while (st->op == s->op.pushed && st->n == 2u && n < 64u) {
        chain[n++] = st;
        st = st->kid[1];
    }
    if (st->op != s->op.stream || st->n != 2u)
        return NULL;
    GnVal *base = gn_app2(s->cx, s->op.stream, st->kid[0], gn_const(s->cx, s->op.ev_nil));
    while (n-- > 0)
        base = gn_app2(s->cx, s->op.pushed, chain[n]->kid[0], base);
    return gn_app3(s->cx, s->op.reader, state->kid[0], base, state->kid[2]);
}

/* ── the adapter ── */

static int kind_cat(const MgSource *s, const GnVal *v, uint8_t cat) {
    uint32_t k = v->op < GN_FIRST_OP + gn_ctx_program(s->cx)->op_count ? s->kind_of[v->op] : 0u;
    if (!k || op_kinds[k - 1u].cat != cat || op_kinds[k - 1u].arity != v->n)
        return -1;
    return op_kinds[k - 1u].kind;
}

static bool is(const GnVal *v, uint32_t op, uint32_t n) { return v->op == op && v->n == n; }

/* A span of the source's scalars: the source's own bytes. */
static bool is_text_span(MgSource *s, const GnVal *v) {
    return v->op == GN_SPAN && gn_span_tail(v)->cons == s->op.cp_cons && gn_span_tail(v)->nil == s->op.cp_nil;
}

/* A list of (cp N) as UTF-8, a byte for each escaped byte; a span's bytes as
 * they are, for the decoding is one to one. */
static const char *ad_str(MgSource *s, const GnVal *v, bool *bad) {
    size_t n = 0;
    const GnVal *t = v;
    for (; is(t, s->op.cp_cons, 2u); t = t->kid[1])
        n += 4u;
    if (is_text_span(s, t))
        n += gn_span_tail(t)->end - (size_t)t->i;
    char *out = ralloc(s, n + 1u);
    if (!out)
        return "";
    size_t k = 0;
    t = v;
    for (; is(t, s->op.cp_cons, 2u); t = t->kid[1]) {
        const GnVal *cp = t->kid[0];
        if (cp->op != GN_OP_CP || cp->n != 1u || cp->kid[0]->op != GN_INT) {
            *bad = true;
            break;
        }
        uint32_t c = (uint32_t)cp->kid[0]->i;
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
    if (!*bad && is_text_span(s, t)) {
        size_t len = gn_span_tail(t)->end - (size_t)t->i;
        memcpy(out + k, t->src->bytes + t->i, len);
        k += len;
    } else if (!is(t, s->op.cp_nil, 0u)) {
        *bad = true;
    }
    out[k] = '\0';
    return out;
}

static const char *ad_optstr(MgSource *s, const GnVal *v, bool *bad) {
    if (is(v, s->op.none, 0u))
        return NULL;
    if (!is(v, s->op.some, 1u)) {
        *bad = true;
        return NULL;
    }
    return ad_str(s, v->kid[0], bad);
}

static int64_t ad_int(MgSource *s, const GnVal *v, bool *bad) {
    if (!is(v, s->op.num, 1u)) {
        *bad = true;
        return 0;
    }
    const char *t = ad_str(s, v->kid[0], bad);
    char *end;
    long long x = strtoll(t, &end, 10);
    if (!*t || *end)
        *bad = true;
    return (int64_t)x;
}

static bool ad_bool(MgSource *s, const GnVal *v, bool *bad) {
    if (is(v, s->op.yes, 0u))
        return true;
    if (!is(v, s->op.no, 0u))
        *bad = true;
    return false;
}

static uint32_t list_len(const MgSource *s, const GnVal *v, bool *bad) {
    uint32_t n = 0;
    for (; is(v, s->op.list_cons, 2u); v = v->kid[1])
        ++n;
    if (!is(v, s->op.list_nil, 0u))
        *bad = true;
    return n;
}

static const char **ad_names(MgSource *s, const GnVal *v, uint32_t *len, bool *bad) {
    uint32_t n = list_len(s, v, bad);
    const char **out = ralloc(s, sizeof *out * (n ? n : 1u));
    if (!out)
        return NULL;
    uint32_t k = 0;
    for (; k < n; v = v->kid[1])
        out[k++] = ad_str(s, v->kid[0], bad);
    *len = n;
    return out;
}

static MgAscKind ad_asckind(MgSource *s, const GnVal *v, bool *bad) {
    if (is(v, s->op.asc_tp, 0u))
        return MG_ASC_TP;
    if (is(v, s->op.asc_set, 0u))
        return MG_ASC_SET;
    if (!is(v, s->op.asc_subeq, 0u))
        *bad = true;
    return MG_ASC_SUBEQ;
}

static MgSetInfix ad_setinfix(MgSource *s, const GnVal *v, bool *bad) {
    if (is(v, s->op.inf_mem, 0u))
        return MG_SET_MEM;
    if (!is(v, s->op.inf_subq, 0u))
        *bad = true;
    return MG_SET_SUBQ;
}

static MgL *ad_l(MgSource *s, const GnVal *v, bool *bad);

static MgL *ad_optl(MgSource *s, const GnVal *v, bool *bad) {
    if (is(v, s->op.none, 0u))
        return NULL;
    if (!is(v, s->op.some, 1u)) {
        *bad = true;
        return NULL;
    }
    return ad_l(s, v->kid[0], bad);
}

/* None, or (Some (Pair KIND TYPE)) */
static const MgLAsc *ad_optasc(MgSource *s, const GnVal *v, bool *bad) {
    if (is(v, s->op.none, 0u))
        return NULL;
    if (!is(v, s->op.some, 1u) || !is(v->kid[0], s->op.pair, 2u)) {
        *bad = true;
        return NULL;
    }
    MgLAsc *a = ralloc(s, sizeof *a);
    if (!a)
        return NULL;
    a->kind = ad_asckind(s, v->kid[0]->kid[0], bad);
    a->type = ad_l(s, v->kid[0]->kid[1], bad);
    return a;
}

static MgL **ad_llist(MgSource *s, const GnVal *v, uint32_t *len, bool *bad) {
    uint32_t n = list_len(s, v, bad);
    MgL **out = ralloc(s, sizeof *out * (n ? n : 1u));
    if (!out)
        return NULL;
    uint32_t k = 0;
    for (; k < n; v = v->kid[1])
        out[k++] = ad_l(s, v->kid[0], bad);
    *len = n;
    return out;
}

static MgL *ad_l(MgSource *s, const GnVal *v, bool *bad) {
    MgL *l = ralloc(s, sizeof *l);
    int kind = kind_cat(s, v, CAT_L);
    if (!l || kind < 0) {
        *bad = true;
        return l;
    }
    l->kind = (MgLKind)kind;
    GnVal *const *k = v->kid;
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
        const GnVal *g = k[2];
        for (uint32_t i = 0; l->groups && i < n; i++, g = g->kid[1]) {
            const GnVal *p = g->kid[0];
            if (!is(p, s->op.pair, 2u)) {
                *bad = true;
                break;
            }
            l->groups[i].names = ad_names(s, p->kid[0], &l->groups[i].name_count, bad);
            l->groups[i].asc = ad_optasc(s, p->kid[1], bad);
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
            l->text = ad_str(s, k[0]->kid[0], bad);
        } else if (is(k[0], s->op.inf_set, 1u)) {
            l->set_infix = true;
            l->set_op = ad_setinfix(s, k[0]->kid[0], bad);
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

static MgPicase ad_picase(MgSource *s, const GnVal *v, bool *bad) {
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

static void ad_doc(MgSource *s, MgSourceItem *it, const GnVal *v, bool *bad) {
    int kind = kind_cat(s, v, CAT_DOC);
    if (kind < 0) {
        *bad = true;
        return;
    }
    it->kind = kind;
    GnVal *const *k = v->kid;
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

static void ad_tac(MgSource *s, MgSourceItem *it, const GnVal *v, bool *bad) {
    int kind = kind_cat(s, v, CAT_TAC);
    if (kind < 0) {
        *bad = true;
        return;
    }
    it->kind = kind;
    GnVal *const *k = v->kid;
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
        const GnVal *g = k[1];
        for (uint32_t i = 0; it->case_groups && i < n; i++, g = g->kid[1]) {
            uint32_t m = list_len(s, g->kid[0], bad);
            MgCaseGroup *cg = &it->case_groups[i];
            cg->cases = ralloc(s, sizeof *cg->cases * (m ? m : 1u));
            cg->count = m;
            const GnVal *c = g->kid[0];
            for (uint32_t j = 0; cg->cases && j < m; j++, c = c->kid[1]) {
                if (!is(c->kid[0], s->op.pair, 2u)) {
                    *bad = true;
                    break;
                }
                cg->cases[j].name = ad_str(s, c->kid[0]->kid[0], bad);
                cg->cases[j].term = ad_l(s, c->kid[0]->kid[1], bad);
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
        const GnVal *p = k[2];
        for (uint32_t i = 0; it->positions && i < n; i++, p = p->kid[1])
            it->positions[i] = ad_int(s, p->kid[0], bad);
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
    MgSource *s = calloc(1u, sizeof *s);
    if (!s)
        return NULL;
    s->cx = mg_gslt_reader_context();
    const GnProgramV1 *p = &megalodon_reader_v1_program;
    s->kind_of = calloc(p->op_count + GN_FIRST_OP, sizeof *s->kind_of);
    if (!s->cx || !s->kind_of) {
        mg_source_free(s);
        return NULL;
    }
#define MG_READER_RESOLVE(f, name, arity) s->op.f = op_of(name, arity);
    MG_READER_OPS(MG_READER_RESOLVE)
#undef MG_READER_RESOLVE
    for (size_t i = 0; i < sizeof op_kinds / sizeof op_kinds[0]; i++) {
        uint32_t id = op_of(op_kinds[i].name, op_kinds[i].arity);
        if (id)
            s->kind_of[id] = (uint16_t)(i + 1u);
    }
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
    if (s->cx)
        gn_ctx_free(s->cx);
    for (MgInput *in = s->inputs; in;) {
        MgInput *next = in->next;
        free(in);
        in = next;
    }

    free(s->kind_of);
    free(s);
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
static void message_text(MgSource *s, const GnVal *parts, char *out, size_t cap, bool *bad) {
    size_t n = 0;
    out[0] = '\0';
    for (; is(parts, s->op.list_cons, 2u); parts = parts->kid[1]) {
        const GnVal *part = parts->kid[0];
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
static void report_error(MgSource *s, const GnVal *e) {
    char message[400];
    bool bad = false;
    MgPos start = {s->lineno, s->charno, s->pos}, end = start;
    if (is(e, s->op.syntax_error, 3u)) {
        message_text(s, e->kid[0], message, sizeof message, &bad);
        if (!bad && fold_place(s, &start, e->kid[1]) && fold_place(s, &end, e->kid[2])) {
            finish(s, MG_GSLT_READ_ERROR, NULL);
            snprintf(s->error, sizeof s->error, "parse error %d:%d-%d:%d: %s", start.lineno, start.charno,
                     end.lineno, end.charno, message);
            s->lineno = end.lineno;
            s->charno = end.charno;
            return;
        }
    } else if (is(e, s->op.failure, 2u)) {
        message_text(s, e->kid[0], message, sizeof message, &bad);
        if (!bad && fold_place(s, &end, e->kid[1])) {
            finish(s, MG_GSLT_READ_ERROR, NULL);
            snprintf(s->error, sizeof s->error, "failure: %s", message);
            s->lineno = end.lineno;
            s->charno = end.charno;
            return;
        }
    }
    finish(s, MG_GSLT_READ_MALFORMED, outside_contract);
}

typedef struct {
    MgSource *s;
    GnVal *in, *in2, *out;
    bool ok;
    int step;
} MgReaderStep;

static void reader_step(MgReaderStep *t) {
    MgSource *s = t->s;
    switch (t->step) {
    case 0: t->ok = megalodon_reader_v1_entry_MgReaderStartV1_10(s->cx, t->in, &t->out); break;
    case 1: t->ok = megalodon_reader_v1_entry_MgReaderNextFileV1_110(s->cx, t->in, t->in2, &t->out); break;
    default: t->ok = megalodon_reader_v1_entry_MgReadItemV1_10(s->cx, t->in, &t->out); break;
    }
}

/* A step of the presentations with running out of memory caught: 1 with
 * *out, 0 refused, -1 out of memory. */
static int run_step(MgSource *s, int step, GnVal *in, GnVal *in2, GnVal **out) {
    MgReaderStep t = {s, in, in2, NULL, false, step};
    if (!GN_TRY(s->cx, reader_step(&t)))
        return -1;
    *out = t.out;
    return t.ok ? 1 : 0;
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
    MgInput *input = malloc(sizeof *input);
    if (!input) {
        finish(s, MG_GSLT_READ_NOMEM, out_of_memory);
        return;
    }
    *input = (MgInput){{(const uint8_t *)bytes, len}, s->inputs};
    s->inputs = input;
    s->src = &input->src;
    s->lineno = 1;
    s->charno = 0;
    s->pos = 0;
    GnVal *cur = NULL, *next = NULL;
    if (!GN_TRY(s->cx, cur = gn_cursor(s->cx, s->src, 0))) {
        finish(s, MG_GSLT_READ_NOMEM, out_of_memory);
        return;
    }
    int r = s->state ? run_step(s, 1, s->state, cur, &next) : run_step(s, 0, cur, NULL, &next);
    if (r == 0) {
        refuse(s, "the reader cannot start");
        return;
    }
    if (r < 0 || !GN_TRY(s->cx, s->state = gn_keep_state(s->cx, next))) {
        finish(s, MG_GSLT_READ_NOMEM, out_of_memory);
        return;
    }
    gn_scratch_reset(s->cx);
    s->ended = false;
}

void mg_source_position(const MgSource *s, int *line, int *charno) {
    *line = s->lineno;
    *charno = s->charno;
}

const char *mg_source_error(const MgSource *s) { return s->error; }

GnCtx *mg_source_context(const MgSource *s) { return s->cx; }

/* What a step does with the item it read (O, an MgItemV1), before the
 * step's values end: false when the item is outside the contract. */
typedef bool (*MgItemUse)(MgSource *s, const GnVal *o, void *ctx);

/* One read: 1 when an item was read and USE took it, 0 at the end of the
 * input, -1 when the reading finished (s->status).  The reader's position
 * moves past the item, and the state is kept without its folded events. */
static int source_step(MgSource *s, MgItemUse use, void *ctx) {
    if (s->finished)
        return -1;
    if (s->ended)
        return 0;
    GnVal *o;
    int r = run_step(s, 2, s->state, NULL, &o);
    if (r < 0) {
        finish(s, MG_GSLT_READ_NOMEM, out_of_memory);
        return -1;
    }
    if (r == 0) {
        /* every reading answers an item, the end or an error */
        refuse(s, "the reader has no reading");
        return -1;
    }
    if (is(o, s->op.reader_end, 2u)) {
        if (!GN_TRY(s->cx, s->state = gn_keep_state(s->cx, o))) {
            finish(s, MG_GSLT_READ_NOMEM, out_of_memory);
            return -1;
        }
        gn_scratch_reset(s->cx);
        s->ended = true;
        return 0;
    }
    if (is(o, s->op.reader_error, 1u)) {
        report_error(s, o->kid[0]);
        gn_scratch_reset(s->cx);
        return -1;
    }
    /* volatile: these live across the catches below */
    volatile bool ok = is(o, s->op.item, 2u) && is(o->kid[1], s->op.reader, 3u) &&
                       (is(o->kid[0], s->op.doc, 1u) || is(o->kid[0], s->op.pftac, 1u));
    if (ok && use) {
        volatile bool took = false;
        if (!GN_TRY(s->cx, took = use(s, o, ctx)))
            s->oom = true;
        else
            ok = took;
    }
    MgPos p = {s->lineno, s->charno, s->pos};
    ok = ok && fold_stream(s, &p, o->kid[1]->kid[1]);
    GnVal *volatile fresh = NULL;
    if (ok && !s->oom && !GN_TRY(s->cx, fresh = fresh_state(s, o->kid[1])))
        s->oom = true;
    ok = ok && (s->oom || fresh);
    if (ok && !s->oom && !GN_TRY(s->cx, s->state = gn_keep_state(s->cx, fresh)))
        s->oom = true;
    if (!ok || s->oom) {
        gn_scratch_reset(s->cx);
        finish(s, s->oom ? MG_GSLT_READ_NOMEM : MG_GSLT_READ_MALFORMED, s->oom ? out_of_memory : outside_contract);
        return -1;
    }
    s->lineno = p.lineno;
    s->charno = p.charno;
    s->pos = p.pos;
    gn_scratch_reset(s->cx);
    return 1;
}

/* The evaluator's parse tree of the item. */
static bool adapt(MgSource *s, const GnVal *o, void *ctx) {
    (void)ctx;
    bool bad = false;
    const GnVal *v = o->kid[0];
    if (is(v, s->op.doc, 1u)) {
        ad_doc(s, &s->item, v->kid[0], &bad);
    } else {
        s->item.tactic = true;
        ad_tac(s, &s->item, v->kid[0], &bad);
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

static bool emit_use(MgSource *s, const GnVal *o, void *ctx) {
    (void)s;
    MgEmitUse *e = ctx;
    e->emit(e->ctx, o->kid[0]);
    return true;
}

MgGsltReadStatus mg_source_read_items(MgSource *s, MgGsltEmit emit, void *ctx) {
    MgEmitUse e = {emit, ctx};
    int r;
    while ((r = source_step(s, emit ? emit_use : NULL, &e)) > 0)
        ;
    return r == 0 ? MG_GSLT_READ_END : s->status;
}

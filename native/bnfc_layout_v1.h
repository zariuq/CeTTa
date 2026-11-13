#ifndef CETTA_BNFC_LAYOUT_V1_H
#define CETTA_BNFC_LAYOUT_V1_H

/* BNFC's layout resolution (the resolveLayout of BNFC's generated Layout
 * module), shared by CeTTa's readers: the build-time presentation reader
 * (src/gslt_presentation_v1.c) and the run-time token route
 * (native/parser_token_runtime_v1.c).
 *
 *   - After a layout word an implicit block opens at the column of the next
 *     token (at least one column right of the enclosing block) unless "{"
 *     follows; the grammar's "{" is inserted.
 *   - In an implicit block, a token starting a line at the block's column is
 *     preceded by ";" (unless the previous token is ";" or "{"); a line
 *     starting left of it closes the block ("}").
 *   - A stop word closes the current implicit block and every enclosing
 *     implicit block more indented than itself.
 *   - "}" closes the innermost explicit block (and the implicit ones inside
 *     it).
 *   - At the end, implicit blocks close; with layout toplevel the document
 *     is one implicit block at column 1 whose statements end with ";".
 * Header-only, so that every program linking a reader gets it. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t start, end; /* byte span; an inserted token has start == end */
    int32_t term;
    uint32_t line, col;  /* from 1; columns count code points */
    bool virt;
    int32_t extra;       /* carried for the caller; -1 on inserted tokens */
} CettaBnfcLayoutTokenV1;

typedef struct {
    int32_t open, sep, close;  /* the terminals "{", ";" and "}"; sep may be -1 */
    bool toplevel;
    const uint8_t *word_kind;  /* per terminal: 1 opens a block, 2 closes one */
    uint32_t n_terminals;
} CettaBnfcLayoutRulesV1;

typedef struct {
    CettaBnfcLayoutTokenV1 *v;
    size_t n, cap;
} CettaBnfcLayoutTokensV1;

static inline bool cetta_bnfc_layout_v1_push(CettaBnfcLayoutTokensV1 *t, CettaBnfcLayoutTokenV1 k) {
    if (t->n == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 1024;
        CettaBnfcLayoutTokenV1 *v = (CettaBnfcLayoutTokenV1 *)realloc(t->v, nc * sizeof *v);
        if (!v) return false;
        t->v = v;
        t->cap = nc;
    }
    t->v[t->n++] = k;
    return true;
}

/* Lines and columns of tokens in BYTES (UTF-8), as BNFC's lexer counts
   them: a tab advances to the next multiple of 8. TOKS are in order. */
static inline void cetta_bnfc_layout_v1_positions(const uint8_t *bytes, size_t len,
                                                  CettaBnfcLayoutTokenV1 *toks, size_t n) {
    size_t at = 0;
    uint32_t line = 1, col = 1;
    for (size_t i = 0; i < n; i++) {
        for (; at < toks[i].start && at < len; at++) {
            if (bytes[at] == '\n') { line++; col = 1; }
            else if (bytes[at] == '\t') col = ((col - 1) / 8 + 1) * 8 + 1;
            else if ((bytes[at] & 0xC0) != 0x80) col++;
        }
        toks[i].line = line;
        toks[i].col = col;
    }
}

static inline int cetta_bnfc_layout_v1_kind(const CettaBnfcLayoutRulesV1 *r, int32_t term) {
    return term >= 0 && (uint32_t)term < r->n_terminals ? r->word_kind[term] : 0;
}

/* Resolve IN into OUT (owned by the caller). Returns false with a diagnosis. */
static inline bool cetta_bnfc_layout_v1_resolve(const CettaBnfcLayoutRulesV1 *rules,
                                                const CettaBnfcLayoutTokenV1 *in, size_t n,
                                                CettaBnfcLayoutTokensV1 *out, char *err, size_t errsz) {
    int *stack = NULL; /* 0: explicit; c > 0: implicit at column c */
    size_t sp = 0, cap = 0;
    bool ok = false;
#define BL_PUSH(c) do { \
        if (sp == cap) { \
            cap = cap ? cap * 2 : 64; \
            int *ns_ = (int *)realloc(stack, cap * sizeof *ns_); \
            if (!ns_) { goto bl_done; } \
            stack = ns_; \
        } \
        stack[sp++] = (c); \
    } while (0)
#define BL_NEWLINE(t) (out->n == 0 || out->v[out->n - 1].line != (t).line)
#define BL_VIRTUAL(term_) do { CettaBnfcLayoutTokenV1 v_; memset(&v_, 0, sizeof v_); v_.term = (term_); \
        v_.virt = true; v_.extra = -1; \
        if (out->n) { v_.start = v_.end = out->v[out->n - 1].end; v_.line = out->v[out->n - 1].line; \
                      v_.col = out->v[out->n - 1].col + 1; } else { v_.line = 1; v_.col = 1; } \
        if (!cetta_bnfc_layout_v1_push(out, v_)) goto bl_done; } while (0)
#define BL_EMIT(t) do { if (!cetta_bnfc_layout_v1_push(out, (t))) goto bl_done; } while (0)
    BL_PUSH(rules->toplevel ? 1 : 0);
    {
        size_t i = 0;
        while (i < n) {
            CettaBnfcLayoutTokenV1 t0 = in[i];
            int top = stack[sp - 1];
            int kind = cetta_bnfc_layout_v1_kind(rules, t0.term);
            if (t0.term == rules->open) {
                BL_PUSH(0);
                BL_EMIT(t0);
                i++;
                continue;
            }
            if (top > 0) {
                if (kind == 2) {
                    size_t b = 1;
                    while (sp >= b + 1 && stack[sp - 1 - b] > 0 && (uint32_t)stack[sp - 1 - b] > t0.col) b++;
                    if (sp - b < 1) {
                        if (err && errsz) snprintf(err, errsz, "line %u, column %u: a layout stop word closes the whole document",
                                                   (unsigned)t0.line, (unsigned)t0.col);
                        goto bl_done;
                    }
                    for (size_t k = 0; k < b; k++) BL_VIRTUAL(rules->close);
                    sp -= b;
                    BL_EMIT(t0);
                    i++;
                    continue;
                }
                if (BL_NEWLINE(t0) && t0.col < (uint32_t)top) {
                    BL_VIRTUAL(rules->close);
                    sp--;
                    if (sp == 0) {
                        if (err && errsz) snprintf(err, errsz, "line %u, column %u: the text is indented left of the document",
                                                   (unsigned)t0.line, (unsigned)t0.col);
                        goto bl_done;
                    }
                    continue;
                }
            }
            if (kind == 1) {
                if (i + 1 < n && in[i + 1].term == rules->open) {
                    BL_EMIT(t0);
                    i++;
                    continue;
                }
                int indentation = top > 0 ? top : 0;
                int col = i + 1 < n ? (int)in[i + 1].col : (int)t0.col;
                if (col < indentation + 1) col = indentation + 1;
                if (top > 0 && BL_NEWLINE(t0) && t0.col == (uint32_t)top && out->n &&
                    out->v[out->n - 1].term != rules->sep && out->v[out->n - 1].term != rules->open)
                    BL_VIRTUAL(rules->sep);
                BL_EMIT(t0);
                {
                    CettaBnfcLayoutTokenV1 v;
                    memset(&v, 0, sizeof v);
                    v.start = v.end = t0.end;
                    v.term = rules->open;
                    v.line = t0.line;
                    v.col = t0.col + 1;
                    v.virt = true;
                    v.extra = -1;
                    BL_EMIT(v);
                }
                BL_PUSH(col);
                i++;
                continue;
            }
            if (t0.term == rules->close) {
                while (sp > 0 && stack[sp - 1] > 0) sp--;
                if (sp <= 1) {
                    if (err && errsz) snprintf(err, errsz, "line %u, column %u: '}' without an explicit block",
                                               (unsigned)t0.line, (unsigned)t0.col);
                    goto bl_done;
                }
                sp--;
                BL_EMIT(t0);
                i++;
                continue;
            }
            if (top > 0 && BL_NEWLINE(t0) && t0.col == (uint32_t)top) {
                if (out->n && out->v[out->n - 1].term != rules->sep && out->v[out->n - 1].term != rules->open)
                    BL_VIRTUAL(rules->sep);
                BL_EMIT(t0);
                i++;
                continue;
            }
            BL_EMIT(t0);
            i++;
        }
    }
    /* the end of the input */
    while (out->n && sp > 0) {
        int top = stack[sp - 1];
        if (top == 0) {
            if (sp == 1) break;
            sp--;
            continue;
        }
        if (sp == 1) {
            if (rules->sep >= 0 && out->v[out->n - 1].term != rules->sep) BL_VIRTUAL(rules->sep);
            break;
        }
        BL_VIRTUAL(rules->close);
        sp--;
    }
    ok = true;
bl_done:
#undef BL_PUSH
#undef BL_NEWLINE
#undef BL_VIRTUAL
#undef BL_EMIT
    if (!ok && err && errsz && !err[0]) snprintf(err, errsz, "layout resolution failed");
    free(stack);
    return ok;
}

#endif

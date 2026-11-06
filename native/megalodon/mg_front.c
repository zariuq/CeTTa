#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mg_front.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── growable text ────────────────────────────────────────────────────── */

static bool buf_reserve(MgBuf *b, size_t more) {
    if (b->failed)
        return false;
    if (b->len + more + 1u <= b->cap)
        return true;
    size_t cap = b->cap ? b->cap : 256u;
    while (cap < b->len + more + 1u)
        cap *= 2u;
    char *grown = realloc(b->data, cap);
    if (!grown) {
        b->failed = true;
        return false;
    }
    b->data = grown;
    b->cap = cap;
    return true;
}

void mg_buf_putc(MgBuf *b, char c) {
    if (buf_reserve(b, 1u))
        b->data[b->len++] = c;
}

void mg_buf_puts(MgBuf *b, const char *s) {
    size_t n = strlen(s);
    if (buf_reserve(b, n)) {
        memcpy(b->data + b->len, s, n);
        b->len += n;
    }
}

void mg_buf_printf(MgBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char small[128];
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(small, sizeof small, fmt, copy);
    va_end(copy);
    if (n < 0) {
        b->failed = true;
    } else if ((size_t)n < sizeof small) {
        if (buf_reserve(b, (size_t)n)) {
            memcpy(b->data + b->len, small, (size_t)n);
            b->len += (size_t)n;
        }
    } else if (buf_reserve(b, (size_t)n)) {
        vsnprintf(b->data + b->len, (size_t)n + 1u, fmt, ap);
        b->len += (size_t)n;
    }
    va_end(ap);
}

const char *mg_buf_text(MgBuf *b) {
    if (b->failed || !buf_reserve(b, 0u))
        return "";
    b->data[b->len] = '\0';
    return b->data;
}

void mg_buf_reset(MgBuf *b) {
    b->len = 0u;
    b->failed = false;
}

void mg_buf_free(MgBuf *b) {
    free(b->data);
    *b = (MgBuf){0};
}

/* ── -sexprinfo spellings (syntax.ml tp_to_sexpr, tm_to_sexpr) ────────── */

static void name_text(MgBuf *b, const MgStore *s, uint32_t id) {
    const MgName *n = mg_name_get(s, id);
    if (!n) {
        b->failed = true;
        return;
    }
    if (buf_reserve(b, n->len)) {
        memcpy(b->data + b->len, n->text, n->len);
        b->len += n->len;
    }
}

void mg_front_tp_sexpr(MgBuf *b, const MgTp *a) {
    switch (a->tag) {
    case MG_TP_VAR: mg_buf_printf(b, "(TPVAR %u)", a->n); return;
    case MG_TP_PROP: mg_buf_puts(b, "(PROP)"); return;
    case MG_TP_BASE: mg_buf_puts(b, "(SET)"); return;
    case MG_TP_ARR:
        mg_buf_puts(b, "(AR ");
        mg_front_tp_sexpr(b, a->a);
        mg_buf_putc(b, ' ');
        mg_front_tp_sexpr(b, a->b);
        mg_buf_putc(b, ')');
        return;
    default:
        b->failed = true; /* no front-end type has type binders */
        return;
    }
}

void mg_front_tm_sexpr(MgBuf *b, const MgStore *s, const MgTm *m) {
    switch (m->tag) {
    case MG_TM_DB: mg_buf_printf(b, "(DB %u)", m->n); return;
    case MG_TM_NAME:
        mg_buf_puts(b, "(TMH \"");
        name_text(b, s, m->n);
        mg_buf_puts(b, "\")");
        return;
    case MG_TM_PRIM: mg_buf_printf(b, "(PRIM %u)", m->n); return;
    case MG_TM_TAP:
        mg_buf_puts(b, "(TPAP ");
        mg_front_tm_sexpr(b, s, m->f);
        mg_buf_putc(b, ' ');
        mg_front_tp_sexpr(b, m->tp);
        mg_buf_putc(b, ')');
        return;
    case MG_TM_AP:
    case MG_TM_IMP:
        mg_buf_puts(b, m->tag == MG_TM_AP ? "(AP " : "(IMP ");
        mg_front_tm_sexpr(b, s, m->f);
        mg_buf_putc(b, ' ');
        mg_front_tm_sexpr(b, s, m->x);
        mg_buf_putc(b, ')');
        return;
    case MG_TM_LAM:
    case MG_TM_ALL:
        mg_buf_puts(b, m->tag == MG_TM_LAM ? "(LAM " : "(ALL ");
        mg_front_tp_sexpr(b, m->tp);
        mg_buf_putc(b, ' ');
        mg_front_tm_sexpr(b, s, m->f);
        mg_buf_putc(b, ')');
        return;
    default:
        b->failed = true;
        return;
    }
}

void mg_front_pf_sexpr(MgBuf *b, const MgStore *s, const MgPf *d) {
    switch (d->tag) {
    case MG_PF_HYP: mg_buf_printf(b, "(HYP %u)", d->n); return;
    case MG_PF_KNOWN:
        mg_buf_puts(b, "(KNOWN \"");
        name_text(b, s, d->n);
        mg_buf_puts(b, "\")");
        return;
    case MG_PF_TPAP:
        mg_buf_puts(b, "(PTPAP ");
        mg_front_pf_sexpr(b, s, d->p);
        mg_buf_putc(b, ' ');
        mg_front_tp_sexpr(b, d->tp);
        mg_buf_putc(b, ')');
        return;
    case MG_PF_TMAP:
        mg_buf_puts(b, "(PTMAP ");
        mg_front_pf_sexpr(b, s, d->p);
        mg_buf_putc(b, ' ');
        mg_front_tm_sexpr(b, s, d->t);
        mg_buf_putc(b, ')');
        return;
    case MG_PF_PFAP:
        mg_buf_puts(b, "(PPFAP ");
        mg_front_pf_sexpr(b, s, d->p);
        mg_buf_putc(b, ' ');
        mg_front_pf_sexpr(b, s, d->q);
        mg_buf_putc(b, ')');
        return;
    case MG_PF_PLAM:
        mg_buf_puts(b, "(PLAM ");
        mg_front_tm_sexpr(b, s, d->t);
        mg_buf_putc(b, ' ');
        mg_front_pf_sexpr(b, s, d->p);
        mg_buf_putc(b, ')');
        return;
    case MG_PF_TLAM:
        mg_buf_puts(b, "(TLAM ");
        mg_front_tp_sexpr(b, d->tp);
        mg_buf_putc(b, ' ');
        mg_front_pf_sexpr(b, s, d->p);
        mg_buf_putc(b, ')');
        return;
    default:
        b->failed = true;
        return;
    }
}

/* ── message spellings (syntax.ml tp_to_str, tm_to_str) ───────────────── */

void mg_front_tp_str(MgBuf *b, const MgTp *a) {
    switch (a->tag) {
    case MG_TP_VAR: mg_buf_printf(b, "?%u", a->n); return;
    case MG_TP_PROP: mg_buf_puts(b, "prop"); return;
    case MG_TP_BASE: mg_buf_puts(b, "set"); return;
    case MG_TP_ARR:
        mg_buf_putc(b, '(');
        mg_front_tp_str(b, a->a);
        mg_buf_puts(b, " -> ");
        mg_front_tp_str(b, a->b);
        mg_buf_putc(b, ')');
        return;
    default:
        b->failed = true;
        return;
    }
}

void mg_front_tm_str(MgBuf *b, const MgStore *s, const MgTm *m) {
    switch (m->tag) {
    case MG_TM_DB: mg_buf_printf(b, "_%u", m->n); return;
    case MG_TM_NAME:
        mg_buf_putc(b, '#');
        name_text(b, s, m->n);
        return;
    case MG_TM_PRIM: mg_buf_printf(b, "\"%u\"", m->n); return;
    case MG_TM_TAP:
        mg_buf_putc(b, '(');
        mg_front_tm_str(b, s, m->f);
        mg_buf_putc(b, ' ');
        mg_front_tp_str(b, m->tp);
        mg_buf_putc(b, ')');
        return;
    case MG_TM_AP:
        mg_buf_putc(b, '(');
        mg_front_tm_str(b, s, m->f);
        mg_buf_putc(b, ' ');
        mg_front_tm_str(b, s, m->x);
        mg_buf_putc(b, ')');
        return;
    case MG_TM_LAM:
    case MG_TM_ALL:
        mg_buf_puts(b, m->tag == MG_TM_LAM ? "(fun _:" : "(forall _:");
        mg_front_tp_str(b, m->tp);
        mg_buf_puts(b, m->tag == MG_TM_LAM ? " => " : ", ");
        mg_front_tm_str(b, s, m->f);
        mg_buf_putc(b, ')');
        return;
    case MG_TM_IMP:
        mg_buf_putc(b, '(');
        mg_front_tm_str(b, s, m->f);
        mg_buf_puts(b, " -> ");
        mg_front_tm_str(b, s, m->x);
        mg_buf_putc(b, ')');
        return;
    default:
        b->failed = true;
        return;
    }
}

/* syntax.ml pf_to_str */
void mg_front_pf_str(MgBuf *b, const MgStore *s, const MgPf *d) {
    switch (d->tag) {
    case MG_PF_HYP: mg_buf_printf(b, "__%u", d->n); return;
    case MG_PF_KNOWN:
        mg_buf_puts(b, "##");
        name_text(b, s, d->n);
        return;
    case MG_PF_TPAP:
        mg_buf_putc(b, '(');
        mg_front_pf_str(b, s, d->p);
        mg_buf_putc(b, ' ');
        mg_front_tp_str(b, d->tp);
        mg_buf_putc(b, ')');
        return;
    case MG_PF_TMAP:
        mg_buf_putc(b, '(');
        mg_front_pf_str(b, s, d->p);
        mg_buf_putc(b, ' ');
        mg_front_tm_str(b, s, d->t);
        mg_buf_putc(b, ')');
        return;
    case MG_PF_PFAP:
        mg_buf_putc(b, '(');
        mg_front_pf_str(b, s, d->p);
        mg_buf_putc(b, ' ');
        mg_front_pf_str(b, s, d->q);
        mg_buf_putc(b, ')');
        return;
    case MG_PF_PLAM:
        mg_buf_puts(b, "(fun _:");
        mg_front_tm_str(b, s, d->t);
        mg_buf_puts(b, " => ");
        mg_front_pf_str(b, s, d->p);
        mg_buf_putc(b, ')');
        return;
    case MG_PF_TLAM:
        mg_buf_puts(b, "(fun _:");
        mg_front_tp_str(b, d->tp);
        mg_buf_puts(b, " => ");
        mg_front_pf_str(b, s, d->p);
        mg_buf_putc(b, ')');
        return;
    default:
        b->failed = true;
        return;
    }
}

/* ── parsing the -sexprinfo type spelling ────────────────────────────── */

static MgTp *parse_tp(MgStore *s, const char **p, int depth) {
    if (depth > 4096)
        return NULL;
    while (**p == ' ')
        ++*p;
    if (strncmp(*p, "(PROP)", 6) == 0) {
        *p += 6;
        return mg_tp_prop(s);
    }
    if (strncmp(*p, "(SET)", 5) == 0) {
        *p += 5;
        return mg_tp_base(s, 0);
    }
    if (strncmp(*p, "(TPVAR ", 7) == 0) {
        *p += 7;
        char *end;
        unsigned long n = strtoul(*p, &end, 10);
        if (end == *p || *end != ')' || n > 64u)
            return NULL;
        *p = end + 1;
        return mg_tp_var(s, (uint32_t)n);
    }
    if (strncmp(*p, "(AR ", 4) == 0) {
        *p += 4;
        MgTp *a = parse_tp(s, p, depth + 1);
        MgTp *b = a ? parse_tp(s, p, depth + 1) : NULL;
        while (b && **p == ' ')
            ++*p;
        if (!b || **p != ')')
            return NULL;
        ++*p;
        return mg_tp_arr(s, a, b);
    }
    return NULL;
}

MgTp *mg_front_parse_tp(MgStore *s, const char *text) {
    const char *p = text;
    MgTp *a = parse_tp(s, &p, 0);
    return a && *p == '\0' ? a : NULL;
}

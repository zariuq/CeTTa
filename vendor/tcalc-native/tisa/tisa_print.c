/*
 * tisa_print.c -- t-ISA profile p1 programs as HOL Light terms or
 * S-expressions. Compound forms print as (Constructor arg ...) in both
 * styles; only literals, lists and tuples are spelled differently.
 */
#include "tisa_print.h"

#include <inttypes.h>

static const char *type_name(TcIsaValType t) {
    switch (t) {
    case TISA_TYPE_U64:  return "TyU64";
    case TISA_TYPE_U32Z: return "TyU32Z";
    case TISA_TYPE_I32S: return "TyI32S";
    case TISA_TYPE_BOOL: return "TyBool";
    case TISA_TYPE_PTR:  return "TyPtr";
    default:             return NULL;
    }
}

static const char *binop_name(uint8_t op) {
    static const char *names[] = {"BinAdd", "BinSub", "BinAnd", "BinOr", "BinXor", "BinMulLo"};
    return op < 6 ? names[op] : NULL;
}

static const char *shift_name(uint8_t op) {
    static const char *names[] = {"ShiftLeft", "ShiftRightU", "ShiftRightS"};
    return op < 3 ? names[op] : NULL;
}

static const char *cond_name(uint8_t c) {
    static const char *names[] = {"CondEq", "CondNe", "CondUlt", "CondUle", "CondSlt", "CondSle"};
    return c < 6 ? names[c] : NULL;
}

static void word(FILE *f, TcIsaTextStyle style, uint64_t w) {
    if (style == TISA_TEXT_HOL) fprintf(f, "(word %" PRIu64 ")", w);
    else fprintf(f, "%" PRIu64, w);
}

static void integer(FILE *f, TcIsaTextStyle style, int64_t k) {
    if (style == TISA_TEXT_HOL) {
        if (k < 0) fprintf(f, "(-- &%" PRIu64 ":int)", (uint64_t)0 - (uint64_t)k);
        else fprintf(f, "(&%" PRId64 ":int)", k);
    } else fprintf(f, "%" PRId64, k);
}

static bool instr(FILE *f, TcIsaTextStyle style, const TcIsaInstr *i) {
    const char *t = type_name(i->ty);
    switch (i->op) {
    case TISA_OP_IMM:
        if (!t) return false;
        fprintf(f, "(TImm %s %u ", t, i->dst); word(f, style, i->imm64); fputc(')', f);
        return true;
    case TISA_OP_MOV:
        if (!t) return false;
        fprintf(f, "(TMov %s %u %u)", t, i->dst, i->src1);
        return true;
    case TISA_OP_BIN:
        if (!t || !binop_name(i->subop)) return false;
        fprintf(f, "(TBin %s %s %u %u %u)", binop_name(i->subop), t, i->dst, i->src1, i->src2);
        return true;
    case TISA_OP_SHIFT:
        if (!t || !shift_name(i->subop)) return false;
        fprintf(f, "(TShift %s %s %u %u %u)", shift_name(i->subop), t, i->dst, i->src1, i->bit_width);
        return true;
    case TISA_OP_EXTRACT:
        if (!t) return false;
        fprintf(f, "(TExtract %s %u %u %u %u)", t, i->dst, i->src1, i->scale_lg2, i->bit_width);
        return true;
    case TISA_OP_CMP:
        if (!t || !cond_name(i->subop)) return false;
        fprintf(f, "(TCmp %s %s %u %u ", cond_name(i->subop), t, i->dst, i->src1);
        if (i->bit_width) { fputs("(OpImm ", f); word(f, style, i->imm64); fputs("))", f); }
        else fprintf(f, "(OpReg %u))", i->src2);
        return true;
    case TISA_OP_SELECT:
        if (!t) return false;
        fprintf(f, "(TSelect %s %u %u %u %u)", t, i->dst, i->src3, i->src1, i->src2);
        return true;
    case TISA_OP_ADDR:
        fprintf(f, "(TAddr %u %u %u ", i->region_id, i->dst, i->src1);
        if (i->src2 == 0xFFu) fputs("NONE", f); else fprintf(f, "(SOME %u)", i->src2);
        fprintf(f, " %u ", i->scale_lg2); integer(f, style, i->disp32); fputc(')', f);
        return true;
    case TISA_OP_LOAD:
        if (!t) return false;
        if (i->region_id == TISA_REGION_FRAME) {
            if (i->disp32 < 0) return false;
            fprintf(f, "(TLoadFrame %s %u %u)", t, i->dst, (unsigned)i->disp32 / 8u);
            return true;
        }
        fprintf(f, "(TLoad %s %u %u %u %u ", t, i->subop, i->region_id, i->dst, i->src1);
        integer(f, style, i->disp32); fputc(')', f);
        return true;
    case TISA_OP_STORE:
        fprintf(f, "(TStore %u %u %u ", i->subop, i->region_id, i->src1);
        integer(f, style, i->disp32); fprintf(f, " %u)", i->src2);
        return true;
    }
    return false;
}

bool tisa_print_stmt(FILE *f, TcIsaTextStyle style, const TcIsaStmt *s) {
    if (!s) { fputs("TSkip", f); return true; }
    switch (s->kind) {
    case TISA_CTRL_INSTR:
        fputs("(TInstr ", f);
        if (!instr(f, style, &s->u.instr)) return false;
        fputc(')', f);
        return true;
    case TISA_CTRL_SEQ:
        fputs("(TSeq ", f);
        if (!tisa_print_stmt(f, style, s->u.seq.first)) return false;
        fputc(' ', f);
        if (!tisa_print_stmt(f, style, s->u.seq.second)) return false;
        fputc(')', f);
        return true;
    case TISA_CTRL_IF:
        fprintf(f, "(TIf %u ", s->u.if_cond.cond_reg);
        if (!tisa_print_stmt(f, style, s->u.if_cond.then_branch)) return false;
        fputc(' ', f);
        if (!tisa_print_stmt(f, style, s->u.if_cond.else_branch)) return false;
        fputc(')', f);
        return true;
    case TISA_CTRL_COUNT_LOOP:
        fprintf(f, "(TCountLoop %u %u %u ", s->u.count_loop.idx_reg, s->u.count_loop.bound_reg,
                s->u.count_loop.max_iters);
        if (!tisa_print_stmt(f, style, s->u.count_loop.body)) return false;
        fputc(')', f);
        return true;
    case TISA_CTRL_EARLY_EXIT_LOOP:
        fprintf(f, "(TEarlyLoop %u %u %u %u ", s->u.early_exit_loop.idx_reg, s->u.early_exit_loop.bound_reg,
                s->u.early_exit_loop.cont_reg, s->u.early_exit_loop.max_iters);
        if (!tisa_print_stmt(f, style, s->u.early_exit_loop.body)) return false;
        fputc(')', f);
        return true;
    case TISA_CTRL_RETURN:
        fprintf(f, "(TReturn %u %u)", (unsigned)s->u.ret.outcome, s->u.ret.ret_reg);
        return true;
    case TISA_CTRL_FUND:
        fprintf(f, "(TFund %u)", s->u.fund.units);
        return true;
    }
    return false;
}

bool tisa_print_program(FILE *f, TcIsaTextStyle style, const TcIsaProgram *p) {
    bool hol = style == TISA_TEXT_HOL;
    fputs(hol ? "(TProg [" : "(TProg (", f);
    for (uint8_t r = 0; r < p->nregions; ++r) {
        const TcIsaRegionDecl *d = &p->regions[r];
        if (r) fputs(hol ? "; " : " ", f);
        fprintf(f, hol ? "(%u,%u,%u)" : "(%u %u %u)", (unsigned)d->mode, d->min_byte_len, d->align_bytes);
    }
    fputs(hol ? "] [" : ") (", f);
    bool first = true;
    for (uint8_t r = 1; r < TISA_NUM_VREGS; ++r) {
        if (!(p->init_reg_mask & (1u << r))) continue;
        const char *t = type_name(p->init_reg_types[r]);
        if (!t) return false;
        if (!first) fputs(hol ? "; " : " ", f);
        first = false;
        fprintf(f, hol ? "(%u,%s,%u)" : "(%u %s %u)", r, t, p->init_reg_types[r] == TISA_TYPE_PTR ? p->init_reg_region[r] : 0u);
    }
    fputs(hol ? "] " : ") ", f);
    if (!tisa_print_stmt(f, style, p->root)) return false;
    fputc(')', f);
    return true;
}

static void span_paths(const TcIsaStmt *s, const char *path, char (*paths)[256], size_t *k, size_t cap) {
    if (!s) return;
    char child[256];
    #define CHILD(n) (snprintf(child, sizeof child, "%s%s%d", path, *path ? "." : "", (n)), child)
    #define HERE() do { if (*k < cap) snprintf(paths[*k], 256, "%s", path); ++*k; } while (0)
    switch (s->kind) {
    case TISA_CTRL_INSTR: case TISA_CTRL_RETURN: case TISA_CTRL_FUND:
        HERE();
        return;
    case TISA_CTRL_SEQ:
        span_paths(s->u.seq.first, CHILD(0), paths, k, cap);
        span_paths(s->u.seq.second, CHILD(1), paths, k, cap);
        return;
    case TISA_CTRL_IF:
        HERE();
        span_paths(s->u.if_cond.then_branch, CHILD(1), paths, k, cap);
        if (s->u.if_cond.else_branch) {
            HERE();
            span_paths(s->u.if_cond.else_branch, CHILD(2), paths, k, cap);
        }
        return;
    case TISA_CTRL_COUNT_LOOP: case TISA_CTRL_EARLY_EXIT_LOOP:
        HERE(); HERE();
        span_paths(s->kind == TISA_CTRL_COUNT_LOOP ? s->u.count_loop.body : s->u.early_exit_loop.body,
                   CHILD(0), paths, k, cap);
        HERE();
        return;
    }
    #undef HERE
    #undef CHILD
}

size_t tisa_span_paths(const TcIsaStmt *s, char (*paths)[256], size_t cap) {
    size_t k = 0;
    span_paths(s, "", paths, &k, cap);
    return k;
}

/* Every statement with its path, for spans found by statement. */
typedef struct { const TcIsaStmt *s; char path[256]; } StmtPath;
static void stmt_paths(const TcIsaStmt *s, const char *path, StmtPath *out, size_t *k, size_t cap) {
    if (!s) return;
    if (*k < cap) { out[*k].s = s; snprintf(out[*k].path, sizeof out[*k].path, "%s", path); }
    ++*k;
    char child[256];
    #define CHILD(n) (snprintf(child, sizeof child, "%s%s%d", path, *path ? "." : "", (n)), child)
    switch (s->kind) {
    case TISA_CTRL_SEQ:
        stmt_paths(s->u.seq.first, CHILD(0), out, k, cap);
        stmt_paths(s->u.seq.second, CHILD(1), out, k, cap);
        return;
    case TISA_CTRL_IF:
        stmt_paths(s->u.if_cond.then_branch, CHILD(1), out, k, cap);
        stmt_paths(s->u.if_cond.else_branch, CHILD(2), out, k, cap);
        return;
    case TISA_CTRL_COUNT_LOOP: case TISA_CTRL_EARLY_EXIT_LOOP:
        stmt_paths(s->kind == TISA_CTRL_COUNT_LOOP ? s->u.count_loop.body : s->u.early_exit_loop.body,
                   CHILD(0), out, k, cap);
        return;
    default: return;
    }
    #undef CHILD
}

/* Spans name their statement's path, "-" for the wrapper and handlers; a span
   in a loop's copy without hoisted checks has its path prefixed by "F". */
bool tisa_print_image_record(FILE *f, const char *name, const TcIsaProgram *p, const TcIsaX86 *code) {
    static StmtPath paths[8192];
    size_t n = 0;
    stmt_paths(p->root, "", paths, &n, 8192);
    if (n > 8192) return false;
    fprintf(f, "tisa_image \"%s\" `", name);
    if (!tisa_print_program(f, TISA_TEXT_HOL, p)) return false;
    fputs("` \"", f);
    for (size_t b = 0; b < code->len; b++) fprintf(f, "%02x", code->bytes[b]);
    fputs("\" [", f);
    for (size_t k = 0; k < code->nspans; k++) {
        const char *path = "-";
        if (code->spans[k].stmt) {
            size_t j = 0;
            while (j < n && paths[j].s != code->spans[k].stmt) ++j;
            if (j == n) return false;
            path = paths[j].path;
        }
        fprintf(f, "%s(%u,%u,\"%s\",\"%s%s\")", k ? "; " : "", code->spans[k].offset, code->spans[k].length,
                code->spans[k].template_name, code->spans[k].fast ? "F" : "", path);
    }
    fputs("];;\n", f);
    /* Hoisted loops: tisa_hoist "name" [("path", [(region, base, k, lo, hi)], fund units)];; */
    bool any = false;
    for (size_t l = 0; l < code->nloops; ++l) any |= code->loops[l].hoisted;
    if (!any) return true;
    fprintf(f, "tisa_hoist \"%s\" [", name);
    bool first = true;
    for (size_t l = 0; l < code->nloops; ++l) {
        const TcIsaX86Loop *lp = &code->loops[l];
        if (!lp->hoisted) continue;
        size_t j = 0;
        while (j < n && paths[j].s != lp->loop) ++j;
        if (j == n) return false;
        fprintf(f, "%s(\"%s\", [", first ? "" : "; ", paths[j].path);
        for (uint32_t q = 0; q < lp->nranges; ++q)
            fprintf(f, "%s(%u, %u, %u, %lld, %lld)", q ? "; " : "", lp->ranges[q].region, lp->ranges[q].base,
                    lp->ranges[q].k, (long long)lp->ranges[q].lo, (long long)lp->ranges[q].hi);
        fprintf(f, "], %llu)", (unsigned long long)lp->fund_units);
        first = false;
    }
    fputs("];;\n", f);
    return true;
}

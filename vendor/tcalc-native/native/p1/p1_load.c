/*
 * p1_load.c -- t-ISA profile p1 programs from their text (p1_load.h).
 *
 * A recursive-descent reader of exactly the text tisa_print.c writes. Each
 * reader below names the printer's format it inverts; the two must change
 * together. Every literal is matched character for character, so the only
 * freedom left is in the numbers, and those are read in their canonical
 * decimal form and checked against the field that holds them.
 */
#include "p1_load.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct P1Block {
    P1Block  *next;
    size_t    used, cap;
    TcIsaStmt items[];
};

typedef struct {
    const char     *s;
    size_t          n, i;
    bool            hol;
    P1Program      *out;
    P1Error        *err;
    bool            failed;
} Parser;

/* ---- errors -------------------------------------------------------------- */

static bool fail_at(Parser *p, size_t at, const char *format, ...) {
    if (!p->failed && p->err) {
        P1Error *e = p->err;
        memset(e, 0, sizeof *e);
        e->offset = at;
        e->line = 1; e->column = 1;
        for (size_t k = 0; k < at && k < p->n; ++k) {
            if (p->s[k] == '\n') { e->line++; e->column = 1; }
            else e->column++;
        }
        va_list args;
        va_start(args, format);
        vsnprintf(e->message, sizeof e->message, format, args);
        va_end(args);
    }
    p->failed = true;
    return false;
}

/* What the text holds at a position, for messages: a few characters, or the end. */
static const char *shown(const Parser *p, size_t at, char buf[24]) {
    if (at >= p->n) return "the end of the text";
    size_t k = 0;
    buf[k++] = '"';
    for (size_t j = at; j < p->n && k < 16; ++j) {
        unsigned char c = (unsigned char)p->s[j];
        buf[k++] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    buf[k++] = '"';
    buf[k] = 0;
    return buf;
}

/* ---- statement storage ---------------------------------------------------- */

static TcIsaStmt *new_stmt(Parser *p, TcIsaCtrlKind kind) {
    P1Block *b = p->out->blocks;
    if (!b || b->used == b->cap) {
        size_t cap = b ? 2 * b->cap : 256;
        P1Block *nb = malloc(sizeof *nb + cap * sizeof(TcIsaStmt));
        if (!nb) { fail_at(p, p->i, "out of memory after %zu statements", p->out->nstmts); return NULL; }
        nb->next = b; nb->used = 0; nb->cap = cap;
        p->out->blocks = b = nb;
    }
    TcIsaStmt *s = &b->items[b->used++];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    p->out->nstmts++;
    return s;
}

/* ---- lexical pieces ------------------------------------------------------- */

static bool starts(const Parser *p, const char *x) {
    size_t k = strlen(x);
    return p->n - p->i >= k && !memcmp(p->s + p->i, x, k);
}

/* The literal x, character for character. */
static bool lit(Parser *p, const char *x) {
    size_t k = 0;
    while (x[k] && p->i + k < p->n && p->s[p->i + k] == x[k]) ++k;
    if (x[k]) {
        char buf[24];
        return fail_at(p, p->i + k, "expected \"%s\", found %s", x + k, shown(p, p->i + k, buf));
    }
    p->i += k;
    return true;
}

/* A natural number in canonical decimal ("0" or a non-zero digit first), at
 * most max; field names what holds it. */
static bool natural(Parser *p, uint64_t max, uint64_t *v, const char *field) {
    size_t at = p->i;
    char buf[24];
    if (at >= p->n || p->s[at] < '0' || p->s[at] > '9')
        return fail_at(p, at, "expected a number (%s), found %s", field, shown(p, at, buf));
    if (p->s[at] == '0' && at + 1 < p->n && p->s[at + 1] >= '0' && p->s[at + 1] <= '9')
        return fail_at(p, at, "%s: a number with a leading zero is not printed that way", field);
    uint64_t x = 0;
    bool over = false;
    size_t j = at;
    for (; j < p->n && p->s[j] >= '0' && p->s[j] <= '9'; ++j) {
        unsigned d = (unsigned)(p->s[j] - '0');
        if (x > (UINT64_MAX - d) / 10) over = true;
        else x = 10 * x + d;
    }
    if (over || x > max)
        return fail_at(p, at, "%.*s is out of range for %s (at most %" PRIu64 ")",
                       (int)(j - at < 40 ? j - at : 40), p->s + at, field, max);
    p->i = j;
    *v = x;
    return true;
}

static bool u8f(Parser *p, uint8_t *v, uint64_t max, const char *field) {
    uint64_t x;
    if (!natural(p, max, &x, field)) return false;
    *v = (uint8_t)x;
    return true;
}

static bool u32f(Parser *p, uint32_t *v, const char *field) {
    uint64_t x;
    if (!natural(p, UINT32_MAX, &x, field)) return false;
    *v = (uint32_t)x;
    return true;
}

/* A constructor or type name: letters and digits. */
static bool ident(Parser *p, char *buf, size_t cap, const char *what) {
    size_t at = p->i, k = 0;
    while (p->i < p->n && k + 1 < cap &&
           ((p->s[p->i] >= 'A' && p->s[p->i] <= 'Z') || (p->s[p->i] >= 'a' && p->s[p->i] <= 'z') ||
            (p->s[p->i] >= '0' && p->s[p->i] <= '9')))
        buf[k++] = p->s[p->i++];
    buf[k] = 0;
    if (!k) {
        char b[24];
        return fail_at(p, at, "expected %s, found %s", what, shown(p, at, b));
    }
    return true;
}

/* One of names[0..count), as its index. */
static bool choice(Parser *p, const char *const *names, unsigned count, unsigned *v, const char *what) {
    size_t at = p->i;
    char name[32];
    if (!ident(p, name, sizeof name, what)) return false;
    for (unsigned k = 0; k < count; ++k)
        if (!strcmp(name, names[k])) { *v = k; return true; }
    return fail_at(p, at, "\"%s\" is not %s", name, what);
}

/* type_name in tisa_print.c: TcIsaValType 1..5. */
static bool type(Parser *p, TcIsaValType *t) {
    static const char *const names[] = {"TyU64", "TyU32Z", "TyI32S", "TyBool", "TyPtr"};
    unsigned k;
    if (!choice(p, names, 5, &k, "a type (TyU64 TyU32Z TyI32S TyBool TyPtr)")) return false;
    *t = (TcIsaValType)(TISA_TYPE_U64 + k);
    return true;
}

/* word(): "(word N)" in HOL, N in S-expressions. */
static bool word(Parser *p, uint64_t *v) {
    if (!p->hol) return natural(p, UINT64_MAX, v, "a 64-bit word");
    return lit(p, "(word ") && natural(p, UINT64_MAX, v, "a 64-bit word") && lit(p, ")");
}

/* integer() on an int32_t: "(&N:int)" or "(-- &N:int)" with N > 0 in HOL,
 * N or -N in S-expressions. */
static bool integer(Parser *p, int32_t *v) {
    uint64_t m;
    size_t at;
    bool negative;
    if (p->hol) {
        negative = starts(p, "(-- &");
        if (!lit(p, negative ? "(-- &" : "(&")) return false;
    } else {
        negative = p->i < p->n && p->s[p->i] == '-';
        if (negative) p->i++;
    }
    at = p->i;
    if (!natural(p, negative ? (uint64_t)INT32_MAX + 1 : (uint64_t)INT32_MAX, &m, "a 32-bit displacement"))
        return false;
    if (negative && m == 0) return fail_at(p, at, "a negative zero is printed as zero");
    *v = negative ? (int32_t)(-(int64_t)m) : (int32_t)m;
    return !p->hol || lit(p, ":int)");
}

/* ---- instructions: instr() in tisa_print.c ---------------------------------- */

static bool reg(Parser *p, uint8_t *v) { return u8f(p, v, UINT8_MAX, "a register field"); }

static bool instr(Parser *p, TcIsaInstr *i) {
    static const char *const binops[] = {"BinAdd", "BinSub", "BinAnd", "BinOr", "BinXor", "BinMulLo"};
    static const char *const shifts[] = {"ShiftLeft", "ShiftRightU", "ShiftRightS"};
    static const char *const conds[] = {"CondEq", "CondNe", "CondUlt", "CondUle", "CondSlt", "CondSle"};
    memset(i, 0, sizeof *i);
    if (!lit(p, "(")) return false;
    size_t at = p->i;
    char name[16];
    unsigned k;
    if (!ident(p, name, sizeof name, "an instruction constructor") || !lit(p, " ")) return false;
    bool ok;
    if (!strcmp(name, "TImm")) {
        i->op = TISA_OP_IMM;
        ok = type(p, &i->ty) && lit(p, " ") && reg(p, &i->dst) && lit(p, " ") && word(p, &i->imm64);
    } else if (!strcmp(name, "TMov")) {
        i->op = TISA_OP_MOV;
        ok = type(p, &i->ty) && lit(p, " ") && reg(p, &i->dst) && lit(p, " ") && reg(p, &i->src1);
    } else if (!strcmp(name, "TBin")) {
        i->op = TISA_OP_BIN;
        ok = choice(p, binops, 6, &k, "a binary operation") && (i->subop = (uint8_t)k, lit(p, " ")) &&
             type(p, &i->ty) && lit(p, " ") && reg(p, &i->dst) && lit(p, " ") && reg(p, &i->src1) &&
             lit(p, " ") && reg(p, &i->src2);
    } else if (!strcmp(name, "TShift")) {
        i->op = TISA_OP_SHIFT;
        ok = choice(p, shifts, 3, &k, "a shift operation") && (i->subop = (uint8_t)k, lit(p, " ")) &&
             type(p, &i->ty) && lit(p, " ") && reg(p, &i->dst) && lit(p, " ") && reg(p, &i->src1) &&
             lit(p, " ") && u8f(p, &i->bit_width, UINT8_MAX, "a shift amount");
    } else if (!strcmp(name, "TExtract")) {
        i->op = TISA_OP_EXTRACT;
        ok = type(p, &i->ty) && lit(p, " ") && reg(p, &i->dst) && lit(p, " ") && reg(p, &i->src1) &&
             lit(p, " ") && u8f(p, &i->scale_lg2, UINT8_MAX, "an EXTRACT lsb") &&
             lit(p, " ") && u8f(p, &i->bit_width, UINT8_MAX, "an EXTRACT width");
    } else if (!strcmp(name, "TCmp")) {
        i->op = TISA_OP_CMP;
        ok = choice(p, conds, 6, &k, "a comparison") && (i->subop = (uint8_t)k, lit(p, " ")) &&
             type(p, &i->ty) && lit(p, " ") && reg(p, &i->dst) && lit(p, " ") && reg(p, &i->src1) &&
             lit(p, " ");
        if (ok) {
            if (starts(p, "(OpImm ")) {
                i->bit_width = 1;
                ok = lit(p, "(OpImm ") && word(p, &i->imm64) && lit(p, ")");
            } else ok = lit(p, "(OpReg ") && reg(p, &i->src2) && lit(p, ")");
        }
    } else if (!strcmp(name, "TSelect")) {
        i->op = TISA_OP_SELECT;
        ok = type(p, &i->ty) && lit(p, " ") && reg(p, &i->dst) && lit(p, " ") && reg(p, &i->src3) &&
             lit(p, " ") && reg(p, &i->src1) && lit(p, " ") && reg(p, &i->src2);
    } else if (!strcmp(name, "TAddr")) {
        /* The index register 0xFF is how tisa.h writes NONE. */
        i->op = TISA_OP_ADDR; i->ty = TISA_TYPE_PTR;
        ok = u8f(p, &i->region_id, UINT8_MAX, "a region") && lit(p, " ") && reg(p, &i->dst) &&
             lit(p, " ") && reg(p, &i->src1) && lit(p, " ");
        if (ok) {
            if (starts(p, "NONE")) { i->src2 = 0xFFu; p->i += 4; }
            else ok = lit(p, "(SOME ") && u8f(p, &i->src2, 0xFEu, "an index register (0xFF means NONE)") &&
                      lit(p, ")");
        }
        ok = ok && lit(p, " ") && u8f(p, &i->scale_lg2, UINT8_MAX, "an ADDR scale") && lit(p, " ") &&
             integer(p, &i->disp32);
    } else if (!strcmp(name, "TLoad")) {
        /* Region 255 is the frame, printed as TLoadFrame. */
        i->op = TISA_OP_LOAD;
        ok = type(p, &i->ty) && lit(p, " ") && u8f(p, &i->subop, UINT8_MAX, "a width") && lit(p, " ") &&
             u8f(p, &i->region_id, TISA_REGION_FRAME - 1u, "a region (255 is the frame: TLoadFrame)") &&
             lit(p, " ") && reg(p, &i->dst) && lit(p, " ") && reg(p, &i->src1) && lit(p, " ") &&
             integer(p, &i->disp32);
    } else if (!strcmp(name, "TLoadFrame")) {
        uint64_t w;
        i->op = TISA_OP_LOAD; i->subop = TISA_MEM_U64; i->region_id = TISA_REGION_FRAME; i->src1 = TISA_V0;
        ok = type(p, &i->ty) && lit(p, " ") && reg(p, &i->dst) && lit(p, " ") &&
             natural(p, (uint64_t)INT32_MAX / 8u, &w, "a frame word index (its displacement 8w is an int32)");
        if (ok) i->disp32 = (int32_t)(8u * w);
    } else if (!strcmp(name, "TStore")) {
        i->op = TISA_OP_STORE; i->ty = TISA_TYPE_U64;
        ok = u8f(p, &i->subop, UINT8_MAX, "a width") && lit(p, " ") &&
             u8f(p, &i->region_id, UINT8_MAX, "a region") && lit(p, " ") && reg(p, &i->src1) &&
             lit(p, " ") && integer(p, &i->disp32) && lit(p, " ") && reg(p, &i->src2);
    } else return fail_at(p, at, "\"%s\" is not an instruction constructor", name);
    return ok && lit(p, ")");
}

/* ---- statements: tisa_print_stmt -------------------------------------------- */

static bool stmt(Parser *p, const TcIsaStmt **out, size_t depth) {
    if (depth > p->out->depth) p->out->depth = depth;
    if (starts(p, "TSkip")) { p->i += 5; *out = NULL; return true; }
    if (!lit(p, "(")) return false;
    size_t at = p->i;
    char name[16];
    if (!ident(p, name, sizeof name, "a statement constructor (or TSkip)") || !lit(p, " ")) return false;
    TcIsaStmt *s;
    bool ok;
    if (!strcmp(name, "TInstr")) {
        if (!(s = new_stmt(p, TISA_CTRL_INSTR))) return false;
        ok = instr(p, &s->u.instr);
    } else if (!strcmp(name, "TSeq")) {
        if (!(s = new_stmt(p, TISA_CTRL_SEQ))) return false;
        ok = stmt(p, &s->u.seq.first, depth + 1) && lit(p, " ") && stmt(p, &s->u.seq.second, depth + 1);
    } else if (!strcmp(name, "TIf")) {
        if (!(s = new_stmt(p, TISA_CTRL_IF))) return false;
        ok = reg(p, &s->u.if_cond.cond_reg) && lit(p, " ") &&
             stmt(p, &s->u.if_cond.then_branch, depth + 1) && lit(p, " ") &&
             stmt(p, &s->u.if_cond.else_branch, depth + 1);
    } else if (!strcmp(name, "TCountLoop")) {
        if (!(s = new_stmt(p, TISA_CTRL_COUNT_LOOP))) return false;
        ok = reg(p, &s->u.count_loop.idx_reg) && lit(p, " ") && reg(p, &s->u.count_loop.bound_reg) &&
             lit(p, " ") && u32f(p, &s->u.count_loop.max_iters, "a loop cap") && lit(p, " ") &&
             stmt(p, &s->u.count_loop.body, depth + 1);
    } else if (!strcmp(name, "TEarlyLoop")) {
        if (!(s = new_stmt(p, TISA_CTRL_EARLY_EXIT_LOOP))) return false;
        ok = reg(p, &s->u.early_exit_loop.idx_reg) && lit(p, " ") &&
             reg(p, &s->u.early_exit_loop.bound_reg) && lit(p, " ") &&
             reg(p, &s->u.early_exit_loop.cont_reg) && lit(p, " ") &&
             u32f(p, &s->u.early_exit_loop.max_iters, "a loop cap") && lit(p, " ") &&
             stmt(p, &s->u.early_exit_loop.body, depth + 1);
    } else if (!strcmp(name, "TReturn")) {
        uint8_t o = 0;
        if (!(s = new_stmt(p, TISA_CTRL_RETURN))) return false;
        ok = u8f(p, &o, TISA_OUTCOME_EXHAUSTED, "an outcome code (0..4)") && lit(p, " ") &&
             reg(p, &s->u.ret.ret_reg);
        s->u.ret.outcome = (TcIsaOutcome)o;
    } else if (!strcmp(name, "TFund")) {
        if (!(s = new_stmt(p, TISA_CTRL_FUND))) return false;
        ok = u32f(p, &s->u.fund.units, "FUND units");
    } else return fail_at(p, at, "\"%s\" is not a statement constructor", name);
    *out = s;
    return ok && lit(p, ")");
}

/* ---- programs: tisa_print_program ------------------------------------------- */

/* After a list item: another follows exactly when the separator's first
 * character does; the rest of the separator must then be there too (so the
 * error falls on the first character that leaves the grammar). True when an
 * item follows, or when the separator is broken (p->failed is then set). */
static bool next_item(Parser *p, const char *item) {
    if (p->i >= p->n || p->s[p->i] != item[0]) return false;
    lit(p, item);
    return true;
}

static bool program(Parser *p) {
    TcIsaProgram *g = &p->out->prog;
    const char *item = p->hol ? "; " : " ", *field = p->hol ? "," : " ";
    if (!lit(p, "(TProg ") || !lit(p, p->hol ? "[" : "(")) return false;
    /* Region declarations (mode, minimum length, alignment), regions 0, 1, ... */
    if (!starts(p, p->hol ? "]" : ")")) {
        for (;;) {
            uint8_t mode;
            uint32_t min, align;
            size_t at = p->i;
            if (g->nregions == TISA_MAX_REGIONS)
                return fail_at(p, at, "a tenth region declaration: p1 declares at most %u", TISA_MAX_REGIONS);
            if (!lit(p, "(") || !u8f(p, &mode, TISA_CAP_RW, "a region mode (0 none, 1 RO, 2 RW)") ||
                !lit(p, field) || !u32f(p, &min, "a minimum length") || !lit(p, field) ||
                !u32f(p, &align, "an alignment") || !lit(p, ")"))
                return false;
            g->regions[g->nregions] = (TcIsaRegionDecl){g->nregions, (TcIsaCapMode)mode, min, align, NULL};
            g->nregions++;
            if (!next_item(p, item)) break;
            if (p->failed) return false;
        }
    }
    if (!lit(p, p->hol ? "] [" : ") (")) return false;
    /* Initialized registers, printed for v1..v11 in increasing order. */
    if (!starts(p, p->hol ? "]" : ")")) {
        int last = 0;
        for (;;) {
            uint8_t r, region;
            TcIsaValType t;
            if (!lit(p, "(")) return false;
            size_t at = p->i;
            if (!u8f(p, &r, TISA_NUM_VREGS - 1u, "an entry register (v1..v11)")) return false;
            if (r <= last)
                return fail_at(p, at, "entry register v%u: registers are listed once each, v1..v11 in increasing order",
                               (unsigned)r);
            if (!lit(p, field) || !type(p, &t) || !lit(p, field)) return false;
            at = p->i;
            if (!u8f(p, &region, UINT8_MAX, "an entry pointer's region")) return false;
            if (t != TISA_TYPE_PTR && region != 0)
                return fail_at(p, at, "a register that is not a pointer has region 0");
            if (!lit(p, ")")) return false;
            g->init_reg_mask |= (uint16_t)(1u << r);
            g->init_reg_types[r] = t;
            g->init_reg_region[r] = t == TISA_TYPE_PTR ? region : 0;
            last = r;
            if (!next_item(p, item)) break;
            if (p->failed) return false;
        }
    }
    if (!lit(p, p->hol ? "] " : ") ")) return false;
    return stmt(p, &g->root, 1) && lit(p, ")");
}

/* ---- the interface ---------------------------------------------------------- */

void p1_free(P1Program *p) {
    if (!p) return;
    for (P1Block *b = p->blocks, *next; b; b = next) { next = b->next; free(b); }
    memset(p, 0, sizeof *p);
}

bool p1_detect_style(const char *text, size_t len, TcIsaTextStyle *style) {
    static const char head[] = "(TProg ";
    if (len < sizeof head || memcmp(text, head, sizeof head - 1)) return false;
    char c = text[sizeof head - 1];
    if (c == '[') { *style = TISA_TEXT_HOL; return true; }
    if (c == '(') { *style = TISA_TEXT_SEXPR; return true; }
    return false;
}

bool p1_parse_prefix(const char *text, size_t len, TcIsaTextStyle style,
                     P1Program *out, size_t *end, P1Error *err) {
    memset(out, 0, sizeof *out);
    Parser p = {.s = text, .n = len, .i = 0, .hol = style == TISA_TEXT_HOL, .out = out, .err = err};
    if (!program(&p)) { p1_free(out); return false; }
    if (end) *end = p.i;
    return true;
}

bool p1_parse(const char *text, size_t len, TcIsaTextStyle style, P1Program *out, P1Error *err) {
    size_t end;
    if (!p1_parse_prefix(text, len, style, out, &end, err)) return false;
    if (end != len) {
        Parser p = {.s = text, .n = len, .err = err};
        char buf[24];
        fail_at(&p, end, "the text goes on after the program: %s", shown(&p, end, buf));
        p1_free(out);
        return false;
    }
    return true;
}

bool p1_load_file(const char *path, P1Program *out, TcIsaTextStyle *style, P1Error *err) {
    memset(out, 0, sizeof *out);
    FILE *f = fopen(path, "rb");
    char *text = NULL;
    size_t len = 0, cap = 0;
    if (f) {
        for (;;) {
            if (cap - len < 65536) {
                char *t = realloc(text, cap ? 2 * cap : 65536);
                if (!t) { free(text); fclose(f); f = NULL; text = NULL; break; }
                text = t; cap = cap ? 2 * cap : 65536;
            }
            size_t got = fread(text + len, 1, cap - len, f);
            len += got;
            if (got == 0) break;
        }
        if (f && ferror(f)) { free(text); text = NULL; }
        if (f) fclose(f);
    }
    if (!text) {
        if (err) { memset(err, 0, sizeof *err); snprintf(err->message, sizeof err->message, "cannot read the file"); }
        return false;
    }
    TcIsaTextStyle st;
    if (!p1_detect_style(text, len, &st)) {
        Parser p = {.s = text, .n = len, .err = err, .hol = true};
        lit(&p, "(TProg ");
        if (!p.failed) {
            char buf[24];
            fail_at(&p, p.i, "expected \"[\" (HOL text) or \"(\" (S-expression), found %s", shown(&p, p.i, buf));
        }
        free(text);
        return false;
    }
    size_t body = len > 0 && text[len - 1] == '\n' ? len - 1 : len;
    bool ok = p1_parse(text, body, st, out, err);
    free(text);
    if (ok && style) *style = st;
    return ok;
}

void p1_print_error(FILE *f, const char *path, const P1Error *err) {
    fprintf(f, "%s%s%u:%u: %s (byte %zu)\n", path ? path : "", path ? ":" : "", err->line, err->column,
            err->message, err->offset);
}

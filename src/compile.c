#define _GNU_SOURCE
#include "compile.h"
#include "match.h"
#include "grounded.h"
#include <string.h>
#include <stdlib.h>

/* ── Equation Analysis ──────────────────────────────────────────────────── */

typedef struct {
    const char *head;
    Atom **lhs;
    Atom **rhs;
    uint32_t arity;
    uint32_t len, cap;
} EqGroup;

typedef struct {
    EqGroup *groups;
    uint32_t len, cap;
} EqGroupSet;

static void eq_group_set_init(EqGroupSet *gs) {
    gs->groups = NULL; gs->len = 0; gs->cap = 0;
}

static EqGroup *eq_group_find_or_create(EqGroupSet *gs, const char *head, uint32_t arity) {
    for (uint32_t i = 0; i < gs->len; i++)
        if (strcmp(gs->groups[i].head, head) == 0) return &gs->groups[i];
    if (gs->len >= gs->cap) {
        gs->cap = gs->cap ? gs->cap * 2 : 8;
        gs->groups = realloc(gs->groups, sizeof(EqGroup) * gs->cap);
    }
    EqGroup *g = &gs->groups[gs->len++];
    g->head = head; g->lhs = NULL; g->rhs = NULL;
    g->arity = arity; g->len = 0; g->cap = 0;
    return g;
}

static void eq_group_add(EqGroup *g, Atom *lhs, Atom *rhs) {
    if (g->len >= g->cap) {
        g->cap = g->cap ? g->cap * 2 : 4;
        g->lhs = realloc(g->lhs, sizeof(Atom *) * g->cap);
        g->rhs = realloc(g->rhs, sizeof(Atom *) * g->cap);
    }
    g->lhs[g->len] = lhs; g->rhs[g->len] = rhs; g->len++;
}

static void analyze_equations(Space *s, EqGroupSet *out) {
    eq_group_set_init(out);
    for (uint32_t i = 0; i < s->len; i++) {
        Atom *a = s->atoms[i];
        if (a->kind != ATOM_EXPR || a->expr.len != 3) continue;
        if (!atom_is_symbol(a->expr.elems[0], "=")) continue;
        Atom *lhs = a->expr.elems[1], *rhs = a->expr.elems[2];
        const char *head = NULL; uint32_t arity = 0;
        if (lhs->kind == ATOM_SYMBOL) { head = lhs->name; arity = 0; }
        else if (lhs->kind == ATOM_EXPR && lhs->expr.len >= 1 &&
                 lhs->expr.elems[0]->kind == ATOM_SYMBOL) {
            head = lhs->expr.elems[0]->name; arity = lhs->expr.len - 1;
        }
        if (!head) continue;
        EqGroup *g = eq_group_find_or_create(out, head, arity);
        eq_group_add(g, lhs, rhs);
    }
}

/* ── LLVM IR Helpers ────────────────────────────────────────────────────── */

static int g_label = 0;
static int g_tmp = 0;

static void emit_mangled(FILE *out, const char *name) {
    for (const char *p = name; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_') fputc(*p, out);
        else fprintf(out, "_%02x", (unsigned char)*p);
    }
}

/* ── Variable Capture Table ─────────────────────────────────────────────── */

#define MAX_CAPTURES 32
typedef struct {
    const char *var_name;  /* MeTTa variable name */
    char llvm_reg[32];     /* LLVM register holding the captured atom */
} VarCapture;

typedef struct {
    VarCapture caps[MAX_CAPTURES];
    uint32_t len;
} CaptureTable;

static void capture_init(CaptureTable *ct) { ct->len = 0; }

static void capture_add(CaptureTable *ct, const char *var, const char *reg) {
    if (ct->len >= MAX_CAPTURES) return;
    ct->caps[ct->len].var_name = var;
    strncpy(ct->caps[ct->len].llvm_reg, reg, 31);
    ct->caps[ct->len].llvm_reg[31] = '\0';
    ct->len++;
}

static const char *capture_lookup(CaptureTable *ct, const char *var) {
    for (uint32_t i = 0; i < ct->len; i++)
        if (strcmp(ct->caps[i].var_name, var) == 0)
            return ct->caps[i].llvm_reg;
    return NULL;
}

/* ── Pattern Match Emission (with variable capture) ─────────────────────── */

static void emit_pattern(FILE *out, const char *atom_reg, Atom *pattern,
                         int fail_label, CaptureTable *captures) {
    if (pattern->kind == ATOM_VAR) {
        /* Capture variable — record which LLVM register holds it */
        capture_add(captures, pattern->name, atom_reg);
        return;
    }
    if (pattern->kind == ATOM_SYMBOL) {
        int l = g_label++;
        fprintf(out, "  %%chk%d = call i1 @cetta_atom_is_symbol(%%Atom* %s, i8* "
                "getelementptr([%zu x i8], [%zu x i8]* @str_",
                l, atom_reg, strlen(pattern->name)+1, strlen(pattern->name)+1);
        emit_mangled(out, pattern->name);
        fprintf(out, ", i32 0, i32 0))\n");
        fprintf(out, "  br i1 %%chk%d, label %%ok%d, label %%fail%d\n", l, l, fail_label);
        fprintf(out, "ok%d:\n", l);
        return;
    }
    if (pattern->kind == ATOM_GROUNDED && pattern->ground.gkind == GV_INT) {
        int l = g_label++;
        fprintf(out, "  %%chk%d = call i1 @cetta_atom_is_int(%%Atom* %s, i64 %ld)\n",
                l, atom_reg, (long)pattern->ground.ival);
        fprintf(out, "  br i1 %%chk%d, label %%ok%d, label %%fail%d\n", l, l, fail_label);
        fprintf(out, "ok%d:\n", l);
        return;
    }
    if (pattern->kind == ATOM_EXPR) {
        int l = g_label++;
        /* Check it's an expression with correct length */
        fprintf(out, "  %%isexpr%d = call i1 @cetta_atom_is_expr(%%Atom* %s)\n", l, atom_reg);
        fprintf(out, "  br i1 %%isexpr%d, label %%exok%d, label %%fail%d\n", l, l, fail_label);
        fprintf(out, "exok%d:\n", l);
        fprintf(out, "  %%elen%d = call i32 @cetta_expr_len(%%Atom* %s)\n", l, atom_reg);
        fprintf(out, "  %%elenok%d = icmp eq i32 %%elen%d, %u\n", l, l, pattern->expr.len);
        fprintf(out, "  br i1 %%elenok%d, label %%earok%d, label %%fail%d\n", l, l, fail_label);
        fprintf(out, "earok%d:\n", l);
        for (uint32_t i = 0; i < pattern->expr.len; i++) {
            int el = g_tmp++;
            fprintf(out, "  %%el%d = call %%Atom* @cetta_expr_elem(%%Atom* %s, i32 %u)\n",
                    el, atom_reg, i);
            char elem_reg[16];
            snprintf(elem_reg, sizeof(elem_reg), "%%el%d", el);
            emit_pattern(out, elem_reg, pattern->expr.elems[i], fail_label, captures);
        }
    }
}

/* ── RHS Construction Emission ──────────────────────────────────────────── */

/* Emit LLVM IR to build an atom from an RHS pattern using captured variables.
   Returns the LLVM register name holding the constructed atom. */
static const char *emit_rhs(FILE *out, Atom *rhs, CaptureTable *captures,
                             EqGroupSet *all_groups) {
    static char reg[32];

    if (rhs->kind == ATOM_VAR) {
        const char *cap = capture_lookup(captures, rhs->name);
        if (cap) return cap;
        /* Uncaptured variable — return as-is (shouldn't happen in well-formed equations) */
        int t = g_tmp++;
        snprintf(reg, sizeof(reg), "%%uncap%d", t);
        fprintf(out, "  %s = call %%Atom* @cetta_atom_symbol(%%Arena* %%arena, i8* "
                "getelementptr([2 x i8], [2 x i8]* @str__3f, i32 0, i32 0)) ; uncaptured var\n", reg);
        return reg;
    }
    if (rhs->kind == ATOM_SYMBOL) {
        int t = g_tmp++;
        snprintf(reg, sizeof(reg), "%%sym%d", t);
        fprintf(out, "  %s = call %%Atom* @cetta_atom_symbol(%%Arena* %%arena, i8* "
                "getelementptr([%zu x i8], [%zu x i8]* @str_",
                reg, strlen(rhs->name)+1, strlen(rhs->name)+1);
        emit_mangled(out, rhs->name);
        fprintf(out, ", i32 0, i32 0))\n");
        return reg;
    }
    if (rhs->kind == ATOM_GROUNDED && rhs->ground.gkind == GV_INT) {
        int t = g_tmp++;
        snprintf(reg, sizeof(reg), "%%int%d", t);
        fprintf(out, "  %s = call %%Atom* @cetta_atom_int(%%Arena* %%arena, i64 %ld)\n",
                reg, (long)rhs->ground.ival);
        return reg;
    }
    if (rhs->kind == ATOM_EXPR && rhs->expr.len > 0) {
        /* Build children, then construct expression */
        const char **child_regs = malloc(sizeof(const char *) * rhs->expr.len);
        for (uint32_t i = 0; i < rhs->expr.len; i++) {
            child_regs[i] = strdup(emit_rhs(out, rhs->expr.elems[i], captures, all_groups));
        }
        /* Allocate children array */
        int t = g_tmp++;
        snprintf(reg, sizeof(reg), "%%arr%d", t);
        fprintf(out, "  %s = alloca %%Atom*, i32 %u\n", reg, rhs->expr.len);
        for (uint32_t i = 0; i < rhs->expr.len; i++) {
            int gi = g_tmp++;
            fprintf(out, "  %%gep%d = getelementptr %%Atom*, %%Atom** %s, i32 %u\n", gi, reg, i);
            fprintf(out, "  store %%Atom* %s, %%Atom** %%gep%d\n", child_regs[i], gi);
            free((void *)child_regs[i]);
        }
        free(child_regs);
        int r = g_tmp++;
        snprintf(reg, sizeof(reg), "%%expr%d", r);
        fprintf(out, "  %s = call %%Atom* @cetta_atom_expr(%%Arena* %%arena, %%Atom** %%arr%d, i32 %u)\n",
                reg, t, rhs->expr.len);
        return reg;
    }
    /* Fallback */
    return "null";
}

/* ── String Constants ───────────────────────────────────────────────────── */

static void emit_str_const(FILE *out, const char *name) {
    fprintf(out, "@str_");
    emit_mangled(out, name);
    fprintf(out, " = private constant [%zu x i8] c\"", strlen(name) + 1);
    for (const char *p = name; *p; p++) {
        if (*p >= 32 && *p < 127 && *p != '"' && *p != '\\')
            fputc(*p, out);
        else fprintf(out, "\\%02x", (unsigned char)*p);
    }
    fprintf(out, "\\00\"\n");
}

static void collect_syms(Atom *a, const char ***syms, uint32_t *n, uint32_t *cap) {
    if (a->kind == ATOM_SYMBOL) {
        for (uint32_t i = 0; i < *n; i++)
            if (strcmp((*syms)[i], a->name) == 0) return;
        if (*n >= *cap) { *cap = *cap ? *cap * 2 : 32; *syms = realloc((void*)*syms, sizeof(const char *) * *cap); }
        (*syms)[(*n)++] = a->name;
    }
    if (a->kind == ATOM_EXPR)
        for (uint32_t i = 0; i < a->expr.len; i++)
            collect_syms(a->expr.elems[i], syms, n, cap);
}

/* ── Top-Level Compiler ─────────────────────────────────────────────────── */

void compile_space_to_llvm(Space *s, Arena *a, FILE *out) {
    (void)a;
    EqGroupSet gs;
    analyze_equations(s, &gs);
    if (gs.len == 0) { fprintf(out, "; No compilable equations\n"); return; }

    /* Collect symbols */
    const char **syms = NULL; uint32_t nsyms = 0, csyms = 0;
    for (uint32_t gi = 0; gi < gs.len; gi++) {
        collect_syms(atom_symbol(a, gs.groups[gi].head), &syms, &nsyms, &csyms);
        for (uint32_t ei = 0; ei < gs.groups[gi].len; ei++) {
            collect_syms(gs.groups[gi].lhs[ei], &syms, &nsyms, &csyms);
            collect_syms(gs.groups[gi].rhs[ei], &syms, &nsyms, &csyms);
        }
    }
    /* Add "?" for uncaptured vars */
    collect_syms(atom_symbol(a, "?"), &syms, &nsyms, &csyms);

    /* Header */
    fprintf(out, "; CeTTa AOT Compiled LLVM IR\n");
    fprintf(out, "; %u equation groups, %u symbols\n\n", gs.len, nsyms);
    fprintf(out, "%%Atom = type opaque\n%%Space = type opaque\n%%Arena = type opaque\n");
    fprintf(out, "%%ResultSet = type opaque\n\n");

    /* Runtime declarations */
    fprintf(out, "declare i1 @cetta_atom_is_symbol(%%Atom*, i8*)\n");
    fprintf(out, "declare i1 @cetta_atom_is_int(%%Atom*, i64)\n");
    fprintf(out, "declare i1 @cetta_atom_is_expr(%%Atom*)\n");
    fprintf(out, "declare i32 @cetta_expr_len(%%Atom*)\n");
    fprintf(out, "declare %%Atom* @cetta_expr_elem(%%Atom*, i32)\n");
    fprintf(out, "declare %%Atom* @cetta_atom_symbol(%%Arena*, i8*)\n");
    fprintf(out, "declare %%Atom* @cetta_atom_int(%%Arena*, i64)\n");
    fprintf(out, "declare %%Atom* @cetta_atom_expr(%%Arena*, %%Atom**, i32)\n");
    fprintf(out, "declare void @cetta_rs_add(%%ResultSet*, %%Atom*)\n\n");

    /* String constants */
    for (uint32_t i = 0; i < nsyms; i++) emit_str_const(out, syms[i]);
    fprintf(out, "\n");

    /* Emit compiled functions */
    for (uint32_t gi = 0; gi < gs.len; gi++) {
        EqGroup *g = &gs.groups[gi];
        g_label = 0; g_tmp = 0;

        fprintf(out, "; === %s (arity %u, %u equations) ===\n", g->head, g->arity, g->len);
        fprintf(out, "define void @cetta_");
        emit_mangled(out, g->head);
        fprintf(out, "(%%Space* %%space, %%Arena* %%arena");
        for (uint32_t ai = 0; ai < g->arity; ai++)
            fprintf(out, ", %%Atom* %%arg%u", ai);
        fprintf(out, ", i32 %%fuel, %%ResultSet* %%rs) {\nentry:\n");

        for (uint32_t ei = 0; ei < g->len; ei++) {
            int fail_lbl = g_label++;
            CaptureTable captures;
            capture_init(&captures);

            fprintf(out, "\n  ; --- eq %u ---\n", ei);

            /* Pattern match each arg */
            if (g->lhs[ei]->kind == ATOM_EXPR) {
                for (uint32_t ai = 1; ai < g->lhs[ei]->expr.len && ai <= g->arity; ai++) {
                    char areg[16]; snprintf(areg, sizeof(areg), "%%arg%u", ai - 1);
                    emit_pattern(out, areg, g->lhs[ei]->expr.elems[ai], fail_lbl, &captures);
                }
            }

            /* Build RHS from captured variables */
            const char *result_reg = emit_rhs(out, g->rhs[ei], &captures, &gs);
            fprintf(out, "  call void @cetta_rs_add(%%ResultSet* %%rs, %%Atom* %s)\n", result_reg);
            fprintf(out, "  br label %%next%u\n", ei);

            fprintf(out, "fail%d:\n  br label %%next%u\n", fail_lbl, ei);
            fprintf(out, "next%u:\n", ei);
        }

        fprintf(out, "  ret void\n}\n\n");
    }

    /* Cleanup */
    for (uint32_t gi = 0; gi < gs.len; gi++) {
        free(gs.groups[gi].lhs); free(gs.groups[gi].rhs);
    }
    free(gs.groups); free((void *)syms);
}

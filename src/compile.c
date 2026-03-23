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
        gs->groups = cetta_realloc(gs->groups, sizeof(EqGroup) * gs->cap);
    }
    EqGroup *g = &gs->groups[gs->len++];
    g->head = head; g->lhs = NULL; g->rhs = NULL;
    g->arity = arity; g->len = 0; g->cap = 0;
    return g;
}

static void eq_group_add(EqGroup *g, Atom *lhs, Atom *rhs) {
    if (g->len >= g->cap) {
        g->cap = g->cap ? g->cap * 2 : 4;
        g->lhs = cetta_realloc(g->lhs, sizeof(Atom *) * g->cap);
        g->rhs = cetta_realloc(g->rhs, sizeof(Atom *) * g->cap);
    }
    g->lhs[g->len] = lhs; g->rhs[g->len] = rhs; g->len++;
}

static EqGroup *eq_group_lookup(EqGroupSet *gs, const char *head) {
    for (uint32_t i = 0; i < gs->len; i++)
        if (strcmp(gs->groups[i].head, head) == 0) return &gs->groups[i];
    return NULL;
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
    const char *var_name;
    char llvm_reg[32];
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

/* ── Pattern Match Emission ─────────────────────────────────────────────── */

static void emit_pattern(FILE *out, const char *atom_reg, Atom *pattern,
                         int fail_label, CaptureTable *captures) {
    if (pattern->kind == ATOM_VAR) {
        const char *existing = capture_lookup(captures, pattern->name);
        if (existing) {
            int l = g_label++;
            fprintf(out, "  %%dupeq%d = call i1 @cetta_atom_eq(%%Atom* %s, %%Atom* %s)\n",
                    l, existing, atom_reg);
            fprintf(out, "  br i1 %%dupeq%d, label %%dupok%d, label %%fail%d\n", l, l, fail_label);
            fprintf(out, "dupok%d:\n", l);
        } else {
            capture_add(captures, pattern->name, atom_reg);
        }
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
    if (pattern->kind == ATOM_GROUNDED) {
        int l = g_label++;
        switch (pattern->ground.gkind) {
        case GV_INT:
            fprintf(out, "  %%chk%d = call i1 @cetta_atom_is_int(%%Atom* %s, i64 %ld)\n",
                    l, atom_reg, (long)pattern->ground.ival);
            break;
        case GV_FLOAT:
            fprintf(out, "  %%chk%d = call i1 @cetta_atom_is_float(%%Atom* %s, double %e)\n",
                    l, atom_reg, pattern->ground.fval);
            break;
        case GV_BOOL:
            fprintf(out, "  %%chk%d = call i1 @cetta_atom_is_bool(%%Atom* %s, i1 %d)\n",
                    l, atom_reg, pattern->ground.bval ? 1 : 0);
            break;
        case GV_STRING:
            fprintf(out, "  %%chk%d = call i1 @cetta_atom_is_string(%%Atom* %s, i8* "
                    "getelementptr([%zu x i8], [%zu x i8]* @str_",
                    l, atom_reg, strlen(pattern->ground.sval)+1, strlen(pattern->ground.sval)+1);
            emit_mangled(out, pattern->ground.sval);
            fprintf(out, ", i32 0, i32 0))\n");
            break;
        default:
            fprintf(out, "  br label %%fail%d ; unsupported grounded pattern\n", fail_label);
            return;
        }
        fprintf(out, "  br i1 %%chk%d, label %%ok%d, label %%fail%d\n", l, l, fail_label);
        fprintf(out, "ok%d:\n", l);
        return;
    }
    if (pattern->kind == ATOM_EXPR) {
        int l = g_label++;
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

/* ── RHS Emission (with direct calls for compiled heads) ────────────────── */

/* Forward declaration — emit_rhs and emit_call_or_build are mutually aware */
static const char *emit_rhs(FILE *out, Atom *rhs, CaptureTable *captures,
                             EqGroupSet *all_groups);

/* Emit a direct call to a compiled function, returning the first result as
   an Atom* register. Uses a temporary ResultSet on the stack. */
static const char *emit_direct_call(FILE *out, EqGroup *g, Atom *call_expr,
                                     CaptureTable *captures, EqGroupSet *all_groups) {
    static char reg[32];

    /* Build args (children of the call expr, skipping head symbol).
       Each arg must be evaluated before passing to the compiled function,
       because the function pattern-matches on evaluated values (not raw exprs). */
    uint32_t nargs = call_expr->expr.len - 1;
    const char **arg_regs = cetta_malloc(sizeof(const char *) * (nargs ? nargs : 1));
    for (uint32_t i = 0; i < nargs; i++) {
        /* Build the raw arg atom */
        const char *raw = emit_rhs(out, call_expr->expr.elems[i + 1], captures, all_groups);
        /* Evaluate it via metta_eval to get the value */
        int eval_rs = g_tmp++;
        fprintf(out, "  %%evalrs%d = call %%ResultSet* @cetta_rs_alloc()\n", eval_rs);
        fprintf(out, "  call void @metta_eval(%%Space* %%space, %%Arena* %%arena, "
                "%%Atom* null, %%Atom* %s, i32 %%fuel, %%ResultSet* %%evalrs%d)\n", raw, eval_rs);
        int eval_res = g_tmp++;
        char eval_reg[32];
        snprintf(eval_reg, sizeof(eval_reg), "%%evr%d", eval_res);
        fprintf(out, "  %s = call %%Atom* @cetta_rs_first(%%ResultSet* %%evalrs%d)\n", eval_reg, eval_rs);
        fprintf(out, "  call void @cetta_rs_free(%%ResultSet* %%evalrs%d)\n", eval_rs);
        arg_regs[i] = strdup(eval_reg);
    }

    /* Allocate temporary ResultSet for the compiled function's results */
    int rs_id = g_tmp++;
    fprintf(out, "  %%tmprs%d = call %%ResultSet* @cetta_rs_alloc()\n", rs_id);

    /* Call the compiled function directly */
    fprintf(out, "  call void @cetta_");
    emit_mangled(out, g->head);
    fprintf(out, "(%%Space* %%space, %%Arena* %%arena");
    for (uint32_t i = 0; i < nargs; i++)
        fprintf(out, ", %%Atom* %s", arg_regs[i]);
    fprintf(out, ", i32 %%fuel, %%ResultSet* %%tmprs%d)\n", rs_id);

    /* Extract first result (or null if empty), then free temp ResultSet */
    int r = g_tmp++;
    snprintf(reg, sizeof(reg), "%%dres%d", r);
    fprintf(out, "  %s = call %%Atom* @cetta_rs_first(%%ResultSet* %%tmprs%d)\n", reg, rs_id);
    fprintf(out, "  call void @cetta_rs_free(%%ResultSet* %%tmprs%d)\n", rs_id);

    for (uint32_t i = 0; i < nargs; i++) free((void *)arg_regs[i]);
    free(arg_regs);
    return reg;
}

static const char *emit_rhs(FILE *out, Atom *rhs, CaptureTable *captures,
                             EqGroupSet *all_groups) {
    static char reg[32];

    if (rhs->kind == ATOM_VAR) {
        const char *cap = capture_lookup(captures, rhs->name);
        if (cap) return cap;
        int t = g_tmp++;
        snprintf(reg, sizeof(reg), "%%var%d", t);
        fprintf(out, "  %s = call %%Atom* @cetta_atom_var(%%Arena* %%arena, i8* "
                "getelementptr([%zu x i8], [%zu x i8]* @str_",
                reg, strlen(rhs->name)+1, strlen(rhs->name)+1);
        emit_mangled(out, rhs->name);
        fprintf(out, ", i32 0, i32 0))\n");
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
    if (rhs->kind == ATOM_GROUNDED) {
        int t = g_tmp++;
        switch (rhs->ground.gkind) {
        case GV_INT:
            snprintf(reg, sizeof(reg), "%%int%d", t);
            fprintf(out, "  %s = call %%Atom* @cetta_atom_int(%%Arena* %%arena, i64 %ld)\n",
                    reg, (long)rhs->ground.ival);
            return reg;
        case GV_FLOAT:
            snprintf(reg, sizeof(reg), "%%flt%d", t);
            fprintf(out, "  %s = call %%Atom* @cetta_atom_float(%%Arena* %%arena, double %e)\n",
                    reg, rhs->ground.fval);
            return reg;
        case GV_BOOL:
            snprintf(reg, sizeof(reg), "%%bool%d", t);
            fprintf(out, "  %s = call %%Atom* @cetta_atom_bool(%%Arena* %%arena, i1 %d)\n",
                    reg, rhs->ground.bval ? 1 : 0);
            return reg;
        case GV_STRING:
            snprintf(reg, sizeof(reg), "%%str%d", t);
            fprintf(out, "  %s = call %%Atom* @cetta_atom_string(%%Arena* %%arena, i8* "
                    "getelementptr([%zu x i8], [%zu x i8]* @str_",
                    reg, strlen(rhs->ground.sval)+1, strlen(rhs->ground.sval)+1);
            emit_mangled(out, rhs->ground.sval);
            fprintf(out, ", i32 0, i32 0))\n");
            return reg;
        default:
            return "null";
        }
    }
    if (rhs->kind == ATOM_EXPR) {
        if (rhs->expr.len == 0) {
            int t = g_tmp++;
            snprintf(reg, sizeof(reg), "%%emptyexpr%d", t);
            fprintf(out, "  %s = call %%Atom* @cetta_atom_expr(%%Arena* %%arena, %%Atom** null, i32 0)\n", reg);
            return reg;
        }

        /* Check if this is a call to a compiled head → direct call */
        if (rhs->expr.elems[0]->kind == ATOM_SYMBOL) {
            EqGroup *target = eq_group_lookup(all_groups, rhs->expr.elems[0]->name);
            if (target && target->arity == rhs->expr.len - 1) {
                return emit_direct_call(out, target, rhs, captures, all_groups);
            }
        }

        /* Not a compiled head → build atom tree, let metta_eval handle it */
        const char **child_regs = cetta_malloc(sizeof(const char *) * rhs->expr.len);
        for (uint32_t i = 0; i < rhs->expr.len; i++)
            child_regs[i] = strdup(emit_rhs(out, rhs->expr.elems[i], captures, all_groups));
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
        if (*n >= *cap) { *cap = *cap ? *cap * 2 : 32; *syms = cetta_realloc((void*)*syms, sizeof(const char *) * *cap); }
        (*syms)[(*n)++] = a->name;
    }
    if (a->kind == ATOM_VAR) {
        for (uint32_t i = 0; i < *n; i++)
            if (strcmp((*syms)[i], a->name) == 0) return;
        if (*n >= *cap) { *cap = *cap ? *cap * 2 : 32; *syms = cetta_realloc((void*)*syms, sizeof(const char *) * *cap); }
        (*syms)[(*n)++] = a->name;
    }
    if (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING) {
        for (uint32_t i = 0; i < *n; i++)
            if (strcmp((*syms)[i], a->ground.sval) == 0) return;
        if (*n >= *cap) { *cap = *cap ? *cap * 2 : 32; *syms = cetta_realloc((void*)*syms, sizeof(const char *) * *cap); }
        (*syms)[(*n)++] = a->ground.sval;
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

    /* Collect all string constants */
    const char **syms = NULL; uint32_t nsyms = 0, csyms = 0;
    for (uint32_t gi = 0; gi < gs.len; gi++) {
        collect_syms(atom_symbol(a, gs.groups[gi].head), &syms, &nsyms, &csyms);
        for (uint32_t ei = 0; ei < gs.groups[gi].len; ei++) {
            collect_syms(gs.groups[gi].lhs[ei], &syms, &nsyms, &csyms);
            collect_syms(gs.groups[gi].rhs[ei], &syms, &nsyms, &csyms);
        }
    }

    /* Header */
    fprintf(out, "; CeTTa AOT Compiled LLVM IR (with direct calls)\n");
    fprintf(out, "; %u equation groups, %u strings\n\n", gs.len, nsyms);
    fprintf(out, "%%Atom = type opaque\n%%Space = type opaque\n%%Arena = type opaque\n");
    fprintf(out, "%%ResultSet = type opaque\n\n");

    /* Runtime declarations */
    fprintf(out, "; Pattern matching helpers\n");
    fprintf(out, "declare i1 @cetta_atom_is_symbol(%%Atom*, i8*)\n");
    fprintf(out, "declare i1 @cetta_atom_is_int(%%Atom*, i64)\n");
    fprintf(out, "declare i1 @cetta_atom_is_float(%%Atom*, double)\n");
    fprintf(out, "declare i1 @cetta_atom_is_bool(%%Atom*, i1)\n");
    fprintf(out, "declare i1 @cetta_atom_is_string(%%Atom*, i8*)\n");
    fprintf(out, "declare i1 @cetta_atom_is_expr(%%Atom*)\n");
    fprintf(out, "declare i32 @cetta_expr_len(%%Atom*)\n");
    fprintf(out, "declare %%Atom* @cetta_expr_elem(%%Atom*, i32)\n");
    fprintf(out, "declare i1 @cetta_atom_eq(%%Atom*, %%Atom*)\n");
    fprintf(out, "; Atom constructors\n");
    fprintf(out, "declare %%Atom* @cetta_atom_symbol(%%Arena*, i8*)\n");
    fprintf(out, "declare %%Atom* @cetta_atom_var(%%Arena*, i8*)\n");
    fprintf(out, "declare %%Atom* @cetta_atom_int(%%Arena*, i64)\n");
    fprintf(out, "declare %%Atom* @cetta_atom_float(%%Arena*, double)\n");
    fprintf(out, "declare %%Atom* @cetta_atom_bool(%%Arena*, i1)\n");
    fprintf(out, "declare %%Atom* @cetta_atom_string(%%Arena*, i8*)\n");
    fprintf(out, "declare %%Atom* @cetta_atom_expr(%%Arena*, %%Atom**, i32)\n");
    fprintf(out, "; Evaluation callback (for non-compiled heads)\n");
    fprintf(out, "declare void @metta_eval(%%Space*, %%Arena*, %%Atom*, %%Atom*, i32, %%ResultSet*)\n");
    fprintf(out, "; ResultSet helpers (for direct calls needing results as atoms)\n");
    fprintf(out, "declare %%ResultSet* @cetta_rs_alloc()\n");
    fprintf(out, "declare void @cetta_rs_add(%%ResultSet*, %%Atom*)\n");
    fprintf(out, "declare %%Atom* @cetta_rs_first(%%ResultSet*)\n");
    fprintf(out, "declare void @cetta_rs_free(%%ResultSet*)\n");
    fprintf(out, "\n");

    /* String constants */
    for (uint32_t i = 0; i < nsyms; i++) emit_str_const(out, syms[i]);
    fprintf(out, "\n");

    /* Note: compiled functions call each other directly via @cetta_<name>.
       LLVM resolves forward references within the same module automatically. */

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

            /* Pattern match each arg against LHS */
            if (g->lhs[ei]->kind == ATOM_EXPR) {
                for (uint32_t ai = 1; ai < g->lhs[ei]->expr.len && ai <= g->arity; ai++) {
                    char areg[16]; snprintf(areg, sizeof(areg), "%%arg%u", ai - 1);
                    emit_pattern(out, areg, g->lhs[ei]->expr.elems[ai], fail_lbl, &captures);
                }
            }

            /* Build RHS — direct calls for compiled heads, metta_eval for others */
            const char *rhs_reg = emit_rhs(out, g->rhs[ei], &captures, &gs);

            /* Evaluate the constructed RHS via metta_eval.
               For direct-call results (already evaluated), metta_eval on an atom
               that's already a value is a no-op (returns it unchanged). */
            fprintf(out, "  call void @metta_eval(%%Space* %%space, %%Arena* %%arena, "
                    "%%Atom* null, %%Atom* %s, i32 %%fuel, %%ResultSet* %%rs)\n", rhs_reg);

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

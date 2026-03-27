#define _GNU_SOURCE
#include "grounded.h"
#include "match.h"
#include "space.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} StringBuf;

typedef struct {
    const char *name;
    Atom *mapped;
} FoldVarMapEntry;

typedef struct {
    FoldVarMapEntry *items;
    uint32_t len;
    uint32_t cap;
} FoldVarMap;

static void sb_init(StringBuf *sb) {
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_ensure(StringBuf *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return;
    size_t next = sb->cap ? sb->cap * 2 : 64;
    while (next < need) next *= 2;
    sb->buf = cetta_realloc(sb->buf, next);
    sb->cap = next;
}

static void sb_append_n(StringBuf *sb, const char *s, size_t n) {
    sb_ensure(sb, n);
    memcpy(sb->buf + sb->len, s, n);
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

static void sb_append(StringBuf *sb, const char *s) {
    sb_append_n(sb, s, strlen(s));
}

static void sb_free(StringBuf *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void fold_var_map_init(FoldVarMap *map) {
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static void fold_var_map_free(FoldVarMap *map) {
    free(map->items);
    map->items = NULL;
    map->len = 0;
    map->cap = 0;
}

static Atom *fold_var_map_lookup(FoldVarMap *map, const char *name) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (strcmp(map->items[i].name, name) == 0)
            return map->items[i].mapped;
    }
    return NULL;
}

static Atom *fold_var_map_add_fresh(FoldVarMap *map, Arena *a, const char *name) {
    Atom *fresh = atom_var(a, arena_tagged_var_name(a, name, fresh_var_suffix()));
    if (map->len >= map->cap) {
        map->cap = map->cap ? map->cap * 2 : 8;
        map->items = cetta_realloc(map->items, sizeof(FoldVarMapEntry) * map->cap);
    }
    map->items[map->len].name = name;
    map->items[map->len].mapped = fresh;
    map->len++;
    return fresh;
}

static Atom *grounded_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++)
        elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *foldl_bind_step_atom(Arena *a, Atom *atom,
                                  const char *acc_name, Atom *acc_val,
                                  const char *item_name, Atom *item_val,
                                  FoldVarMap *fresh_vars) {
    switch (atom->kind) {
    case ATOM_VAR:
        if (strcmp(atom->name, acc_name) == 0)
            return acc_val;
        if (strcmp(atom->name, item_name) == 0)
            return item_val;
        {
            Atom *mapped = fold_var_map_lookup(fresh_vars, atom->name);
            if (mapped)
                return mapped;
            return fold_var_map_add_fresh(fresh_vars, a, atom->name);
        }
    case ATOM_EXPR: {
        Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        bool changed = false;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            elems[i] = foldl_bind_step_atom(a, atom->expr.elems[i],
                                            acc_name, acc_val,
                                            item_name, item_val,
                                            fresh_vars);
            if (elems[i] != atom->expr.elems[i])
                changed = true;
        }
        if (!changed)
            return atom;
        return atom_expr(a, elems, atom->expr.len);
    }
    default:
        return atom;
    }
}

static Atom *grounded_bad_arg_type(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                   int bad_idx, Atom *expected_type, Atom *actual_atom) {
    Atom *actual_type = (actual_atom && actual_atom->kind == ATOM_GROUNDED)
        ? get_grounded_type(a, actual_atom)
        : get_meta_type(a, actual_atom);
    Atom *reason = atom_expr(a, (Atom*[]){
        atom_symbol(a, "BadArgType"),
        atom_int(a, bad_idx),
        expected_type,
        actual_type
    }, 4);
    return atom_error(a, grounded_call_expr(a, head, args, nargs), reason);
}

static Atom *grounded_string_error(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                   const char *message) {
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, message));
}

static Atom *grounded_incorrect_arity(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, "IncorrectNumberOfArguments"));
}

static Atom *grounded_expr_message_error(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                         const char *prefix, Atom *expr) {
    char buf[1024];
    int pos = snprintf(buf, sizeof(buf), "%s", prefix);
    if (pos < 0) pos = 0;
    if ((size_t)pos < sizeof(buf)) {
        FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
        if (tmp) {
            atom_print(expr, tmp);
            fclose(tmp);
        }
    }
    buf[sizeof(buf) - 1] = '\0';
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, buf));
}

static int find_unused_alpha_equal_atom(Atom **elems, bool *used, uint32_t len, Atom *candidate) {
    for (uint32_t i = 0; i < len; i++) {
        if (!used[i] && atom_alpha_eq(elems[i], candidate))
            return (int)i;
    }
    return -1;
}

bool is_grounded_op(const char *name) {
    if (strncmp(name, "__cetta_lib_", 12) == 0) {
        return true;
    }
    return strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
           strcmp(name, "*") == 0 || strcmp(name, "/") == 0 ||
           strcmp(name, "%") == 0 || strcmp(name, "<") == 0 ||
           strcmp(name, ">") == 0 || strcmp(name, "<=") == 0 ||
           strcmp(name, ">=") == 0 || strcmp(name, "==") == 0 ||
           strcmp(name, "=alpha") == 0 ||
           strcmp(name, "if-equal") == 0 ||
           strcmp(name, "sealed") == 0 ||
           strcmp(name, "_minimal-foldl-atom") == 0 ||
           strcmp(name, "_collapse-add-next-atom-from-collapse-bind-result") == 0 ||
           strcmp(name, "foldl-atom-in-space") == 0 ||
           strcmp(name, "and") == 0 || strcmp(name, "or") == 0 ||
           strcmp(name, "not") == 0 || strcmp(name, "xor") == 0 ||
           strcmp(name, "println!") == 0 ||
           strcmp(name, "trace!") == 0 ||
           strcmp(name, "format-args") == 0 ||
           strcmp(name, "py-atom") == 0 ||
           strcmp(name, "py-dot") == 0 ||
           strcmp(name, "py-call") == 0 ||
           strcmp(name, "sort-strings") == 0 ||
           strcmp(name, "print-alternatives!") == 0 ||
           strcmp(name, "unique-atom") == 0 ||
           strcmp(name, "intersection-atom") == 0 ||
           strcmp(name, "subtraction-atom") == 0 ||
           strcmp(name, "max-atom") == 0 ||
           strcmp(name, "min-atom") == 0 ||
           strcmp(name, "pow-math") == 0 ||
           strcmp(name, "sqrt-math") == 0 ||
           strcmp(name, "abs-math") == 0 ||
           strcmp(name, "log-math") == 0 ||
           strcmp(name, "trunc-math") == 0 ||
           strcmp(name, "ceil-math") == 0 ||
           strcmp(name, "floor-math") == 0 ||
           strcmp(name, "round-math") == 0 ||
           strcmp(name, "sin-math") == 0 ||
           strcmp(name, "asin-math") == 0 ||
           strcmp(name, "cos-math") == 0 ||
           strcmp(name, "acos-math") == 0 ||
           strcmp(name, "tan-math") == 0 ||
           strcmp(name, "atan-math") == 0 ||
           strcmp(name, "isnan-math") == 0 ||
           strcmp(name, "isinf-math") == 0 ||
           strcmp(name, "size-atom") == 0 || strcmp(name, "index-atom") == 0;
}

/* ── Numeric arg extraction (int or float, promote to double) ──────────── */

typedef struct {
    double val;
    int64_t ival;
    bool is_float;
} NumArg;

static bool get_numeric_arg(Atom *a, NumArg *out) {
    if (a->kind != ATOM_GROUNDED) return false;
    if (a->ground.gkind == GV_INT) {
        out->val = (double)a->ground.ival;
        out->ival = a->ground.ival;
        out->is_float = false;
        return true;
    }
    if (a->ground.gkind == GV_FLOAT) {
        out->val = a->ground.fval;
        out->ival = 0;
        out->is_float = true;
        return true;
    }
    return false;
}

/* Return int if both inputs were int and result is exact, otherwise float */
static Atom *make_numeric(Arena *a, double val, bool any_float) {
    if (any_float)
        return atom_float(a, val);
    if (!isfinite(val))
        return atom_float(a, val);
    if (val < (double)INT64_MIN || val > (double)INT64_MAX)
        return atom_float(a, val);
    int64_t lv = (int64_t)val;
    if ((double)lv == val)
        return atom_int(a, lv);
    return atom_float(a, val);
}

static Atom *grounded_division_by_zero(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    return atom_error(a, grounded_call_expr(a, head, args, nargs),
                      atom_symbol(a, "DivisionByZero"));
}

static Atom *grounded_math_domain_error(Arena *a, Atom *head, Atom **args,
                                        uint32_t nargs, int bad_idx,
                                        const char *constraint) {
    Atom *reason = atom_expr(a, (Atom *[]){
        atom_symbol(a, "MathDomainError"),
        atom_int(a, bad_idx),
        atom_symbol(a, constraint)
    }, 3);
    return atom_error(a, grounded_call_expr(a, head, args, nargs), reason);
}

static bool numeric_arg_is_integral(const NumArg *arg) {
    if (!arg->is_float) return true;
    if (!isfinite(arg->val)) return false;
    double whole = 0.0;
    return modf(arg->val, &whole) == 0.0;
}

/* ── Boolean arg extraction (True/False symbols) ──────────────────────── */

static bool get_bool_arg(Atom *a, bool *out) {
    if (atom_is_symbol(a, "True"))  { *out = true;  return true; }
    if (atom_is_symbol(a, "False")) { *out = false; return true; }
    if (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_BOOL) {
        *out = a->ground.bval; return true;
    }
    return false;
}

static Atom *grounded_bool_bad_arg(Arena *a, Atom *head, Atom **args, uint32_t nargs,
                                   int bad_idx, Atom *actual) {
    return grounded_bad_arg_type(a, head, args, nargs, bad_idx, atom_symbol(a, "Bool"), actual);
}

static Atom *grounded_format_args(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 2)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (!(args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_STRING))
        return grounded_string_error(a, head, args, nargs,
                                     "format-args expects format string as a first argument and expression as a second argument");
    if (args[1]->kind != ATOM_EXPR)
        return grounded_string_error(a, head, args, nargs, "Atom is not an ExpressionAtom");

    const char *fmt = args[0]->ground.sval;
    Atom *arg_list = args[1];
    StringBuf sb;
    sb_init(&sb);
    uint32_t argi = 0;
    for (const char *p = fmt; *p; ) {
        if (p[0] == '{' && p[1] == '}') {
            if (argi < arg_list->expr.len) {
                char *rendered = atom_to_string(a, arg_list->expr.elems[argi++]);
                sb_append(&sb, rendered);
            }
            p += 2;
            continue;
        }
        sb_append_n(&sb, p, 1);
        p++;
    }
    Atom *out = atom_string(a, sb.buf ? sb.buf : "");
    sb_free(&sb);
    return out;
}

static int grounded_sort_strings_cmp(const void *lhs, const void *rhs) {
    const char *const *a = lhs;
    const char *const *b = rhs;
    return strcmp(*a, *b);
}

static Atom *grounded_sort_strings(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 1)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (args[0]->kind != ATOM_EXPR)
        return grounded_string_error(a, head, args, nargs,
                                     "sort-strings expects expression with strings as a first argument");

    Atom *list = args[0];
    const char **strings = arena_alloc(a, sizeof(const char *) * list->expr.len);
    Atom **sorted = arena_alloc(a, sizeof(Atom *) * list->expr.len);
    for (uint32_t i = 0; i < list->expr.len; i++) {
        Atom *elem = list->expr.elems[i];
        if (!(elem->kind == ATOM_GROUNDED && elem->ground.gkind == GV_STRING)) {
            return grounded_string_error(a, head, args, nargs,
                                         "sort-strings expects expression with strings as a first argument");
        }
        strings[i] = elem->ground.sval;
    }

    qsort(strings, list->expr.len, sizeof(const char *), grounded_sort_strings_cmp);
    for (uint32_t i = 0; i < list->expr.len; i++)
        sorted[i] = atom_string(a, strings[i]);
    return atom_expr(a, sorted, list->expr.len);
}

static Atom *grounded_collapse_add_next(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 2)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (args[0]->kind != ATOM_EXPR)
        return grounded_bad_arg_type(a, head, args, nargs, 1,
                                     atom_expression_type(a), args[0]);

    Atom *pair = args[1];
    if (pair->kind != ATOM_EXPR || pair->expr.len != 2)
        return grounded_string_error(a, head, args, nargs,
                                     "(Atom Bindings) pair is expected as a second argument");

    Bindings bindings;
    if (!bindings_from_atom(pair->expr.elems[1], &bindings))
        return grounded_string_error(a, head, args, nargs,
                                     "(Atom Bindings) pair is expected as a second argument");

    Atom *next_atom = bindings_apply(&bindings, a, pair->expr.elems[0]);
    bindings_free(&bindings);

    Atom *list = args[0];
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (list->expr.len + 1));
    for (uint32_t i = 0; i < list->expr.len; i++)
        elems[i] = list->expr.elems[i];
    elems[list->expr.len] = next_atom;
    return atom_expr(a, elems, list->expr.len + 1);
}

static Atom *grounded_foldl_in_space(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (nargs != 6)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (args[0]->kind != ATOM_EXPR)
        return grounded_bad_arg_type(a, head, args, nargs, 1,
                                     atom_expression_type(a), args[0]);
    if (args[2]->kind != ATOM_VAR)
        return grounded_bad_arg_type(a, head, args, nargs, 3,
                                     atom_variable_type(a), args[2]);
    if (args[3]->kind != ATOM_VAR)
        return grounded_bad_arg_type(a, head, args, nargs, 4,
                                     atom_variable_type(a), args[3]);
    if (!(args[5]->kind == ATOM_GROUNDED && args[5]->ground.gkind == GV_SPACE))
        return grounded_bad_arg_type(a, head, args, nargs, 6,
                                     atom_symbol(a, "SpaceType"), args[5]);

    Atom *list = args[0];
    Atom *init = args[1];
    Atom *acc_var = args[2];
    Atom *item_var = args[3];
    Atom *op_expr = args[4];
    Atom *space = args[5];

    if (list->expr.len == 0)
        return atom_expr2(a, atom_symbol(a, "return"), init);

    Atom *head_item = list->expr.elems[0];
    Atom *tail = atom_expr(a, list->expr.elems + 1, list->expr.len - 1);

    FoldVarMap fresh_vars;
    fold_var_map_init(&fresh_vars);
    Atom *step_op = foldl_bind_step_atom(a, op_expr,
                                         acc_var->name, init,
                                         item_var->name, head_item,
                                         &fresh_vars);
    fold_var_map_free(&fresh_vars);

    char tmp_name[256];
    snprintf(tmp_name, sizeof(tmp_name), "$__foldl_step#%u", fresh_var_suffix());
    Atom *tmp = atom_var(a, tmp_name);

    Atom *metta_args[4] = {
        atom_symbol(a, "metta"),
        step_op,
        atom_undefined_type(a),
        space
    };
    Atom *recur_args[7] = {
        head,
        tail,
        tmp,
        acc_var,
        item_var,
        op_expr,
        space
    };
    Atom *chain_args[4] = {
        atom_symbol(a, "chain"),
        atom_expr(a, metta_args, 4),
        tmp,
        atom_expr2(a, atom_symbol(a, "eval"), atom_expr(a, recur_args, 7))
    };
    return atom_expr(a, chain_args, 4);
}

/* ── Dispatch ──────────────────────────────────────────────────────────── */

Atom *grounded_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    const char *op = head->name;

    if (strcmp(op, "println!") == 0) {
        if (nargs != 1)
            return grounded_incorrect_arity(a, head, args, nargs);
        if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_STRING)
            fputs(args[0]->ground.sval, stdout);
        else
            atom_print(args[0], stdout);
        fputc('\n', stdout);
        fflush(stdout);
        return atom_unit(a);
    }

    if (strcmp(op, "trace!") == 0) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        atom_print(args[0], stderr);
        fputc('\n', stderr);
        fflush(stderr);
        return args[1];
    }

    if (strcmp(op, "format-args") == 0)
        return grounded_format_args(a, head, args, nargs);

    if (strcmp(op, "sort-strings") == 0)
        return grounded_sort_strings(a, head, args, nargs);

    if (strcmp(op, "_collapse-add-next-atom-from-collapse-bind-result") == 0)
        return grounded_collapse_add_next(a, head, args, nargs);

    if (strcmp(op, "_minimal-foldl-atom") == 0 ||
        strcmp(op, "foldl-atom-in-space") == 0)
        return grounded_foldl_in_space(a, head, args, nargs);

    if (strcmp(op, "=alpha") == 0) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        return atom_alpha_eq(args[0], args[1]) ? atom_true(a) : atom_false(a);
    }

    if (strcmp(op, "if-equal") == 0) {
        if (nargs != 4)
            return grounded_incorrect_arity(a, head, args, nargs);
        return atom_alpha_eq(args[0], args[1]) ? args[2] : args[3];
    }

    if (strcmp(op, "sealed") == 0) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        return rename_vars_except(a, args[1], args[0]);
    }

    if (strcmp(op, "print-alternatives!") == 0) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        if (args[1]->kind != ATOM_EXPR)
            return grounded_string_error(a, head, args, nargs, "Atom is not an ExpressionAtom");
        char *label = atom_to_string(a, args[0]);
        printf("%u %s:\n", args[1]->expr.len, label);
        for (uint32_t i = 0; i < args[1]->expr.len; i++) {
            char *rendered = atom_to_string(a, args[1]->expr.elems[i]);
            printf("    %s\n", rendered);
        }
        fflush(stdout);
        return atom_unit(a);
    }

    if (strcmp(op, "pow-math") == 0) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        NumArg base;
        if (!get_numeric_arg(args[0], &base))
            return grounded_string_error(a, head, args, nargs,
                                         "pow-math expects two arguments: number (base) and number (power)");
        NumArg power;
        double power_val;
        if (args[1]->kind == ATOM_GROUNDED && args[1]->ground.gkind == GV_INT) {
            int64_t n = args[1]->ground.ival;
            if (n > INT32_MAX || n < INT32_MIN)
                return grounded_string_error(a, head, args, nargs,
                                             "power argument is too big, try using float value");
            power.ival = n;
            power.val = (double)n;
            power.is_float = false;
            power_val = (double)n;
        } else {
            if (!get_numeric_arg(args[1], &power))
                return grounded_string_error(a, head, args, nargs,
                                             "pow-math expects two arguments: number (base) and number (power)");
            power_val = power.val;
        }
        if (base.val == 0.0 && power.val < 0.0)
            return grounded_math_domain_error(a, head, args, nargs, 1,
                                              "NonZeroBaseWhenExponentNegative");
        if (base.val < 0.0 && !numeric_arg_is_integral(&power))
            return grounded_math_domain_error(a, head, args, nargs, 2,
                                              "IntegralExponentWhenBaseNegative");
        double res = pow(base.val, power_val);
        return atom_float(a, res);
    }

    if (strcmp(op, "log-math") == 0) {
        if (nargs != 2)
            return grounded_incorrect_arity(a, head, args, nargs);
        NumArg base, input;
        if (!get_numeric_arg(args[0], &base) || !get_numeric_arg(args[1], &input))
            return grounded_string_error(a, head, args, nargs,
                                         "log-math expects two arguments: base (number) and input value (number)");
        if (!(base.val > 0.0) || base.val == 1.0)
            return grounded_math_domain_error(a, head, args, nargs, 1,
                                              "PositiveRealNotOne");
        if (!(input.val > 0.0))
            return grounded_math_domain_error(a, head, args, nargs, 2,
                                              "PositiveReal");
        return atom_float(a, log(input.val) / log(base.val));
    }

    if (strcmp(op, "sqrt-math") == 0 || strcmp(op, "abs-math") == 0 ||
        strcmp(op, "trunc-math") == 0 || strcmp(op, "ceil-math") == 0 ||
        strcmp(op, "floor-math") == 0 || strcmp(op, "round-math") == 0 ||
        strcmp(op, "sin-math") == 0 || strcmp(op, "asin-math") == 0 ||
        strcmp(op, "cos-math") == 0 || strcmp(op, "acos-math") == 0 ||
        strcmp(op, "tan-math") == 0 || strcmp(op, "atan-math") == 0 ||
        strcmp(op, "isnan-math") == 0 || strcmp(op, "isinf-math") == 0) {
        if (nargs != 1)
            return grounded_incorrect_arity(a, head, args, nargs);
        NumArg input;
        if (!get_numeric_arg(args[0], &input)) {
            const char *msg = NULL;
            if (strcmp(op, "sqrt-math") == 0)
                msg = "sqrt-math expects one argument: number";
            else if (strcmp(op, "abs-math") == 0)
                msg = "abs-math expects one argument: number";
            else if (strcmp(op, "trunc-math") == 0)
                msg = "trunc-math expects one argument: input number";
            else if (strcmp(op, "ceil-math") == 0)
                msg = "ceil-math expects one argument: input number";
            else if (strcmp(op, "floor-math") == 0)
                msg = "floor-math expects one argument: input number";
            else if (strcmp(op, "round-math") == 0)
                msg = "round-math expects one argument: input number";
            else if (strcmp(op, "sin-math") == 0)
                msg = "sin-math expects one argument: input number";
            else if (strcmp(op, "asin-math") == 0)
                msg = "asin-math expects one argument: input number";
            else if (strcmp(op, "cos-math") == 0)
                msg = "cos-math expects one argument: input number";
            else if (strcmp(op, "acos-math") == 0)
                msg = "acos-math expects one argument: input number";
            else if (strcmp(op, "tan-math") == 0)
                msg = "tan-math expects one argument: input number";
            else if (strcmp(op, "atan-math") == 0)
                msg = "atan-math expects one argument: input number";
            else if (strcmp(op, "isnan-math") == 0)
                msg = "isnan-math expects one argument: input number";
            else
                msg = "isinf-math expects one argument: input number";
            return grounded_string_error(a, head, args, nargs, msg);
        }

        if (strcmp(op, "sqrt-math") == 0) {
            if (input.val < 0.0)
                return grounded_math_domain_error(a, head, args, nargs, 1,
                                                  "NonNegativeReal");
            return atom_float(a, sqrt(input.val));
        }
        if (strcmp(op, "abs-math") == 0) {
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT)
                return atom_int(a, llabs(args[0]->ground.ival));
            return atom_float(a, fabs(input.val));
        }
        if (strcmp(op, "trunc-math") == 0) {
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT)
                return atom_int(a, args[0]->ground.ival);
            return atom_float(a, trunc(input.val));
        }
        if (strcmp(op, "ceil-math") == 0) {
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT)
                return atom_int(a, args[0]->ground.ival);
            return atom_float(a, ceil(input.val));
        }
        if (strcmp(op, "floor-math") == 0) {
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT)
                return atom_int(a, args[0]->ground.ival);
            return atom_float(a, floor(input.val));
        }
        if (strcmp(op, "round-math") == 0) {
            if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_INT)
                return atom_int(a, args[0]->ground.ival);
            return atom_float(a, round(input.val));
        }
        if (strcmp(op, "sin-math") == 0)
            return atom_float(a, sin(input.val));
        if (strcmp(op, "asin-math") == 0) {
            if (input.val < -1.0 || input.val > 1.0)
                return grounded_math_domain_error(a, head, args, nargs, 1,
                                                  "ClosedUnitInterval");
            return atom_float(a, asin(input.val));
        }
        if (strcmp(op, "cos-math") == 0)
            return atom_float(a, cos(input.val));
        if (strcmp(op, "acos-math") == 0) {
            if (input.val < -1.0 || input.val > 1.0)
                return grounded_math_domain_error(a, head, args, nargs, 1,
                                                  "ClosedUnitInterval");
            return atom_float(a, acos(input.val));
        }
        if (strcmp(op, "tan-math") == 0)
            return atom_float(a, tan(input.val));
        if (strcmp(op, "atan-math") == 0)
            return atom_float(a, atan(input.val));
        if (strcmp(op, "isnan-math") == 0)
            return isnan(input.val) ? atom_true(a) : atom_false(a);
        return isinf(input.val) ? atom_true(a) : atom_false(a);
    }

    if (strcmp(op, "max-atom") == 0 || strcmp(op, "min-atom") == 0) {
        bool want_max = strcmp(op, "max-atom") == 0;
        if (nargs != 1)
            return grounded_incorrect_arity(a, head, args, nargs);
        if (args[0]->kind != ATOM_EXPR)
            return grounded_string_error(a, head, args, nargs,
                                         "Atom is not an ExpressionAtom");
        if (args[0]->expr.len == 0)
            return grounded_string_error(a, head, args, nargs, "Empty expression");

        double acc = want_max ? -INFINITY : INFINITY;
        for (uint32_t i = 0; i < args[0]->expr.len; i++) {
            NumArg n;
            if (!get_numeric_arg(args[0]->expr.elems[i], &n))
                return grounded_expr_message_error(
                    a, head, args, nargs,
                    "Only numbers are allowed in expression: ",
                    args[0]);
            acc = want_max ? fmax(acc, n.val) : fmin(acc, n.val);
        }
        return atom_float(a, acc);
    }

    /* ── Expression introspection ─────────────────────────────────────── */
    if (strcmp(op, "size-atom") == 0 && nargs == 1) {
        if (args[0]->kind == ATOM_EXPR)
            return atom_int(a, args[0]->expr.len);
        if (args[0]->kind == ATOM_GROUNDED)
            return grounded_bad_arg_type(a, head, args, nargs, 1,
                                         atom_expression_type(a), args[0]);
        return NULL;
    }

    if (strcmp(op, "index-atom") == 0 && nargs == 2) {
        if (args[0]->kind != ATOM_EXPR) {
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_expression_type(a), args[0]);
            return NULL;
        }
        if (args[1]->kind != ATOM_GROUNDED || args[1]->ground.gkind != GV_INT) {
            if (args[1]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 2,
                                             atom_symbol(a, "Number"), args[1]);
            return NULL;
        }
        int64_t idx = args[1]->ground.ival;
        if (idx < 0 || (uint64_t)idx >= args[0]->expr.len)
            return atom_error(a, grounded_call_expr(a, head, args, nargs),
                              atom_string(a, "Index is out of bounds"));
        return args[0]->expr.elems[idx];
    }

    if (strcmp(op, "unique-atom") == 0 && nargs == 1) {
        if (args[0]->kind != ATOM_EXPR) {
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_expression_type(a), args[0]);
            return NULL;
        }
        Atom **uniq = arena_alloc(a, sizeof(Atom *) * args[0]->expr.len);
        uint32_t out_len = 0;
        for (uint32_t i = 0; i < args[0]->expr.len; i++) {
            Atom *candidate = args[0]->expr.elems[i];
            bool seen = false;
            for (uint32_t j = 0; j < out_len; j++) {
                if (atom_alpha_eq(uniq[j], candidate)) {
                    seen = true;
                    break;
                }
            }
            if (!seen)
                uniq[out_len++] = candidate;
        }
        return atom_expr(a, uniq, out_len);
    }

    if (strcmp(op, "intersection-atom") == 0 && nargs == 2) {
        if (args[0]->kind != ATOM_EXPR || args[1]->kind != ATOM_EXPR) {
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_expression_type(a), args[0]);
            if (args[1]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 2,
                                             atom_expression_type(a), args[1]);
            return NULL;
        }
        Atom **out = arena_alloc(a, sizeof(Atom *) * args[0]->expr.len);
        bool *rhs_used = arena_alloc(a, sizeof(bool) * args[1]->expr.len);
        memset(rhs_used, 0, sizeof(bool) * args[1]->expr.len);
        uint32_t out_len = 0;
        for (uint32_t i = 0; i < args[0]->expr.len; i++) {
            Atom *candidate = args[0]->expr.elems[i];
            int match_idx = find_unused_alpha_equal_atom(args[1]->expr.elems, rhs_used,
                                                         args[1]->expr.len, candidate);
            if (match_idx >= 0) {
                rhs_used[match_idx] = true;
                out[out_len++] = candidate;
            }
        }
        return atom_expr(a, out, out_len);
    }

    if (strcmp(op, "subtraction-atom") == 0 && nargs == 2) {
        if (args[0]->kind != ATOM_EXPR || args[1]->kind != ATOM_EXPR) {
            if (args[0]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 1,
                                             atom_expression_type(a), args[0]);
            if (args[1]->kind == ATOM_GROUNDED)
                return grounded_bad_arg_type(a, head, args, nargs, 2,
                                             atom_expression_type(a), args[1]);
            return NULL;
        }
        Atom **out = arena_alloc(a, sizeof(Atom *) * args[0]->expr.len);
        bool *rhs_used = arena_alloc(a, sizeof(bool) * args[1]->expr.len);
        memset(rhs_used, 0, sizeof(bool) * args[1]->expr.len);
        uint32_t out_len = 0;
        for (uint32_t i = 0; i < args[0]->expr.len; i++) {
            Atom *candidate = args[0]->expr.elems[i];
            int match_idx = find_unused_alpha_equal_atom(args[1]->expr.elems, rhs_used,
                                                         args[1]->expr.len, candidate);
            if (match_idx >= 0) {
                rhs_used[match_idx] = true;
                continue;
            }
            out[out_len++] = candidate;
        }
        return atom_expr(a, out, out_len);
    }

    /* ── Structural equality (any atom type) ───────────────────────────── */
    if (strcmp(op, "==") == 0 && nargs == 2) {
        return atom_eq(args[0], args[1]) ? atom_true(a) : atom_false(a);
    }

    /* ── Boolean ops ───────────────────────────────────────────────────── */
    if (strcmp(op, "not") == 0) {
        if (nargs != 1)
            return grounded_incorrect_arity(a, head, args, nargs);
        bool bv;
        if (get_bool_arg(args[0], &bv))
            return bv ? atom_false(a) : atom_true(a);
        if (args[0]->kind == ATOM_GROUNDED)
            return grounded_bool_bad_arg(a, head, args, nargs, 1, args[0]);
        return NULL;
    }
    if ((strcmp(op, "and") == 0 || strcmp(op, "or") == 0 || strcmp(op, "xor") == 0) && nargs != 2)
        return grounded_incorrect_arity(a, head, args, nargs);
    if (nargs == 2) {
        bool bx, by;
        if (strcmp(op, "and") == 0 || strcmp(op, "or") == 0 || strcmp(op, "xor") == 0) {
            bool okx = get_bool_arg(args[0], &bx);
            bool oky = get_bool_arg(args[1], &by);
            if (okx && oky) {
                if (strcmp(op, "and") == 0)
                    return (bx && by) ? atom_true(a) : atom_false(a);
                if (strcmp(op, "or") == 0)
                    return (bx || by) ? atom_true(a) : atom_false(a);
                return (bx != by) ? atom_true(a) : atom_false(a);
            }
            if (!okx && args[0]->kind == ATOM_GROUNDED)
                return grounded_bool_bad_arg(a, head, args, nargs, 1, args[0]);
            if (!oky && args[1]->kind == ATOM_GROUNDED)
                return grounded_bool_bad_arg(a, head, args, nargs, 2, args[1]);
            return NULL;
        }
    }

    /* ── Numeric ops ───────────────────────────────────────────────────── */
    if (nargs != 2) return NULL;

    /* Check if this is an arithmetic op that expects numeric args */
    bool is_arith = (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
                     strcmp(op, "*") == 0 || strcmp(op, "/") == 0 ||
                     strcmp(op, "%") == 0 || strcmp(op, "<") == 0 ||
                     strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
                     strcmp(op, ">=") == 0);
    NumArg na = {0, false}, nb = {0, false};
    bool na_ok = get_numeric_arg(args[0], &na);
    bool nb_ok = get_numeric_arg(args[1], &nb);
    if (is_arith && (!na_ok || !nb_ok)) {
        /* Only produce BadArgType for grounded non-numeric args (like strings).
           For symbols and variables, return NULL (expression unchanged) —
           matches HE behavior where type-checker handles symbols. */
        Atom *bad_arg = !na_ok ? args[0] : args[1];
        if (bad_arg->kind == ATOM_GROUNDED) {
            return grounded_bad_arg_type(a, head, args, nargs,
                                         !na_ok ? 1 : 2,
                                         atom_symbol(a, "Number"),
                                         bad_arg);
        }
        return NULL; /* Symbol/variable args → return unchanged */
    }
    if (!na_ok || !nb_ok)
        return NULL;
    /* Both args are numeric from here */
    bool fl = na.is_float || nb.is_float;

    if (!fl) {
        int64_t ai = na.ival;
        int64_t bi = nb.ival;

        if (strcmp(op, "+") == 0) {
            __int128 sum = (__int128)ai + (__int128)bi;
            if (sum >= INT64_MIN && sum <= INT64_MAX)
                return atom_int(a, (int64_t)sum);
            return atom_float(a, (double)ai + (double)bi);
        }
        if (strcmp(op, "-") == 0) {
            __int128 diff = (__int128)ai - (__int128)bi;
            if (diff >= INT64_MIN && diff <= INT64_MAX)
                return atom_int(a, (int64_t)diff);
            return atom_float(a, (double)ai - (double)bi);
        }
        if (strcmp(op, "*") == 0) {
            __int128 prod = (__int128)ai * (__int128)bi;
            if (prod >= INT64_MIN && prod <= INT64_MAX)
                return atom_int(a, (int64_t)prod);
            return atom_float(a, (double)ai * (double)bi);
        }
        if (strcmp(op, "/") == 0) {
            if (bi == 0)
                return grounded_division_by_zero(a, head, args, nargs);
            if (ai % bi == 0)
                return atom_int(a, ai / bi);
            return atom_float(a, (double)ai / (double)bi);
        }
        if (strcmp(op, "%") == 0) {
            if (bi == 0)
                return grounded_division_by_zero(a, head, args, nargs);
            return atom_int(a, ai % bi);
        }
        if (strcmp(op, "<") == 0)  return ai < bi  ? atom_true(a) : atom_false(a);
        if (strcmp(op, ">") == 0)  return ai > bi  ? atom_true(a) : atom_false(a);
        if (strcmp(op, "<=") == 0) return ai <= bi ? atom_true(a) : atom_false(a);
        if (strcmp(op, ">=") == 0) return ai >= bi ? atom_true(a) : atom_false(a);
    }

    if (strcmp(op, "+") == 0) return make_numeric(a, na.val + nb.val, fl);
    if (strcmp(op, "-") == 0) return make_numeric(a, na.val - nb.val, fl);
    if (strcmp(op, "*") == 0) return make_numeric(a, na.val * nb.val, fl);
    if (strcmp(op, "/") == 0) return nb.val != 0 ? make_numeric(a, na.val / nb.val, fl)
                                                 : atom_float(a, na.val / nb.val);
    if (strcmp(op, "%") == 0) return nb.val != 0 ? make_numeric(a, fmod(na.val, nb.val), fl)
                                                 : grounded_division_by_zero(a, head, args, nargs);
    if (strcmp(op, "<") == 0)  return na.val < nb.val  ? atom_true(a) : atom_false(a);
    if (strcmp(op, ">") == 0)  return na.val > nb.val  ? atom_true(a) : atom_false(a);
    if (strcmp(op, "<=") == 0) return na.val <= nb.val ? atom_true(a) : atom_false(a);
    if (strcmp(op, ">=") == 0) return na.val >= nb.val ? atom_true(a) : atom_false(a);

    return NULL;
}

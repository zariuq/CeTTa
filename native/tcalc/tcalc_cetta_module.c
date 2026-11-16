/* tcalc_cetta_module.c - tcalc as a module of MeTTa: (import! &self tcalc).
 *
 *   (tcalc:load PATH)                      the program of a file, in any of the
 *                                          three notations, as an expression of its
 *                                          forms in the canonical form
 *   (tcalc:outcomes PROGRAM PROFILE)       a run, as `cetta --lang tcalc` prints it:
 *                                          per interpretation its line, then
 *                                          (outcome ROW ...), (ambiguous ...) and
 *                                          (explored ...)
 *   (tcalc:step PROGRAM PROFILE STATE)     one step from a state, an expression of
 *                                          rows: (successor ROW ...) per state reached
 *   (tcalc:query PROGRAM PROFILE PATTERN)  in each outcome, the rows a read of
 *                                          PATTERN can select:
 *                                          (answers (outcome ROW ...) ROW ...)
 *
 * PROGRAM is an expression of the program's forms as a .tcalc file writes them,
 * ((step one) (token 1) (rule a ...)), so a program is MeTTa data that MeTTa
 * can build and take apart.  PROFILE is a string of axis settings, "" for none.
 * A variable in a row is an unknown of the state; an unknown in an answer is
 * the variable $uN.  A refusal is an Error holding the runner's message.
 *
 * Every operation runs the same runner as the command line, on the canonical
 * text of its arguments, so a program answers here as it does there. */
#define _POSIX_C_SOURCE 200809L   /* open_memstream */

#include "tcalc_cetta_module.h"
#include "tcalc_cli.h"
#include "tcalc_surface.h"
#include "tcalc_syntax.h"
#include "parser.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TCALC_HEAD "__cetta_lib_tcalc_"

static bool is_string(const Atom *x) {
    return x && x->kind == ATOM_GROUNDED && x->ground.gkind == GV_STRING;
}

/* A form as the runner reads it.  A variable is written by its identity,
 * $vBASE_EPOCH: MeTTa prints a variable by its name, and two variables
 * renamed by one let or call can print alike, but no two share an identity.
 * Nesting is as deep as the reader's own (tcalc_syntax.c reads recursively). */
static void print_form(Atom *x, FILE *f) {
    if (x->kind == ATOM_VAR) {
        fprintf(f, "$v%" PRIu64 "_%" PRIu64, x->var_id.base, x->var_id.epoch.word);
    } else if (x->kind == ATOM_EXPR) {
        fputc('(', f);
        for (CettaExprLen i = 0; i < x->expr.len; i++) {
            if (i) fputc(' ', f);
            print_form(x->expr.elems[i], f);
        }
        fputc(')', f);
    } else {
        atom_print(x, f);
    }
}

/* Text the runner reads: each item of an expression on a line of its own,
 * each written as (put ITEM) when `rows`. */
static char *lines_of(Atom *items, bool rows, size_t *length) {
    char *buf = NULL;
    *length = 0;
    FILE *f = open_memstream(&buf, length);
    if (!f) return NULL;
    for (CettaExprLen i = 0; i < items->expr.len; i++) {
        if (rows) fputs("(put ", f);
        print_form(items->expr.elems[i], f);
        fputs(rows ? ")\n" : "\n", f);
    }
    fclose(f);
    return buf;
}

static char *text_of(Atom *x, size_t *length) {
    char *buf = NULL;
    *length = 0;
    FILE *f = open_memstream(&buf, length);
    if (!f) return NULL;
    print_form(x, f);
    fclose(f);
    return buf;
}

/* The runner's output as atoms, one per line.  An unknown, which tcalc prints
 * ?N, is written as the variable $uN, so that rows read back as a state keep
 * their unknowns. */
static Atom *output_atoms(Arena *a, const char *text) {
    size_t n = strlen(text);
    char *rewritten = malloc(2 * n + 1);
    if (!rewritten) return NULL;
    char *w = rewritten;
    for (size_t i = 0; i < n; i++) {
        bool starts = text[i] == '?' && i + 1 < n && isdigit((unsigned char)text[i + 1]) &&
                      (i == 0 || text[i - 1] == ' ' || text[i - 1] == '(');
        if (starts) { *w++ = '$'; *w++ = 'u'; continue; }
        *w++ = text[i];
    }
    *w = 0;
    Atom **atoms = NULL;
    int count = parse_metta_text(rewritten, a, &atoms);
    free(rewritten);
    if (count < 0) { free(atoms); return NULL; }
    Atom **kept = arena_alloc(a, sizeof(Atom *) * (size_t)(count ? count : 1));
    for (int i = 0; i < count; i++) kept[i] = atoms[i];
    free(atoms);
    return atom_expr(a, kept, (CettaExprLen)count);
}

/* (Error (tcalc:OPERATION ARG ...) MESSAGE), naming the operation as lib/tcalc.metta does. */
static Atom *refusal(Arena *a, Atom *head, Atom **args, uint32_t nargs, const char *message) {
    Atom **items = arena_alloc(a, sizeof(Atom *) * (nargs + 1u));
    char public_name[64];
    snprintf(public_name, sizeof public_name, "tcalc:%s", atom_name_cstr(head) + strlen(TCALC_HEAD));
    items[0] = atom_symbol(a, public_name);
    for (uint32_t i = 0; i < nargs; i++) items[i + 1] = args[i];
    size_t n = strlen(message);
    char *trimmed = arena_alloc(a, n + 1);
    memcpy(trimmed, message, n + 1);
    while (n > 0 && trimmed[n - 1] == '\n') trimmed[--n] = 0;
    return atom_error(a, atom_expr(a, items, nargs + 1u), atom_string(a, trimmed));
}

/* The program of a file, in the notation its suffix names, as canonical text. */
static char *load_text(const char *path, char *message, size_t message_size) {
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(message, message_size, "error: cannot open %s", path); return NULL; }
    char *text = NULL;
    size_t length = 0, cap = 0;
    for (;;) {
        if (length + 1 >= cap) {
            cap = cap ? cap * 2 : 65536;
            char *g = realloc(text, cap);
            if (!g) { free(text); fclose(f); snprintf(message, message_size, "error: out of memory"); return NULL; }
            text = g;
        }
        size_t got = fread(text + length, 1, cap - length - 1, f);
        length += got;
        if (got == 0) break;
    }
    fclose(f);
    text[length] = 0;
    TcSurface surface = tc_surface_of_path(path);
    if (surface == TC_SURFACE_TCALC) return text;
    TcCanonical c;
    TcSyntaxError e;
    bool ok = tc_surface_translate(surface, text, length, &c, &e);
    free(text);
    if (!ok) {
        snprintf(message, message_size, "error: %zu:%zu: %s", e.line, e.column, e.message);
        return NULL;
    }
    char *canonical = malloc(c.length + 1);
    if (canonical) { memcpy(canonical, c.text, c.length); canonical[c.length] = 0; }
    tc_canonical_free(&c);
    return canonical;
}

typedef enum { OP_LOAD, OP_OUTCOMES, OP_STEP, OP_QUERY } Operation;

bool cetta_tcalc_module_call(struct CettaLibraryContext *ctx, Space *space, Arena *a,
                             Atom *head, Atom **args, uint32_t nargs, CettaCallOutcome *out) {
    (void)ctx; (void)space;
    if (!head || head->kind != ATOM_SYMBOL) return false;
    const char *name = atom_name_cstr(head);
    if (strncmp(name, TCALC_HEAD, strlen(TCALC_HEAD)) != 0) return false;
    name += strlen(TCALC_HEAD);
    Operation op;
    uint32_t arity;
    if (strcmp(name, "load") == 0) op = OP_LOAD, arity = 1;
    else if (strcmp(name, "outcomes") == 0) op = OP_OUTCOMES, arity = 2;
    else if (strcmp(name, "step") == 0) op = OP_STEP, arity = 3;
    else if (strcmp(name, "query") == 0) op = OP_QUERY, arity = 3;
    else return false;

    char message[512] = {0};
    if (nargs != arity) {
        snprintf(message, sizeof message, "tcalc:%s takes %u arguments", name, arity);
        *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
        return true;
    }
    if (op == OP_LOAD) {
        Atom *result = NULL;
        if (!is_string(args[0])) {
            snprintf(message, sizeof message, "tcalc:load takes the path of a program as a string");
        } else {
            char *text = load_text(args[0]->ground.sval, message, sizeof message);
            TcTerms *terms = text ? tc_terms_new() : NULL;
            TcDeclarations d;
            TcSyntaxError e;
            TcProgram *p = terms ? tc_read_program(terms, text, strlen(text), &d, &e) : NULL;
            if (p) {
                free(d.region);
                result = output_atoms(a, text);
            } else if (text) {
                snprintf(message, sizeof message, "error: %zu:%zu: %s", e.line, e.column, e.message);
            }
            tc_program_free(p);
            tc_terms_free(terms);
            free(text);
        }
        *out = cetta_call_completed_value(result ? result : refusal(a, head, args, nargs, message));
        return true;
    }

    /* the program and the profile, as the runner reads them */
    if (args[0]->kind != ATOM_EXPR || !is_string(args[1])) {
        snprintf(message, sizeof message,
                 "tcalc:%s takes a program, an expression of its forms, and a profile string", name);
        *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
        return true;
    }
    if (op == OP_STEP && args[2]->kind != ATOM_EXPR) {
        snprintf(message, sizeof message, "tcalc:step takes a state, an expression of rows");
        *out = cetta_call_completed_value(refusal(a, head, args, nargs, message));
        return true;
    }
    size_t program_length = 0, third_length = 0;
    char *program = lines_of(args[0], false, &program_length);
    char *third = op == OP_STEP ? lines_of(args[2], true, &third_length)
                : op == OP_QUERY ? text_of(args[2], &third_length) : NULL;
    const char *profile = args[1]->ground.sval;
    char *result_text = NULL, *error_text = NULL;
    size_t result_length = 0, error_length = 0;
    FILE *result = open_memstream(&result_text, &result_length);
    FILE *error = open_memstream(&error_text, &error_length);
    int status = 1;
    if (program && result && error && (op == OP_OUTCOMES || third)) {
        if (op == OP_OUTCOMES)
            status = tcalc_run_text(program, program_length, "tcalc", profile, NULL, result, error);
        else if (op == OP_STEP)
            status = tcalc_step_text(program, program_length, third, third_length, profile, result, error);
        else
            status = tcalc_query_text(program, program_length, third, third_length, profile, result, error);
    }
    if (result) fclose(result);
    if (error) fclose(error);
    Atom *value = status == 0 && result_text ? output_atoms(a, result_text) : NULL;
    if (!value)
        value = refusal(a, head, args, nargs,
                        error_text && error_text[0] ? error_text : "error: out of memory");
    free(program); free(third); free(result_text); free(error_text);
    *out = cetta_call_completed_value(value);
    return true;
}

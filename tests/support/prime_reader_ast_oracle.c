#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atom.h"
#include "parser.h"
#include "symbol.h"

static void print_code_units(FILE *out, const char *text) {
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i++) {
        fputs("(cons (cp ", out);
        fprintf(out, "%u", (unsigned)(unsigned char)text[i]);
        fputs(") ", out);
    }
    fputs("nil", out);
    for (size_t i = 0; i < len; i++)
        fputc(')', out);
}

static void print_reader_atom(FILE *out, Atom *atom);

static void print_reader_atoms(FILE *out, Atom **atoms, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        fputs("(cons ", out);
        print_reader_atom(out, atoms[i]);
        fputc(' ', out);
    }
    fputs("nil", out);
    for (uint32_t i = 0; i < count; i++)
        fputc(')', out);
}

static void print_symbol(FILE *out, const char *text) {
    fputs("(symbol ", out);
    print_code_units(out, text);
    fputc(')', out);
}

static void print_number(FILE *out, const char *kind, const char *text) {
    fprintf(out, "(number %s ", kind);
    print_code_units(out, text);
    fputc(')', out);
}

static void print_reader_atom(FILE *out, Atom *atom) {
    char buf[96];
    switch (atom->kind) {
    case ATOM_SYMBOL:
        print_symbol(out, atom_name_cstr(atom));
        return;
    case ATOM_VAR:
        if (atom->name_key) {
            fputs("(structural-variable ", out);
            print_reader_atom(out, atom->name_key);
            fputc(')', out);
        } else {
            fputs("(variable ", out);
            print_code_units(out, atom_name_cstr(atom));
            fputc(')', out);
        }
        return;
    case ATOM_EXPR:
        fputs("(expression ", out);
        print_reader_atoms(out, atom->expr.elems, atom->expr.len);
        fputc(')', out);
        return;
    case ATOM_GROUNDED:
        break;
    }

    switch (atom->ground.gkind) {
    case GV_STRING:
        fputs("(string ", out);
        print_code_units(out, atom->ground.sval);
        fputc(')', out);
        return;
    case GV_INT:
        snprintf(buf, sizeof buf, "%ld", (long)atom->ground.ival);
        print_number(out, "integer", buf);
        return;
    case GV_FLOAT:
        cetta_format_float(buf, sizeof buf, atom->ground.fval);
        print_number(out, "float", buf);
        return;
    case GV_BIGINT:
        print_number(out, "integer", atom_bigint_cstr(atom));
        return;
    case GV_RATIONAL:
        print_number(out, "rational", atom_rational_cstr(atom));
        return;
    case GV_BOOL:
        print_symbol(out, atom->ground.bval ? "True" : "False");
        return;
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_FOREIGN:
        fputs("(unsupported-grounded)", out);
        return;
    }
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char *text = malloc((size_t)size + 1u);
    if (!text) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(text, 1u, (size_t)size, file);
    fclose(file);
    text[read] = '\0';
    return text;
}

int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[1], "--input-text") != 0 &&
                      strcmp(argv[1], "--input-file") != 0)) {
        fputs("usage: prime_reader_ast_oracle (--input-text TEXT | --input-file FILE)\n",
              stderr);
        return 2;
    }

    char *owned_text = NULL;
    const char *text = argv[2];
    if (strcmp(argv[1], "--input-file") == 0) {
        owned_text = read_file(argv[2]);
        if (!owned_text) {
            fputs("{\"outcome\":\"EngineFailure\"}\n", stdout);
            return 3;
        }
        text = owned_text;
    }

    SymbolTable symbols;
    VarInternTable vars;
    HashConsTable hashcons;
    Arena arena;
    Atom **atoms = NULL;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&vars);
    hashcons_init(&hashcons);
    arena_init(&arena);
    g_symbols = &symbols;
    g_var_intern = &vars;
    g_hashcons = &hashcons;
    arena_set_hashcons(&arena, &hashcons);
    parser_set_universal_name_syntax_enabled(true);

    int count = parse_metta_text(text, &arena, &atoms);
    if (count < 0) {
        fputs("{\"outcome\":\"NoParse\",\"trees\":0,\"asts\":[]}\n", stdout);
    } else {
        fputs("{\"outcome\":\"Unique\",\"trees\":1,\"asts\":[\"(document ", stdout);
        print_reader_atoms(stdout, atoms, (uint32_t)count);
        fputs(")\"]}\n", stdout);
    }

    free(atoms);
    free(owned_text);
    parser_set_universal_name_syntax_enabled(false);
    g_hashcons = NULL;
    g_var_intern = NULL;
    g_symbols = NULL;
    arena_free(&arena);
    hashcons_free(&hashcons);
    var_intern_free(&vars);
    symbol_table_free(&symbols);
    return 0;
}

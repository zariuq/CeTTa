#include "atom.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned char *read_file(const char *path, size_t *length) {
    FILE *input = fopen(path, "rb");
    unsigned char *buffer;
    long end;
    size_t count;
    if (!input || fseek(input, 0, SEEK_END) != 0) {
        if (input) fclose(input);
        return NULL;
    }
    end = ftell(input);
    if (end < 0 || fseek(input, 0, SEEK_SET) != 0) {
        fclose(input);
        return NULL;
    }
    buffer = malloc((size_t)end + 1);
    if (!buffer) {
        fclose(input);
        return NULL;
    }
    count = fread(buffer, 1, (size_t)end, input);
    fclose(input);
    if (count != (size_t)end) {
        free(buffer);
        return NULL;
    }
    buffer[count] = '\0';
    *length = count;
    return buffer;
}

static void print_hex(const unsigned char *bytes) {
    while (*bytes) {
        printf("%02x", (unsigned int)*bytes);
        bytes++;
    }
}

int main(int argc, char **argv) {
    Arena arena;
    SymbolTable symbols;
    VarInternTable variables;
    unsigned char *source;
    size_t length = 0;
    size_t position = 0;
    Atom *atom;

    if (argc != 2) {
        fprintf(stderr, "usage: cetta-parser-oracle-v1 INPUT\n");
        return 2;
    }
    source = read_file(argv[1], &length);
    if (!source) {
        fprintf(stderr, "cannot read input\n");
        return 2;
    }

    arena_init(&arena);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;

    atom = parse_sexpr(&arena, (const char *)source, &position);
    if (atom && position == length && atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING) {
        printf("accepted\t");
        print_hex((const unsigned char *)atom->ground.sval);
        putchar('\n');
    } else {
        puts("rejected\t");
    }

    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    arena_free(&arena);
    free(source);
    return 0;
}

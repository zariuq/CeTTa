#include "atom.h"
#include "parser.h"
#include "petta_typecheck_v3.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CettaPettaTypecheckV3Policy parse_policy(const char *name, bool *ok) {
    if (ok)
        *ok = true;
    if (strcmp(name, "default") == 0)
        return CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT;
    if (strcmp(name, "strict") == 0)
        return CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT;
    if (strcmp(name, "strict-det") == 0)
        return CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT_DET;
    if (ok)
        *ok = false;
    return CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT;
}

static void print_field(const char *text) {
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    while (*cursor) {
        if (*cursor == '\t') {
            fputs("\\t", stdout);
        } else if (*cursor == '\n') {
            fputs("\\n", stdout);
        } else if (*cursor == '\r') {
            fputs("\\r", stdout);
        } else if (*cursor == '\\') {
            fputs("\\\\", stdout);
        } else {
            fputc((int)*cursor, stdout);
        }
        cursor++;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s POLICY FILE.metta\n", argv[0]);
        return 2;
    }
    bool policy_ok = false;
    CettaPettaTypecheckV3Policy policy = parse_policy(argv[1], &policy_ok);
    if (!policy_ok) {
        fprintf(stderr, "unknown typecheck-v3 policy: %s\n", argv[1]);
        return 2;
    }

    SymbolTable symbols;
    VarInternTable variables;
    Arena source;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    arena_init(&source);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = &variables;

    Atom **forms = NULL;
    int form_count = parse_metta_file(argv[2], &source, &forms);
    PettaProgram *program = petta_program_new();
    Space space;
    bool space_initialized = false;
    CettaPettaTypecheckV3 *checker = NULL;
    CettaPettaTypecheckV3BlockResult result = {0};
    char error[512] = {0};
    bool ready = form_count >= 0 && program &&
        petta_program_enable_analysis(program);
    if (ready) {
        space_init(&space);
        space_initialized = true;
        checker = cetta_petta_typecheck_v3_create(error, sizeof error);
        ready = checker != NULL;
    }
    bool checked = ready && cetta_petta_typecheck_v3_declaration_block(
        checker, program, &space, forms, (size_t)form_count,
        policy, &result);

    if (checked) {
        fputs("PettaTypecheckV3FileV1\t", stdout);
        fputs(cetta_petta_typecheck_v3_verdict_name(result.verdict), stdout);
        fputc('\t', stdout);
        fputs(cetta_petta_typecheck_v3_boundary_name(result.boundary), stdout);
        fputc('\t', stdout);
        print_field(result.relation);
        fputc('\t', stdout);
        print_field(result.subject);
        printf("\t%u\t%u\t%u\t%u\t%u\t",
               result.declarations_seen,
               result.equations_checked,
               result.established_equations,
               result.undetermined_equations,
               result.incomplete_equations);
        print_field(result.diagnostic);
        fputc('\n', stdout);
    } else {
        fprintf(stderr, "typecheck-v3 file fault: %s\n",
                result.diagnostic[0] ? result.diagnostic :
                error[0] ? error : "could not initialize the checker");
    }

    cetta_petta_typecheck_v3_free(checker);
    if (space_initialized)
        space_free(&space);
    petta_program_free(program);
    free(forms);
    g_var_intern = NULL;
    g_symbols = NULL;
    arena_free(&source);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    return checked ? 0 : 1;
}

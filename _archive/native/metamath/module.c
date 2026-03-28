#include "native/metamath/module.h"

#include "library.h"
#include "native_handle.h"
#include "native/metamath/parser.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CETTA_METAMATH_HANDLE_KIND "metamath/parser"

static Atom *module_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static Atom *module_signature_error(Arena *a, Atom *head, Atom **args,
                                    uint32_t nargs, const char *message) {
    return atom_error(a, module_call_expr(a, head, args, nargs),
                      atom_symbol(a, message));
}

static const char *module_filename_arg(Atom *arg) {
    if (!arg) return NULL;
    if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_STRING) {
        return arg->ground.sval;
    }
    if (arg->kind == ATOM_SYMBOL) {
        return atom_name_cstr(arg);
    }
    return NULL;
}

static bool module_cli_has_flag(const CettaLibraryContext *ctx, const char *flag) {
    if (!ctx || !flag) return false;
    for (uint32_t i = 0; i < ctx->cmdline_arg_len; i++) {
        if (ctx->cmdline_args[i] && strcmp(ctx->cmdline_args[i], flag) == 0) {
            return true;
        }
    }
    return false;
}

static bool module_env_var_true(const char *name) {
    const char *value = getenv(name);
    return value && strcmp(value, "true") == 0;
}

static bool metamath_use_dag_format(const CettaLibraryContext *ctx) {
    return module_env_var_true("MM_COMPRESSED_DAG") ||
           module_cli_has_flag(ctx, "--compressed-dag");
}

static Atom *metamath_read_tokens(const CettaLibraryContext *ctx, Arena *a,
                                  Atom *head, Atom **args, uint32_t nargs) {
    const char *fname;
    char errbuf[128];
    Atom *result;

    (void)ctx;
    if (nargs != 1) return NULL;
    fname = module_filename_arg(args[0]);
    if (!fname) {
        return module_signature_error(a, head, args, nargs, "expected filename");
    }
    result = cetta_metamath_read_tokens_file(a, fname, errbuf, sizeof(errbuf));
    if (!result) {
        return atom_error(a, module_call_expr(a, head, args, nargs),
                          atom_symbol(a, errbuf));
    }
    return result;
}

static Atom *metamath_parse_file_mode(CettaLibraryContext *ctx, Arena *a,
                                      Atom *head, Atom **args, uint32_t nargs,
                                      bool pverify_shape) {
    const char *fname;
    char errbuf[128];
    Atom *result;

    if (nargs != 1) return NULL;
    fname = module_filename_arg(args[0]);
    if (!fname) {
        return module_signature_error(a, head, args, nargs, "expected filename");
    }
    result = cetta_metamath_collect_file(a, fname, pverify_shape,
                                         metamath_use_dag_format(ctx),
                                         errbuf, sizeof(errbuf));
    if (!result) {
        return atom_error(a, module_call_expr(a, head, args, nargs),
                          atom_symbol(a, errbuf));
    }
    return result;
}

static Atom *metamath_open_file_mode(CettaLibraryContext *ctx, Arena *a,
                                     Atom *head, Atom **args, uint32_t nargs,
                                     bool pverify_shape) {
    const char *fname;
    char errbuf[128];
    CettaMetamathParser *parser;
    uint64_t id;

    if (!ctx || nargs != 1) return NULL;
    fname = module_filename_arg(args[0]);
    if (!fname) {
        return module_signature_error(a, head, args, nargs, "expected filename");
    }
    parser = cetta_metamath_parser_open(fname, pverify_shape,
                                        metamath_use_dag_format(ctx),
                                        errbuf, sizeof(errbuf));
    if (!parser) {
        return atom_error(a, module_call_expr(a, head, args, nargs),
                          atom_symbol(a, errbuf));
    }
    if (!cetta_native_handle_alloc(ctx, CETTA_METAMATH_HANDLE_KIND, parser,
                                   (CettaNativeHandleFreeFn)cetta_metamath_parser_close,
                                   &id)) {
        cetta_metamath_parser_close(parser);
        return atom_error(a, module_call_expr(a, head, args, nargs),
                          atom_symbol(a, "too many native handles"));
    }
    return cetta_native_handle_atom(a, CETTA_METAMATH_HANDLE_KIND, id);
}

static Atom *metamath_next_stmt(CettaLibraryContext *ctx, Arena *a,
                                Atom *head, Atom **args, uint32_t nargs) {
    char errbuf[128];
    uint64_t id;
    Atom *stmt = NULL;
    CettaMetamathNextStatus status;
    CettaMetamathParser *parser;

    if (!ctx || nargs != 1) return NULL;
    if (!cetta_native_handle_arg(args[0], CETTA_METAMATH_HANDLE_KIND, &id)) {
        return module_signature_error(a, head, args, nargs, "expected metamath parser handle");
    }
    parser = cetta_native_handle_get(ctx, CETTA_METAMATH_HANDLE_KIND, id);
    if (!parser) {
        return atom_error(a, module_call_expr(a, head, args, nargs),
                          atom_symbol(a, "unknown native handle"));
    }
    status = cetta_metamath_parser_next_stmt(parser, a, &stmt, errbuf, sizeof(errbuf));
    if (status == CETTA_MM_NEXT_EOF) {
        cetta_native_handle_close(ctx, CETTA_METAMATH_HANDLE_KIND, id);
        return atom_empty(a);
    }
    if (status == CETTA_MM_NEXT_ERROR) {
        return atom_error(a, module_call_expr(a, head, args, nargs),
                          atom_symbol(a, errbuf));
    }
    return stmt;
}

static Atom *metamath_close_file(CettaLibraryContext *ctx, Arena *a,
                                 Atom *head, Atom **args, uint32_t nargs) {
    uint64_t id;

    if (!ctx || nargs != 1) return NULL;
    if (!cetta_native_handle_arg(args[0], CETTA_METAMATH_HANDLE_KIND, &id)) {
        return module_signature_error(a, head, args, nargs, "expected metamath parser handle");
    }
    if (!cetta_native_handle_close(ctx, CETTA_METAMATH_HANDLE_KIND, id)) {
        return atom_error(a, module_call_expr(a, head, args, nargs),
                          atom_symbol(a, "unknown native handle"));
    }
    return atom_unit(a);
}

Atom *cetta_metamath_native_dispatch(CettaLibraryContext *ctx, Space *space,
                                     Arena *a, Atom *head, Atom **args,
                                     uint32_t nargs) {
    const char *head_name;

    (void)space;
    if (!head || head->kind != ATOM_SYMBOL) return NULL;
    head_name = atom_name_cstr(head);
    if (strcmp(head_name, "__cetta_lib_metamath_read_tokens") == 0) {
        return metamath_read_tokens(ctx, a, head, args, nargs);
    }
    if (strcmp(head_name, "__cetta_lib_metamath_parse_file") == 0) {
        return metamath_parse_file_mode(ctx, a, head, args, nargs, false);
    }
    if (strcmp(head_name, "__cetta_lib_metamath_parse_file_pverify") == 0) {
        return metamath_parse_file_mode(ctx, a, head, args, nargs, true);
    }
    if (strcmp(head_name, "__cetta_lib_metamath_open_file") == 0) {
        return metamath_open_file_mode(ctx, a, head, args, nargs, false);
    }
    if (strcmp(head_name, "__cetta_lib_metamath_open_file_pverify") == 0) {
        return metamath_open_file_mode(ctx, a, head, args, nargs, true);
    }
    if (strcmp(head_name, "__cetta_lib_metamath_next_stmt") == 0) {
        return metamath_next_stmt(ctx, a, head, args, nargs);
    }
    if (strcmp(head_name, "__cetta_lib_metamath_close_file") == 0) {
        return metamath_close_file(ctx, a, head, args, nargs);
    }
    return NULL;
}

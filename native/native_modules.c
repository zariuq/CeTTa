#include "native/native_modules.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "library.h"
#include "lib_parse_inference_native.h"
#include "lib_parse_native_grammar.h"

#define CETTA_NATIVE_IMPORT_GPARSE (1u << 24)

static Atom *native_module_error(Arena *a, Atom *source, const char *message) {
    return atom_error(a, source, atom_string(a, message));
}

static bool native_text_arg(Atom *atom, const char **out) {
    if (!atom || !out)
        return false;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        *out = atom->ground.sval;
        return true;
    }
    if (atom->kind == ATOM_SYMBOL) {
        *out = atom_name_cstr(atom);
        return true;
    }
    return false;
}

static bool native_symbol_arg(Arena *a, Atom *atom, SymbolId *out) {
    if (!a || !atom || !out)
        return false;
    if (atom->kind == ATOM_SYMBOL) {
        *out = atom->sym_id;
        return true;
    }
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        *out = atom_symbol(a, atom->ground.sval)->sym_id;
        return true;
    }
    return false;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} NativeTextBuilder;

static bool native_text_reserve(NativeTextBuilder *b, size_t extra) {
    size_t want;
    size_t next_cap;
    char *next;

    if (!b)
        return false;
    if (extra > SIZE_MAX - b->len - 1)
        return false;
    want = b->len + extra + 1;
    if (want <= b->cap)
        return true;
    next_cap = b->cap ? b->cap : 64;
    while (next_cap < want) {
        if (next_cap > SIZE_MAX / 2)
            return false;
        next_cap *= 2;
    }
    next = cetta_realloc(b->data, next_cap);
    b->data = next;
    b->cap = next_cap;
    return true;
}

static bool native_text_append_len(NativeTextBuilder *b,
                                   const char *text,
                                   size_t len) {
    if (!text)
        return false;
    if (!native_text_reserve(b, len))
        return false;
    memcpy(b->data + b->len, text, len);
    b->len += len;
    b->data[b->len] = '\0';
    return true;
}

static bool native_text_append(NativeTextBuilder *b, const char *text) {
    return native_text_append_len(b, text, strlen(text));
}

static bool native_int_arg(Atom *atom, int64_t *out) {
    if (!atom || !out)
        return false;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT) {
        *out = atom->ground.ival;
        return true;
    }
    return false;
}

static const char *rho_native_fresh_name(uint32_t depth,
                                         char *buf,
                                         size_t buf_size) {
    static const char *base[] = {"x", "y", "z"};
    uint32_t suffix;
    const char *stem;

    if (depth < 3)
        return base[depth];
    suffix = ((depth - 3u) / 3u) + 1u;
    stem = base[(depth - 3u) % 3u];
    snprintf(buf, buf_size, "%s_rho%u", stem, suffix);
    return buf;
}

static bool rho_native_render_proc(Atom *node,
                                   const char **env,
                                   uint32_t env_len,
                                   NativeTextBuilder *out,
                                   char *error_buf,
                                   size_t error_buf_size);

static bool rho_native_set_error(char *error_buf,
                                 size_t error_buf_size,
                                 const char *message) {
    if (error_buf && error_buf_size > 0)
        snprintf(error_buf, error_buf_size, "%s", message);
    return false;
}

static bool rho_native_render_name(Atom *node,
                                   const char **env,
                                   uint32_t env_len,
                                   NativeTextBuilder *out,
                                   char *error_buf,
                                   size_t error_buf_size) {
    Atom *head;

    if (!node || node->kind != ATOM_EXPR || node->expr.len == 0)
        return rho_native_set_error(error_buf, error_buf_size,
                                    "bad rho name node");
    head = node->expr.elems[0];
    if (atom_is_symbol(head, "rho:quote") && node->expr.len == 2) {
        return native_text_append(out, "(rho:quote ") &&
               rho_native_render_proc(node->expr.elems[1], env, env_len, out,
                                      error_buf, error_buf_size) &&
               native_text_append(out, ")");
    }
    if (atom_is_symbol(head, "rho:bound") && node->expr.len == 2) {
        int64_t idx;
        if (!native_int_arg(node->expr.elems[1], &idx) ||
            idx < 0 || (uint64_t)idx >= env_len) {
            return rho_native_set_error(error_buf, error_buf_size,
                                        "rho:bound index out of scope");
        }
        return native_text_append(out, "$") &&
               native_text_append(out, env[(uint32_t)idx]);
    }
    if (atom_is_symbol(head, "rho:free") && node->expr.len == 2) {
        Atom *name = node->expr.elems[1];
        if (!name || (name->kind != ATOM_SYMBOL && name->kind != ATOM_VAR))
            return rho_native_set_error(error_buf, error_buf_size,
                                        "rho:free expects a name atom");
        return native_text_append(out, "$") &&
               native_text_append(out, atom_name_cstr(name));
    }
    return rho_native_set_error(error_buf, error_buf_size,
                                "unknown rho name node");
}

static bool rho_native_render_proc(Atom *node,
                                   const char **env,
                                   uint32_t env_len,
                                   NativeTextBuilder *out,
                                   char *error_buf,
                                   size_t error_buf_size) {
    Atom *head;

    if (atom_is_symbol(node, "rho:nil"))
        return native_text_append(out, "rho:nil");
    if (!node || node->kind != ATOM_EXPR || node->expr.len == 0)
        return rho_native_set_error(error_buf, error_buf_size,
                                    "bad rho proc node");
    head = node->expr.elems[0];
    if (atom_is_symbol(head, "rho:send") && node->expr.len == 3) {
        return native_text_append(out, "(rho:send ") &&
               rho_native_render_name(node->expr.elems[1], env, env_len, out,
                                      error_buf, error_buf_size) &&
               native_text_append(out, " ") &&
               rho_native_render_proc(node->expr.elems[2], env, env_len, out,
                                      error_buf, error_buf_size) &&
               native_text_append(out, ")");
    }
    if (atom_is_symbol(head, "rho:drop") && node->expr.len == 2) {
        return native_text_append(out, "(rho:drop ") &&
               rho_native_render_name(node->expr.elems[1], env, env_len, out,
                                      error_buf, error_buf_size) &&
               native_text_append(out, ")");
    }
    if (atom_is_symbol(head, "rho:par") && node->expr.len == 3) {
        return native_text_append(out, "(rho:par ") &&
               rho_native_render_proc(node->expr.elems[1], env, env_len, out,
                                      error_buf, error_buf_size) &&
               native_text_append(out, " ") &&
               rho_native_render_proc(node->expr.elems[2], env, env_len, out,
                                      error_buf, error_buf_size) &&
               native_text_append(out, ")");
    }
    if (atom_is_symbol(head, "rho:recv-db") && node->expr.len == 3) {
        char name_buf[32];
        const char *binder_name =
            rho_native_fresh_name(env_len, name_buf, sizeof(name_buf));
        const char **body_env =
            cetta_malloc(sizeof(char *) * ((size_t)env_len + 1u));
        bool ok;
        body_env[0] = binder_name;
        for (uint32_t i = 0; i < env_len; i++)
            body_env[i + 1] = env[i];
        ok = native_text_append(out, "(rho:recv ") &&
             rho_native_render_name(node->expr.elems[1], env, env_len, out,
                                    error_buf, error_buf_size) &&
             native_text_append(out, " $") &&
             native_text_append(out, binder_name) &&
             native_text_append(out, " ") &&
             rho_native_render_proc(node->expr.elems[2], body_env, env_len + 1u,
                                    out, error_buf, error_buf_size) &&
             native_text_append(out, ")");
        free(body_env);
        return ok;
    }
    return rho_native_set_error(error_buf, error_buf_size,
                                "unknown rho proc node");
}

static Atom *native_ok_string(Arena *a, const char *value) {
    return atom_expr2(a, atom_symbol(a, "Ok"), atom_string(a, value));
}

static Atom *native_err_string(Arena *a, const char *message) {
    return atom_expr2(a, atom_symbol(a, "Err"), atom_string(a, message));
}

static Atom *rho_native_canon_mrho_text(Arena *a, Atom *ast) {
    NativeTextBuilder out = {0};
    char error_buf[128];
    bool ok;

    error_buf[0] = '\0';
    ok = rho_native_render_proc(ast, NULL, 0, &out, error_buf, sizeof(error_buf));
    if (!ok) {
        free(out.data);
        return native_err_string(a, error_buf[0] ? error_buf : "rho-render-failed");
    }
    {
        Atom *result = native_ok_string(a, out.data ? out.data : "");
        free(out.data);
        return result;
    }
}

static Atom *gparse_make_cons_list(Arena *a, Atom **items, uint32_t len) {
    Atom *out = atom_symbol(a, "Nil");
    uint32_t i;

    for (i = len; i > 0; i--)
        out = atom_expr3(a, atom_symbol(a, "Cons"), items[i - 1], out);
    return out;
}

static Atom *gparse_rhs_item_atom(Arena *a, const CettaLpNativeSymbol *sym) {
    if (sym->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
        Atom **elems = arena_alloc(a, sizeof(Atom *) * 2);
        elems[0] = atom_symbol(a, "Tm");
        elems[1] = atom_symbol_id(a, sym->name);
        return atom_expr(a, elems, 2);
    }
    if (sym->kind == CETTA_LP_NATIVE_SYMBOL_HL) {
        Atom **elems = arena_alloc(a, sizeof(Atom *) * 2);
        elems[0] = atom_symbol(a, "Hl");
        elems[1] = atom_symbol_id(a, sym->name);
        return atom_expr(a, elems, 2);
    }
    {
        Atom **elems = arena_alloc(a, sizeof(Atom *) * 3);
        elems[0] = atom_symbol(a, "HlB");
        elems[1] = atom_symbol_id(a, sym->name);
        elems[2] = atom_symbol_id(a, sym->scope);
        return atom_expr(a, elems, 3);
    }
}

static Atom *gparse_prod_atom(Arena *a, const CettaLpNativeProduction *prod) {
    Atom **rhs_items = NULL;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * 4);
    uint32_t i;

    if (prod->rhs_len > 0) {
        rhs_items = arena_alloc(a, sizeof(Atom *) * prod->rhs_len);
        for (i = 0; i < prod->rhs_len; i++)
            rhs_items[i] = gparse_rhs_item_atom(a, &prod->rhs[i]);
    }
    elems[0] = atom_symbol(a, "Prod");
    elems[1] = atom_symbol_id(a, prod->label);
    elems[2] = atom_symbol_id(a, prod->lhs);
    elems[3] = gparse_make_cons_list(a, rhs_items, prod->rhs_len);
    return atom_expr(a, elems, 4);
}

static Atom *gparse_var_atom(Arena *a, const CettaLpNativeVarDecl *decl) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * 3);
    elems[0] = atom_symbol(a, "VarD");
    elems[1] = atom_symbol_id(a, decl->atom);
    elems[2] = atom_symbol_id(a, decl->nt);
    return atom_expr(a, elems, 3);
}

static Atom *gparse_lex_atom(Arena *a, const CettaLpNativeLexDecl *decl) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * 3);
    elems[0] = atom_symbol(a, "LexD");
    elems[1] = atom_symbol_id(a, decl->klass);
    elems[2] = atom_symbol_id(a, decl->nt);
    return atom_expr(a, elems, 3);
}

static Atom *gparse_grammar_data_atom(Arena *a,
                                      const CettaLpNativeGrammar *grammar) {
    Atom **lexes = NULL;
    Atom **vars = NULL;
    Atom **prods = NULL;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * 4);
    uint32_t i;

    if (grammar->lex_len > 0) {
        lexes = arena_alloc(a, sizeof(Atom *) * grammar->lex_len);
        for (i = 0; i < grammar->lex_len; i++)
            lexes[i] = gparse_lex_atom(a, &grammar->lexes[i]);
    }
    if (grammar->var_len > 0) {
        vars = arena_alloc(a, sizeof(Atom *) * grammar->var_len);
        for (i = 0; i < grammar->var_len; i++)
            vars[i] = gparse_var_atom(a, &grammar->vars[i]);
    }
    if (grammar->production_len > 0) {
        prods = arena_alloc(a, sizeof(Atom *) * grammar->production_len);
        for (i = 0; i < grammar->production_len; i++)
            prods[i] = gparse_prod_atom(a, &grammar->productions[i]);
    }

    elems[0] = atom_symbol(a, "GParseGrammarData");
    elems[1] = gparse_make_cons_list(a, lexes, grammar->lex_len);
    elems[2] = gparse_make_cons_list(a, vars, grammar->var_len);
    elems[3] = gparse_make_cons_list(a, prods, grammar->production_len);
    return atom_expr(a, elems, 4);
}

static Atom *gparse_dispatch(struct CettaLibraryContext *ctx,
                             Space *space,
                             Arena *a,
                             Atom *head,
                             Atom **args,
                             uint32_t nargs) {
    const char *filename = NULL;
    const char *def_name = NULL;
    const char *token_filename = NULL;
    SymbolId start_nt = 0;
    CettaLpNativeGrammar grammar;
    CettaLpNativeGrammarSummary summary;
    CettaLpNativeSlrSummary slr_summary;
    char error_buf[256];
    Atom **elems;

    (void)ctx;
    (void)space;

    if (atom_is_symbol(head, "__cetta_lib_gparse_rho_canon_mrho_text")) {
        if (nargs != 1) {
            return native_module_error(
                a, head,
                "__cetta_lib_gparse_rho_canon_mrho_text expects a rho canonical AST");
        }
        return rho_native_canon_mrho_text(a, args[0]);
    }

    if (atom_is_symbol(head,
                       "__cetta_lib_gparse_inference_presentation")) {
        Atom *result;

        if (nargs != 3) {
            return native_module_error(
                a, head,
                "__cetta_lib_gparse_inference_presentation expects grammar, source, and tokens");
        }
        cetta_lp_native_grammar_init(&grammar);
        error_buf[0] = '\0';
        if (!cetta_lp_native_grammar_load_list(&grammar, args[0],
                                               error_buf,
                                               sizeof(error_buf))) {
            cetta_lp_native_grammar_free(&grammar);
            return native_module_error(
                a, head,
                error_buf[0] ? error_buf : "gparse grammar list load failed");
        }
        result = cetta_lp_native_inference_presentation(
            &grammar, args[1], args[2], a, error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!result) {
            return native_module_error(
                a, head,
                error_buf[0] ? error_buf
                             : "gparse inference presentation failed");
        }
        return atom_expr2(a, atom_symbol(a, "return"), result);
    }

    if (atom_is_symbol(head, "__cetta_lib_gparse_grammar_summary")) {
        if (nargs != 2 ||
            !native_text_arg(args[0], &filename) ||
            !native_text_arg(args[1], &def_name)) {
            return native_module_error(a, head,
                                       "__cetta_lib_gparse_grammar_summary expects filename and definition");
        }
        cetta_lp_native_grammar_init(&grammar);
        error_buf[0] = '\0';
        if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                               error_buf, sizeof(error_buf))) {
            cetta_lp_native_grammar_free(&grammar);
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
        }
        cetta_lp_native_grammar_summary(&grammar, &summary);
        cetta_lp_native_grammar_free(&grammar);

        elems = arena_alloc(a, sizeof(Atom *) * 5);
        elems[0] = atom_symbol(a, "GParseGrammarSummary");
        elems[1] = atom_int(a, summary.lex_len);
        elems[2] = atom_int(a, summary.var_len);
        elems[3] = atom_int(a, summary.production_len);
        elems[4] = atom_int(a, summary.binder_hole_count);
        return atom_expr(a, elems, 5);
    }
    if (atom_is_symbol(head, "__cetta_lib_gparse_grammar_data")) {
        if (nargs != 2 ||
            !native_text_arg(args[0], &filename) ||
            !native_text_arg(args[1], &def_name)) {
            return native_module_error(
                a, head,
                "__cetta_lib_gparse_grammar_data expects filename and definition");
        }
        cetta_lp_native_grammar_init(&grammar);
        error_buf[0] = '\0';
        if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                               error_buf, sizeof(error_buf))) {
            cetta_lp_native_grammar_free(&grammar);
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
        }
        {
            Atom *result = gparse_grammar_data_atom(a, &grammar);
            cetta_lp_native_grammar_free(&grammar);
            return result;
        }
    }
    if (!atom_is_symbol(head, "__cetta_lib_gparse_slr_summary"))
        goto maybe_parse_shared;
    if (nargs != 3 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_slr_summary expects filename, definition, and start");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    if (!cetta_lp_native_slr_summary(&grammar, start_nt,
                                     &slr_summary, error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse SLR summary failed");
    }
    cetta_lp_native_grammar_free(&grammar);

    elems = arena_alloc(a, sizeof(Atom *) * 7);
    elems[0] = atom_symbol(a, "GParseSLRSummary");
    elems[1] = atom_int(a, slr_summary.state_len);
    elems[2] = atom_int(a, slr_summary.shift_len);
    elems[3] = atom_int(a, slr_summary.goto_len);
    elems[4] = atom_int(a, slr_summary.reduce_len);
    elems[5] = atom_int(a, slr_summary.accept_len);
    elems[6] = atom_int(a, slr_summary.conflict_len);
    return atom_expr(a, elems, 7);

maybe_parse_shared:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_slr_parse_shared"))
        goto maybe_glr_class;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_slr_parse_shared expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *parsed =
            cetta_lp_native_slr_parse_shared(&grammar, start_nt, args[3], a,
                                             error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!parsed) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse SLR parse failed");
        }
        return parsed;
    }

maybe_glr_class:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_glr_class"))
        goto maybe_glr_shared;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_glr_class expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *parsed =
            cetta_lp_native_glr_parse_class(&grammar, start_nt, args[3], a,
                                            error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!parsed) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLR class failed");
        }
        return parsed;
    }

maybe_glr_shared:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_glr_parse_shared"))
        goto maybe_glr_forest_summary;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_glr_parse_shared expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *parsed =
            cetta_lp_native_glr_parse_shared(&grammar, start_nt, args[3], a,
                                             error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!parsed) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLR shared parse failed");
        }
        return parsed;
    }

maybe_glr_forest_summary:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_glr_forest_summary"))
        goto maybe_glr_forest_signature;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_glr_forest_summary expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *summary =
            cetta_lp_native_glr_forest_summary(&grammar, start_nt, args[3], a,
                                               error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!summary) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLR forest summary failed");
        }
        return summary;
    }

maybe_glr_forest_signature:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_glr_forest_signature"))
        goto maybe_glr_forest_signature_digest;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_glr_forest_signature expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *signature =
            cetta_lp_native_glr_forest_signature(&grammar, start_nt, args[3], a,
                                                 error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!signature) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLR forest signature failed");
        }
        return signature;
    }

maybe_glr_forest_signature_digest:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_glr_forest_signature_digest"))
        goto maybe_glr_forest_data;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_glr_forest_signature_digest expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *digest =
            cetta_lp_native_glr_forest_signature_digest(
                &grammar, start_nt, args[3], a, error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!digest) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLR forest signature digest failed");
        }
        return digest;
    }

maybe_glr_forest_data:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_glr_forest_data"))
        goto maybe_gll_shared;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_glr_forest_data expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *forest =
            cetta_lp_native_glr_forest_data(&grammar, start_nt, args[3], a,
                                            error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!forest) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLR forest data failed");
        }
        return forest;
    }

maybe_gll_shared:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_parse_shared"))
        goto maybe_gll_recognize;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_parse_shared expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *parsed =
            cetta_lp_native_gll_parse_shared(&grammar, start_nt, args[3], a,
                                             error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!parsed) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL shared parse failed");
        }
        return parsed;
    }

maybe_gll_recognize:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_recognize"))
        goto maybe_gll_recognize_token_file;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_recognize expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *recognized =
            cetta_lp_native_gll_recognize(&grammar, start_nt, args[3], a,
                                          error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!recognized) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL recognize failed");
        }
        return recognized;
    }

maybe_gll_recognize_token_file:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_recognize_token_file"))
        goto maybe_gll_span_summary;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt) ||
        !native_text_arg(args[3], &token_filename)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_recognize_token_file expects filename, definition, start, and token filename");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *recognized =
            cetta_lp_native_gll_recognize_token_file(
                &grammar, start_nt, token_filename, a, error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!recognized) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL recognize token file failed");
        }
        return recognized;
    }

maybe_gll_span_summary:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_span_summary"))
        goto maybe_gll_span_summary_token_file;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_span_summary expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *summary =
            cetta_lp_native_gll_span_summary(&grammar, start_nt, args[3], a,
                                             error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!summary) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL span summary failed");
        }
        return summary;
    }

maybe_gll_span_summary_token_file:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_span_summary_token_file"))
        goto maybe_gll_forest_summary;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt) ||
        !native_text_arg(args[3], &token_filename)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_span_summary_token_file expects filename, definition, start, and token filename");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *summary =
            cetta_lp_native_gll_span_summary_token_file(
                &grammar, start_nt, token_filename, a, error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!summary) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL span summary token file failed");
        }
        return summary;
    }

maybe_gll_forest_summary:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_forest_summary"))
        goto maybe_gll_forest_summary_token_file;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_forest_summary expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *summary =
            cetta_lp_native_gll_forest_summary(&grammar, start_nt, args[3], a,
                                               error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!summary) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL forest summary failed");
        }
        return summary;
    }

maybe_gll_forest_summary_token_file:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_forest_summary_token_file"))
        goto maybe_gll_forest_signature;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt) ||
        !native_text_arg(args[3], &token_filename)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_forest_summary_token_file expects filename, definition, start, and token filename");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *summary =
            cetta_lp_native_gll_forest_summary_token_file(
                &grammar, start_nt, token_filename, a, error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!summary) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL forest summary token file failed");
        }
        return summary;
    }

maybe_gll_forest_signature:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_forest_signature"))
        goto maybe_gll_forest_signature_digest;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_forest_signature expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *signature =
            cetta_lp_native_gll_forest_signature(&grammar, start_nt, args[3], a,
                                                 error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!signature) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL forest signature failed");
        }
        return signature;
    }

maybe_gll_forest_signature_digest:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_forest_signature_digest"))
        goto maybe_gll_forest_signature_digest_token_file;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_forest_signature_digest expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *digest =
            cetta_lp_native_gll_forest_signature_digest(
                &grammar, start_nt, args[3], a, error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!digest) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL forest signature digest failed");
        }
        return digest;
    }

maybe_gll_forest_signature_digest_token_file:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_forest_signature_digest_token_file"))
        goto maybe_gll_forest_data;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt) ||
        !native_text_arg(args[3], &token_filename)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_forest_signature_digest_token_file expects filename, definition, start, and token filename");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *digest =
            cetta_lp_native_gll_forest_signature_digest_token_file(
                &grammar, start_nt, token_filename, a, error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!digest) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL forest signature digest token file failed");
        }
        return digest;
    }

maybe_gll_forest_data:
    if (!atom_is_symbol(head, "__cetta_lib_gparse_gll_forest_data"))
        return NULL;
    if (nargs != 4 ||
        !native_text_arg(args[0], &filename) ||
        !native_text_arg(args[1], &def_name) ||
        !native_symbol_arg(a, args[2], &start_nt)) {
        return native_module_error(
            a, head,
            "__cetta_lib_gparse_gll_forest_data expects filename, definition, start, and tokens");
    }
    cetta_lp_native_grammar_init(&grammar);
    error_buf[0] = '\0';
    if (!cetta_lp_native_grammar_load_file(&grammar, filename, def_name,
                                           error_buf, sizeof(error_buf))) {
        cetta_lp_native_grammar_free(&grammar);
        return native_module_error(
            a, head, error_buf[0] ? error_buf : "gparse grammar load failed");
    }
    {
        Atom *forest =
            cetta_lp_native_gll_forest_data(&grammar, start_nt, args[3], a,
                                            error_buf, sizeof(error_buf));
        cetta_lp_native_grammar_free(&grammar);
        if (!forest) {
            return native_module_error(
                a, head, error_buf[0] ? error_buf : "gparse GLL forest data failed");
        }
        return forest;
    }
}

static bool native_module_loaded(const CettaLibraryContext *ctx,
                                 const CettaNativeBuiltinModule *mod) {
    uint32_t i;

    if (!ctx || !mod || !mod->name)
        return false;
    for (i = 0; i < ctx->loaded_module_len; i++) {
        if (strcmp(ctx->loaded_modules[i].display_name, mod->name) == 0)
            return true;
    }
    return false;
}

static const CettaNativeBuiltinModule g_native_modules[] = {
    {"gparse", CETTA_NATIVE_IMPORT_GPARSE, gparse_dispatch},
};

const CettaNativeBuiltinModule *cetta_native_module_lookup(const char *name) {
    uint32_t i;

    if (!name)
        return NULL;
    for (i = 0; i < sizeof(g_native_modules) / sizeof(g_native_modules[0]); i++) {
        if (strcmp(g_native_modules[i].name, name) == 0)
            return &g_native_modules[i];
    }
    return NULL;
}

Atom *cetta_native_module_dispatch_active(struct CettaLibraryContext *ctx,
                                          Space *space, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs,
                                          uint32_t active_mask) {
    uint32_t i;

    for (i = 0; i < sizeof(g_native_modules) / sizeof(g_native_modules[0]); i++) {
        const CettaNativeBuiltinModule *mod = &g_native_modules[i];
        Atom *result;
        if ((active_mask & mod->import_bit) == 0 &&
            !native_module_loaded(ctx, mod))
            continue;
        result = mod->dispatch(ctx, space, a, head, args, nargs);
        if (result)
            return result;
    }
    return NULL;
}

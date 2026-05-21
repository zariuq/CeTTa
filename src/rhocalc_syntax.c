#include "rhocalc_syntax.h"

#include "parser.h"
#include "rhocalc_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    Atom *var;
} RhoParseBinding;

typedef struct {
    const char *text;
    size_t pos;
    Arena *arena;
    RhoParseBinding *bindings;
    uint32_t binding_len;
    uint32_t binding_cap;
    char error[256];
} RhoParser;

typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
} RhoSyntaxVec;

typedef struct {
    SymbolId spelling;
    VarId source_var_id;
    Atom *var;
} RhoElabBinding;

typedef struct {
    Arena *arena;
    RhoElabBinding *bindings;
    uint32_t binding_len;
    uint32_t binding_cap;
    bool failed;
} RhoElaborator;

static __thread char g_rhocalc_parse_error[256];

const char *rhocalc_last_parse_error(void) {
    return g_rhocalc_parse_error[0] ? g_rhocalc_parse_error : NULL;
}

static void rho_parse_clear_error(void) {
    g_rhocalc_parse_error[0] = '\0';
}

static void rho_parse_store_error(RhoParser *parser) {
    if (parser && parser->error[0]) {
        snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
                 "%s", parser->error);
    }
}

static void rp_error(RhoParser *parser, const char *message) {
    if (!parser || parser->error[0]) return;
    snprintf(parser->error, sizeof(parser->error),
             "%s near byte %zu", message, parser->pos);
}

static void rho_vec_init(RhoSyntaxVec *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static void rho_vec_free(RhoSyntaxVec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rho_vec_push(RhoSyntaxVec *vec, Atom *atom) {
    if (vec->len == vec->cap) {
        uint32_t next_cap = vec->cap ? vec->cap * 2u : 8u;
        Atom **next = cetta_realloc(vec->items, sizeof(Atom *) * next_cap);
        if (!next) return false;
        vec->items = next;
        vec->cap = next_cap;
    }
    vec->items[vec->len++] = atom;
    return true;
}

static Atom *rho_call(Arena *arena, const char *head,
                      Atom *const *args, uint32_t nargs) {
    Atom **elems = arena_alloc(arena, sizeof(Atom *) * (size_t)(nargs + 1));
    elems[0] = atom_symbol(arena, head);
    for (uint32_t i = 0; i < nargs; i++) elems[i + 1] = args[i];
    return atom_expr(arena, elems, nargs + 1);
}

static Atom *rho_nil(Arena *arena) {
    return atom_symbol(arena, "rho:nil");
}

static Atom *rho_unary(Arena *arena, const char *head, Atom *arg) {
    Atom *args[1] = {arg};
    return rho_call(arena, head, args, 1);
}

static Atom *rho_binary(Arena *arena, const char *head, Atom *a, Atom *b) {
    Atom *args[2] = {a, b};
    return rho_call(arena, head, args, 2);
}

static Atom *rho_ternary(Arena *arena, const char *head,
                         Atom *a, Atom *b, Atom *c) {
    Atom *args[3] = {a, b, c};
    return rho_call(arena, head, args, 3);
}

static bool rho_expr_head_named(Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0 &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr(atom->expr.elems[0]), name) == 0;
}

static bool rhocalc_is_domain_proc_atom(Atom *atom);

static bool rhocalc_is_domain_name_atom(Atom *atom) {
    if (!atom) return false;
    if (atom->kind == ATOM_VAR) return true;
    return rho_expr_head_named(atom, "rho:quote") && atom->expr.len == 2 &&
           rhocalc_is_domain_proc_atom(atom->expr.elems[1]);
}

static bool rhocalc_is_domain_proc_atom(Atom *atom) {
    if (atom_is_symbol(atom, "rho:nil")) return true;
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) return false;

    if (rho_expr_head_named(atom, "rho:par")) {
        for (uint32_t i = 1; i < atom->expr.len; i++) {
            if (!rhocalc_is_domain_proc_atom(atom->expr.elems[i])) return false;
        }
        return true;
    }
    if (rho_expr_head_named(atom, "rho:send") && atom->expr.len == 3) {
        return rhocalc_is_domain_name_atom(atom->expr.elems[1]) &&
               rhocalc_is_domain_proc_atom(atom->expr.elems[2]);
    }
    if (rho_expr_head_named(atom, "rho:recv") && atom->expr.len == 4) {
        return rhocalc_is_domain_name_atom(atom->expr.elems[1]) &&
               atom->expr.elems[2]->kind == ATOM_VAR &&
               rhocalc_is_domain_proc_atom(atom->expr.elems[3]);
    }
    if (rho_expr_head_named(atom, "rho:drop") && atom->expr.len == 2) {
        return rhocalc_is_domain_name_atom(atom->expr.elems[1]);
    }
    return false;
}

bool rhocalc_is_domain_atom(Atom *atom) {
    return rhocalc_is_domain_proc_atom(atom);
}

static void rp_skip_ws(RhoParser *parser) {
    for (;;) {
        while (isspace((unsigned char)parser->text[parser->pos])) {
            parser->pos++;
        }
        if (parser->text[parser->pos] == '/' &&
            parser->text[parser->pos + 1] == '/') {
            parser->pos += 2;
            while (parser->text[parser->pos] &&
                   parser->text[parser->pos] != '\n') {
                parser->pos++;
            }
            continue;
        }
        if (parser->text[parser->pos] == '/' &&
            parser->text[parser->pos + 1] == '*') {
            parser->pos += 2;
            while (parser->text[parser->pos] &&
                   !(parser->text[parser->pos] == '*' &&
                     parser->text[parser->pos + 1] == '/')) {
                parser->pos++;
            }
            if (parser->text[parser->pos]) parser->pos += 2;
            continue;
        }
        break;
    }
}

static bool rp_consume_char(RhoParser *parser, char c) {
    rp_skip_ws(parser);
    if (parser->text[parser->pos] != c) return false;
    parser->pos++;
    return true;
}

static bool rp_consume_span(RhoParser *parser, const char *span) {
    size_t len = strlen(span);
    rp_skip_ws(parser);
    if (strncmp(parser->text + parser->pos, span, len) != 0) return false;
    parser->pos += len;
    return true;
}

static bool rp_word_boundary(char c) {
    return c == '\0' || !(isalnum((unsigned char)c) || c == '_' || c == '-');
}

static bool rp_consume_keyword(RhoParser *parser, const char *kw) {
    size_t len = strlen(kw);
    rp_skip_ws(parser);
    if (strncmp(parser->text + parser->pos, kw, len) != 0 ||
        !rp_word_boundary(parser->text[parser->pos + len])) {
        return false;
    }
    parser->pos += len;
    return true;
}

static bool rp_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool rp_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '?';
}

static char *rp_parse_ident(RhoParser *parser) {
    size_t start;
    size_t len;
    char *out;
    rp_skip_ws(parser);
    if (!rp_ident_start(parser->text[parser->pos])) return NULL;
    start = parser->pos;
    parser->pos++;
    while (rp_ident_char(parser->text[parser->pos])) parser->pos++;
    len = parser->pos - start;
    out = arena_alloc(parser->arena, len + 1);
    memcpy(out, parser->text + start, len);
    out[len] = '\0';
    return out;
}

static uint32_t rp_binding_mark(RhoParser *parser) {
    return parser->binding_len;
}

static void rp_binding_pop(RhoParser *parser, uint32_t mark) {
    if (mark <= parser->binding_len) parser->binding_len = mark;
}

static Atom *rp_lookup_binding(RhoParser *parser, const char *name) {
    for (uint32_t i = parser->binding_len; i > 0; i--) {
        if (strcmp(parser->bindings[i - 1].name, name) == 0) {
            return parser->bindings[i - 1].var;
        }
    }
    return NULL;
}

static bool rp_push_binding(RhoParser *parser, const char *name, Atom *var) {
    if (parser->binding_len == parser->binding_cap) {
        uint32_t next_cap = parser->binding_cap ? parser->binding_cap * 2u : 8u;
        RhoParseBinding *next =
            cetta_realloc(parser->bindings,
                          sizeof(RhoParseBinding) * next_cap);
        if (!next) return false;
        parser->bindings = next;
        parser->binding_cap = next_cap;
    }
    parser->bindings[parser->binding_len].name = name;
    parser->bindings[parser->binding_len].var = var;
    parser->binding_len++;
    return true;
}

static Atom *rp_parse_proc(RhoParser *parser);
static Atom *rp_parse_name(RhoParser *parser);

static Atom *rp_parse_name(RhoParser *parser) {
    rp_skip_ws(parser);
    if (parser->text[parser->pos] == '@') {
        Atom *proc;
        uint32_t mark;
        RhoParseBinding *saved = NULL;
        parser->pos++;
        if (!rp_consume_char(parser, '{')) {
            rp_error(parser, "expected '{' after '@'");
            return NULL;
        }
        mark = rp_binding_mark(parser);
        if (mark > 0) {
            saved = malloc(sizeof(RhoParseBinding) * mark);
            if (!saved) {
                rp_error(parser, "could not preserve quote scope");
                return NULL;
            }
            memcpy(saved, parser->bindings, sizeof(RhoParseBinding) * mark);
        }
        parser->binding_len = 0;
        proc = rp_parse_proc(parser);
        if (saved) {
            memcpy(parser->bindings, saved, sizeof(RhoParseBinding) * mark);
            free(saved);
        }
        parser->binding_len = mark;
        if (!proc) return NULL;
        if (!rp_consume_char(parser, '}')) {
            rp_error(parser, "expected '}' after quoted process");
            return NULL;
        }
        return rho_unary(parser->arena, "rho:quote", proc);
    }
    if (parser->text[parser->pos] == '$') {
        char *name;
        Atom *var;
        parser->pos++;
        name = rp_parse_ident(parser);
        if (!name) {
            rp_error(parser, "expected variable name after '$'");
            return NULL;
        }
        var = rp_lookup_binding(parser, name);
        if (!var) {
            return atom_var(parser->arena, name);
        }
        return var;
    }
    if (rp_ident_start(parser->text[parser->pos])) {
        char *name = rp_parse_ident(parser);
        Atom *var = rp_lookup_binding(parser, name);
        if (!var) {
            rp_error(parser, "bare names are not core rho names; use @{...}");
            return NULL;
        }
        return var;
    }
    rp_error(parser, "expected rho name");
    return NULL;
}

static Atom *rp_parse_proc_atom(RhoParser *parser) {
    rp_skip_ws(parser);
    if (rp_consume_char(parser, '{')) {
        Atom *inner = rp_parse_proc(parser);
        if (!inner) return NULL;
        if (!rp_consume_char(parser, '}')) {
            rp_error(parser, "expected '}'");
            return NULL;
        }
        return inner;
    }
    if (rp_consume_char(parser, '0')) {
        return rho_nil(parser->arena);
    }
    if (rp_consume_keyword(parser, "for")) {
        char *binder_name;
        Atom *binder;
        Atom *channel;
        Atom *body;
        uint32_t mark;
        if (!rp_consume_char(parser, '(')) {
            rp_error(parser, "expected '(' after for");
            return NULL;
        }
        if (rp_consume_char(parser, '$')) {
            binder_name = rp_parse_ident(parser);
        } else {
            binder_name = rp_parse_ident(parser);
        }
        if (!binder_name) {
            rp_error(parser, "expected input binder");
            return NULL;
        }
        if (!rp_consume_span(parser, "<-")) {
            rp_error(parser, "expected '<-' in input");
            return NULL;
        }
        channel = rp_parse_name(parser);
        if (!channel) return NULL;
        if (!rp_consume_char(parser, ')')) {
            rp_error(parser, "expected ')' after input channel");
            return NULL;
        }
        if (!rp_consume_char(parser, '{')) {
            rp_error(parser, "expected input body block");
            return NULL;
        }
        binder = atom_var_with_id(parser->arena, binder_name, fresh_var_id());
        mark = rp_binding_mark(parser);
        if (!rp_push_binding(parser, binder_name, binder)) {
            rp_error(parser, "could not record input binder");
            return NULL;
        }
        body = rp_parse_proc(parser);
        rp_binding_pop(parser, mark);
        if (!body) return NULL;
        if (!rp_consume_char(parser, '}')) {
            rp_error(parser, "expected '}' after input body");
            return NULL;
        }
        return rho_ternary(parser->arena, "rho:recv", channel, binder, body);
    }
    if (rp_consume_char(parser, '*')) {
        Atom *name = rp_parse_name(parser);
        if (!name) return NULL;
        return rho_unary(parser->arena, "rho:drop", name);
    }
    {
        Atom *channel = rp_parse_name(parser);
        Atom *payload;
        if (!channel) return NULL;
        if (!rp_consume_char(parser, '!')) {
            rp_error(parser, "expected output after leading name");
            return NULL;
        }
        if (!rp_consume_char(parser, '(')) {
            rp_error(parser, "expected '(' after '!'");
            return NULL;
        }
        payload = rp_parse_proc(parser);
        if (!payload) return NULL;
        if (!rp_consume_char(parser, ')')) {
            rp_error(parser, "expected ')' after output payload");
            return NULL;
        }
        return rho_binary(parser->arena, "rho:send", channel, payload);
    }
}

static Atom *rp_parse_proc(RhoParser *parser) {
    RhoSyntaxVec items;
    Atom *first;
    rho_vec_init(&items);
    first = rp_parse_proc_atom(parser);
    if (!first) {
        rho_vec_free(&items);
        return NULL;
    }
    (void)rho_vec_push(&items, first);
    while (rp_consume_char(parser, '|')) {
        Atom *next = rp_parse_proc_atom(parser);
        if (!next) {
            rho_vec_free(&items);
            return NULL;
        }
        (void)rho_vec_push(&items, next);
    }
    if (items.len == 1) {
        Atom *out = items.items[0];
        rho_vec_free(&items);
        return out;
    }
    {
        Atom *out = rho_call(parser->arena, "rho:par", items.items, items.len);
        rho_vec_free(&items);
        return out;
    }
}

static int rhocalc_parse_rho_text(const char *text, Arena *arena,
                                  Atom ***out_atoms) {
    RhoParser parser = {0};
    Atom *atom;
    rho_parse_clear_error();
    parser.text = text ? text : "";
    parser.arena = arena;
    atom = rp_parse_proc(&parser);
    rp_skip_ws(&parser);
    if (!atom || parser.error[0] || parser.text[parser.pos] != '\0') {
        if (!parser.error[0]) rp_error(&parser, "unexpected trailing input");
        rho_parse_store_error(&parser);
        free(parser.bindings);
        *out_atoms = NULL;
        return -1;
    }
    *out_atoms = cetta_malloc(sizeof(Atom *));
    (*out_atoms)[0] = atom;
    free(parser.bindings);
    return 1;
}

static char *rhocalc_read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long size;
    size_t got;
    char *buf;
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = malloc((size_t)size + 1u);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    return buf;
}

static uint32_t rho_elab_mark(RhoElaborator *elab) {
    return elab->binding_len;
}

static void rho_elab_pop(RhoElaborator *elab, uint32_t mark) {
    elab->binding_len = mark;
}

static Atom *rho_elab_lookup(RhoElaborator *elab, SymbolId spelling,
                             VarId source_var_id) {
    for (uint32_t i = elab->binding_len; i > 0; i--) {
        if (elab->bindings[i - 1].spelling == spelling &&
            elab->bindings[i - 1].source_var_id == source_var_id) {
            return elab->bindings[i - 1].var;
        }
    }
    return NULL;
}

static bool rho_elab_push(RhoElaborator *elab, SymbolId spelling,
                          VarId source_var_id, Atom *var) {
    if (elab->binding_len == elab->binding_cap) {
        uint32_t next_cap = elab->binding_cap ? elab->binding_cap * 2u : 8u;
        RhoElabBinding *next =
            cetta_realloc(elab->bindings,
                          sizeof(RhoElabBinding) * next_cap);
        if (!next) {
            elab->failed = true;
            return false;
        }
        elab->bindings = next;
        elab->binding_cap = next_cap;
    }
    elab->bindings[elab->binding_len].spelling = spelling;
    elab->bindings[elab->binding_len].source_var_id = source_var_id;
    elab->bindings[elab->binding_len].var = var;
    elab->binding_len++;
    return true;
}

static Atom *rho_elab_atom(RhoElaborator *elab, Atom *atom);

static Atom *rho_elab_expr_generic(RhoElaborator *elab, Atom *atom) {
    Atom **elems = arena_alloc(elab->arena,
                               sizeof(Atom *) * atom->expr.len);
    if (!elems && atom->expr.len > 0) {
        elab->failed = true;
        return atom;
    }
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        elems[i] = rho_elab_atom(elab, atom->expr.elems[i]);
    }
    return atom_expr(elab->arena, elems, atom->expr.len);
}

static Atom *rho_elab_atom(RhoElaborator *elab, Atom *atom) {
    if (!atom) return NULL;
    switch (atom->kind) {
    case ATOM_VAR: {
        Atom *bound = rho_elab_lookup(elab, atom->sym_id, atom->var_id);
        return bound ? bound : atom;
    }
    case ATOM_EXPR:
        if (rho_expr_head_named(atom, "rho:quote") && atom->expr.len == 2) {
            uint32_t mark = rho_elab_mark(elab);
            Atom *elems[2];
            RhoElabBinding *saved = NULL;
            /* Literal quote is static data, so outer receive binders do not
               scope over the quoted process. Binders inside the quote still
               bind within that quoted process. */
            if (mark > 0) {
                saved = malloc(sizeof(RhoElabBinding) * mark);
                if (!saved) {
                    elab->failed = true;
                    return atom;
                }
                memcpy(saved, elab->bindings, sizeof(RhoElabBinding) * mark);
            }
            elab->binding_len = 0;
            elems[0] = atom->expr.elems[0];
            elems[1] = rho_elab_atom(elab, atom->expr.elems[1]);
            if (saved) {
                memcpy(elab->bindings, saved, sizeof(RhoElabBinding) * mark);
                free(saved);
            }
            rho_elab_pop(elab, mark);
            return atom_expr(elab->arena, elems, 2);
        }
        if (rho_expr_head_named(atom, "rho:recv") && atom->expr.len == 4) {
            uint32_t mark = rho_elab_mark(elab);
            Atom *elems[4];
            elems[0] = atom->expr.elems[0];
            elems[1] = rho_elab_atom(elab, atom->expr.elems[1]);
            elems[2] = atom->expr.elems[2];
            if (elems[2]->kind == ATOM_VAR) {
                VarId source_var_id = elems[2]->var_id;
                elems[2] = atom_var_with_spelling(elab->arena,
                                                  elems[2]->sym_id,
                                                  fresh_var_id());
                if (!rho_elab_push(elab, elems[2]->sym_id,
                                   source_var_id, elems[2])) {
                    return atom;
                }
            } else {
                elems[2] = rho_elab_atom(elab, elems[2]);
            }
            elems[3] = rho_elab_atom(elab, atom->expr.elems[3]);
            rho_elab_pop(elab, mark);
            return atom_expr(elab->arena, elems, 4);
        }
        return rho_elab_expr_generic(elab, atom);
    case ATOM_SYMBOL:
    case ATOM_GROUNDED:
        return atom;
    }
    return atom;
}

static int rhocalc_elaborate_mrho_atoms(Arena *arena,
                                        Atom **atoms,
                                        int count) {
    RhoElaborator elab = {0};
    elab.arena = arena;
    for (int i = 0; i < count; i++) {
        atoms[i] = rho_elab_atom(&elab, atoms[i]);
        elab.binding_len = 0;
        if (elab.failed) break;
    }
    free(elab.bindings);
    if (elab.failed) {
        snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
                 "could not elaborate rhocalc/mrho binders");
        return -1;
    }
    return count;
}

Atom *rhocalc_elaborate_mrho_atom(Arena *arena, Atom *atom) {
    RhoElaborator elab = {0};
    Atom *out;
    rho_parse_clear_error();
    elab.arena = arena;
    out = rho_elab_atom(&elab, atom);
    free(elab.bindings);
    if (elab.failed) {
        snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
                 "could not elaborate rhocalc/mrho binders");
        return NULL;
    }
    return out;
}

int rhocalc_parse_text(const char *text,
                       CettaSyntaxId syntax,
                       Arena *arena,
                       Atom ***out_atoms) {
    if (!out_atoms) return -1;
    *out_atoms = NULL;
    if (syntax == CETTA_SYNTAX_METTA) syntax = CETTA_SYNTAX_MRHO;
    if (syntax == CETTA_SYNTAX_MRHO) {
        int n;
        rho_parse_clear_error();
        n = parse_metta_text(text, arena, out_atoms);
        if (n < 0) return n;
        return rhocalc_elaborate_mrho_atoms(arena, *out_atoms, n);
    }
    if (syntax == CETTA_SYNTAX_RHO) {
        return rhocalc_parse_rho_text(text, arena, out_atoms);
    }
    snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
             "unsupported rhocalc syntax '%s'", cetta_syntax_name(syntax));
    return -1;
}

int rhocalc_parse_file(const char *path,
                       CettaSyntaxId syntax,
                       Arena *arena,
                       Atom ***out_atoms) {
    if (!out_atoms) return -1;
    *out_atoms = NULL;
    if (syntax == CETTA_SYNTAX_METTA) syntax = CETTA_SYNTAX_MRHO;
    if (syntax == CETTA_SYNTAX_MRHO) {
        int n;
        rho_parse_clear_error();
        n = parse_metta_file(path, arena, out_atoms);
        if (n < 0) return n;
        return rhocalc_elaborate_mrho_atoms(arena, *out_atoms, n);
    }
    if (syntax == CETTA_SYNTAX_RHO) {
        char *text = rhocalc_read_file(path);
        int n;
        if (!text) {
            snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
                     "could not read %s", path ? path : "<null>");
            return -1;
        }
        n = rhocalc_parse_rho_text(text, arena, out_atoms);
        free(text);
        return n;
    }
    snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
             "unsupported rhocalc syntax '%s'", cetta_syntax_name(syntax));
    return -1;
}

typedef struct {
    VarId id;
    const char *name;
    bool owned;
} RhoPrintBinding;

typedef struct {
    VarId *items;
    uint32_t len;
    uint32_t cap;
} RhoIdStack;

typedef struct {
    RhoPrintBinding *bindings;
    uint32_t binding_len;
    uint32_t binding_cap;
    SymbolId *free_syms;
    uint32_t free_len;
    uint32_t free_cap;
    uint32_t fresh_counter;
    bool failed;
} RhoPrintCtx;

static bool rho_id_stack_push(RhoIdStack *stack, VarId id) {
    if (stack->len == stack->cap) {
        uint32_t next_cap = stack->cap ? stack->cap * 2u : 8u;
        VarId *next = cetta_realloc(stack->items, sizeof(VarId) * next_cap);
        if (!next) return false;
        stack->items = next;
        stack->cap = next_cap;
    }
    stack->items[stack->len++] = id;
    return true;
}

static bool rho_id_stack_contains(RhoIdStack *stack, VarId id) {
    for (uint32_t i = stack->len; i > 0; i--) {
        if (stack->items[i - 1] == id) return true;
    }
    return false;
}

static bool rho_print_free_contains(RhoPrintCtx *ctx, SymbolId sym) {
    for (uint32_t i = 0; i < ctx->free_len; i++) {
        if (ctx->free_syms[i] == sym) return true;
    }
    return false;
}

static void rho_print_free_add(RhoPrintCtx *ctx, SymbolId sym) {
    if (rho_print_free_contains(ctx, sym)) return;
    if (ctx->free_len == ctx->free_cap) {
        uint32_t next_cap = ctx->free_cap ? ctx->free_cap * 2u : 8u;
        SymbolId *next =
            cetta_realloc(ctx->free_syms, sizeof(SymbolId) * next_cap);
        if (!next) {
            ctx->failed = true;
            return;
        }
        ctx->free_syms = next;
        ctx->free_cap = next_cap;
    }
    ctx->free_syms[ctx->free_len++] = sym;
}

static void rho_print_collect_free(RhoPrintCtx *ctx, Atom *atom,
                                   RhoIdStack *bound) {
    if (!atom || ctx->failed) return;
    if (atom->kind == ATOM_VAR) {
        if (!rho_id_stack_contains(bound, atom->var_id)) {
            rho_print_free_add(ctx, atom->sym_id);
        }
        return;
    }
    if (atom->kind != ATOM_EXPR) return;

    if (rho_expr_head_named(atom, "rho:quote") && atom->expr.len == 2) {
        RhoIdStack inner = {0};
        rho_print_collect_free(ctx, atom->expr.elems[1], &inner);
        free(inner.items);
        return;
    }
    if (rho_expr_head_named(atom, "rho:recv") && atom->expr.len == 4) {
        uint32_t mark = bound->len;
        rho_print_collect_free(ctx, atom->expr.elems[1], bound);
        if (atom->expr.elems[2]->kind == ATOM_VAR &&
            !rho_id_stack_push(bound, atom->expr.elems[2]->var_id)) {
            ctx->failed = true;
            return;
        }
        rho_print_collect_free(ctx, atom->expr.elems[3], bound);
        bound->len = mark;
        return;
    }
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        rho_print_collect_free(ctx, atom->expr.elems[i], bound);
    }
}

static const char *rho_print_lookup(RhoPrintCtx *ctx, VarId id) {
    for (uint32_t i = ctx->binding_len; i > 0; i--) {
        if (ctx->bindings[i - 1].id == id) return ctx->bindings[i - 1].name;
    }
    return NULL;
}

static bool rho_print_name_conflicts(RhoPrintCtx *ctx, const char *name) {
    for (uint32_t i = 0; i < ctx->free_len; i++) {
        if (strcmp(symbol_bytes(g_symbols, ctx->free_syms[i]), name) == 0) {
            return true;
        }
    }
    for (uint32_t i = 0; i < ctx->binding_len; i++) {
        if (strcmp(ctx->bindings[i].name, name) == 0) return true;
    }
    return false;
}

static char *rho_print_fresh_name(RhoPrintCtx *ctx, const char *base) {
    for (;;) {
        char suffix[64];
        size_t base_len;
        size_t suffix_len;
        char *name;
        snprintf(suffix, sizeof(suffix), "_rho%u", ++ctx->fresh_counter);
        base_len = strlen(base);
        suffix_len = strlen(suffix);
        name = malloc(base_len + suffix_len + 1u);
        if (!name) {
            ctx->failed = true;
            return NULL;
        }
        memcpy(name, base, base_len);
        memcpy(name + base_len, suffix, suffix_len + 1u);
        if (!rho_print_name_conflicts(ctx, name)) return name;
        free(name);
    }
}

static uint32_t rho_print_binding_mark(RhoPrintCtx *ctx) {
    return ctx->binding_len;
}

static void rho_print_binding_pop(RhoPrintCtx *ctx, uint32_t mark) {
    while (ctx->binding_len > mark) {
        ctx->binding_len--;
        if (ctx->bindings[ctx->binding_len].owned) {
            free((char *)ctx->bindings[ctx->binding_len].name);
        }
    }
}

static bool rho_print_binding_push(RhoPrintCtx *ctx, Atom *var,
                                   const char **out_name) {
    const char *base = atom_name_cstr(var);
    const char *name = base;
    bool owned = false;
    if (rho_print_name_conflicts(ctx, base)) {
        name = rho_print_fresh_name(ctx, base);
        owned = true;
        if (!name) return false;
    }
    if (ctx->binding_len == ctx->binding_cap) {
        uint32_t next_cap = ctx->binding_cap ? ctx->binding_cap * 2u : 8u;
        RhoPrintBinding *next =
            cetta_realloc(ctx->bindings,
                          sizeof(RhoPrintBinding) * next_cap);
        if (!next) {
            if (owned) free((char *)name);
            ctx->failed = true;
            return false;
        }
        ctx->bindings = next;
        ctx->binding_cap = next_cap;
    }
    ctx->bindings[ctx->binding_len].id = var->var_id;
    ctx->bindings[ctx->binding_len].name = name;
    ctx->bindings[ctx->binding_len].owned = owned;
    ctx->binding_len++;
    *out_name = name;
    return true;
}

static void rho_print_ctx_free(RhoPrintCtx *ctx) {
    rho_print_binding_pop(ctx, 0);
    free(ctx->bindings);
    free(ctx->free_syms);
}

static void rho_print_mrho_atom(RhoPrintCtx *ctx, Atom *atom, FILE *out);
static void rho_print_surface_name(RhoPrintCtx *ctx, Atom *atom, FILE *out);
static void rho_print_surface_proc(RhoPrintCtx *ctx, Atom *atom, FILE *out);

static void rho_print_mrho_var(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    const char *name = rho_print_lookup(ctx, atom->var_id);
    fprintf(out, "$%s", name ? name : atom_name_cstr(atom));
}

static void rho_print_mrho_expr(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    fputc('(', out);
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (i > 0) fputc(' ', out);
        rho_print_mrho_atom(ctx, atom->expr.elems[i], out);
    }
    fputc(')', out);
}

static void rho_print_mrho_atom(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    if (!atom) return;
    if (atom->kind == ATOM_VAR) {
        rho_print_mrho_var(ctx, atom, out);
        return;
    }
    if (atom->kind != ATOM_EXPR) {
        atom_print(atom, out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:quote") && atom->expr.len == 2) {
        RhoPrintCtx inner = *ctx;
        inner.bindings = NULL;
        inner.binding_len = 0;
        inner.binding_cap = 0;
        fputs("(rho:quote ", out);
        rho_print_mrho_atom(&inner, atom->expr.elems[1], out);
        ctx->fresh_counter = inner.fresh_counter;
        if (inner.failed) ctx->failed = true;
        rho_print_binding_pop(&inner, 0);
        free(inner.bindings);
        fputc(')', out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:recv") && atom->expr.len == 4 &&
        atom->expr.elems[2]->kind == ATOM_VAR) {
        uint32_t mark = rho_print_binding_mark(ctx);
        const char *binder_name = NULL;
        fputs("(rho:recv ", out);
        rho_print_mrho_atom(ctx, atom->expr.elems[1], out);
        fputc(' ', out);
        if (!rho_print_binding_push(ctx, atom->expr.elems[2], &binder_name)) {
            atom_print(atom->expr.elems[2], out);
        } else {
            fprintf(out, "$%s", binder_name);
        }
        fputc(' ', out);
        rho_print_mrho_atom(ctx, atom->expr.elems[3], out);
        rho_print_binding_pop(ctx, mark);
        fputc(')', out);
        return;
    }
    rho_print_mrho_expr(ctx, atom, out);
}

static void rho_print_surface_name(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    if (atom->kind == ATOM_VAR) {
        rho_print_mrho_var(ctx, atom, out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:quote") && atom->expr.len == 2) {
        RhoPrintCtx inner = *ctx;
        inner.bindings = NULL;
        inner.binding_len = 0;
        inner.binding_cap = 0;
        fputs("@{", out);
        rho_print_surface_proc(&inner, atom->expr.elems[1], out);
        ctx->fresh_counter = inner.fresh_counter;
        if (inner.failed) ctx->failed = true;
        rho_print_binding_pop(&inner, 0);
        free(inner.bindings);
        fputs("}", out);
        return;
    }
    rho_print_mrho_atom(ctx, atom, out);
}

static void rho_print_surface_proc(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    if (atom_is_symbol(atom, "rho:nil")) {
        fputs("0", out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:par")) {
        fputc('{', out);
        for (uint32_t i = 1; i < atom->expr.len; i++) {
            if (i > 1) fputs(" | ", out);
            rho_print_surface_proc(ctx, atom->expr.elems[i], out);
        }
        fputc('}', out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:send") && atom->expr.len == 3) {
        rho_print_surface_name(ctx, atom->expr.elems[1], out);
        fputs("!(", out);
        rho_print_surface_proc(ctx, atom->expr.elems[2], out);
        fputs(")", out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:recv") && atom->expr.len == 4 &&
        atom->expr.elems[2]->kind == ATOM_VAR) {
        uint32_t mark = rho_print_binding_mark(ctx);
        const char *binder_name = NULL;
        fputs("for ($", out);
        if (!rho_print_binding_push(ctx, atom->expr.elems[2], &binder_name)) {
            atom_print(atom->expr.elems[2], out);
        } else {
            fputs(binder_name, out);
        }
        fputs(" <- ", out);
        rho_print_surface_name(ctx, atom->expr.elems[1], out);
        fputs(") {", out);
        rho_print_surface_proc(ctx, atom->expr.elems[3], out);
        rho_print_binding_pop(ctx, mark);
        fputc('}', out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:drop") && atom->expr.len == 2) {
        fputc('*', out);
        rho_print_surface_name(ctx, atom->expr.elems[1], out);
        return;
    }
    rho_print_mrho_atom(ctx, atom, out);
}

void rhocalc_print_atom_syntax(Atom *atom, CettaSyntaxId syntax, FILE *out) {
    RhoPrintCtx ctx = {0};
    RhoIdStack bound = {0};
    rho_print_collect_free(&ctx, atom, &bound);
    free(bound.items);
    if (ctx.failed) {
        rho_print_ctx_free(&ctx);
        atom_print(atom, out);
        return;
    }
    if (syntax == CETTA_SYNTAX_RHO) {
        rho_print_surface_proc(&ctx, atom, out);
    } else {
        rho_print_mrho_atom(&ctx, atom, out);
    }
    rho_print_ctx_free(&ctx);
}

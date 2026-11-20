#include "rhocalc_syntax.h"

#include "parser.h"
#include "rhocalc_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The text readers build terms without scope; the elaborator below binds
 * names for every reader, so all readers share one scoping law. */
typedef struct {
    const char *text;
    size_t pos;
    Arena *arena;
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
    VarId id;
    const char *name;
    bool owned;
} RhoPrintBinding;

/* The growable lists of the readers, the elaborator and the printer are
 * Cesta worklists (rhocalc_syntax_lists.export). */
typedef Atom *RhoSyntaxAtom;
static void *rho_syntax_resize(void *old, uint32_t bytes) { return cetta_realloc(old, bytes); }
#include "generated/rhocalc_syntax_lists.generated.h"

/* Which reader a term came from: it decides what a bare name means when no
 * receive binds it. */
typedef enum {
    RHO_READER_MRHO,
    RHO_READER_RHO,
    RHO_READER_RHO_COST,
} RhoReader;

typedef struct {
    Arena *arena;
    RhoReader reader;
    RhoElabBinding *bindings;
    uint32_t binding_len;
    uint32_t binding_cap;
    uint32_t floor;
    bool failed;
    bool reported;
} RhoElaborator;

static int rhocalc_elaborate_atoms(Arena *arena, Atom **atoms, int count,
                                   RhoReader reader);

static __thread char g_rhocalc_parse_error[256];

const char *rhocalc_last_parse_error(void) {
    return g_rhocalc_parse_error[0] ? g_rhocalc_parse_error : NULL;
}

static void rho_parse_clear_error(void) {
    g_rhocalc_parse_error[0] = '\0';
}

static bool rhocalc_semantic_profile_parse_supported(
    RhocalcSemanticProfileId profile) {
    switch (profile) {
    case RHOCALC_SEMANTIC_PROFILE_STRICT_CORE:
    case RHOCALC_SEMANTIC_PROFILE_COST:
    case RHOCALC_SEMANTIC_PROFILE_RHOMETTA:
        return true;
    }
    snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
             "unsupported rhocalc semantic profile '%s'",
             rhocalc_semantic_profile_name(profile));
    return false;
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
    return rho_syntax_push_atom(rho_syntax_resize, &vec->items, &vec->len, &vec->cap, atom);
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

static Atom *rho_cost_signed(Arena *arena, Atom *body, Atom *sig) {
    return rho_binary(arena, "rho:cost:signed", body, sig);
}

static Atom *rho_cost_nil(Arena *arena) {
    return atom_symbol(arena, "rho:cost:nil");
}

static Atom *rho_cost_stack_empty(Arena *arena) {
    return atom_symbol(arena, "rho:cost:stack-empty");
}

static Atom *rho_cost_stack_cons(Arena *arena, Atom *sig, Atom *rest) {
    return rho_binary(arena, "rho:cost:stack-cons", sig, rest);
}

static Atom *rho_cost_purse(Arena *arena, Atom *location, Atom *stack) {
    return rho_binary(arena, "rho:cost:purse", location, stack);
}

static bool rho_expr_head_named(Atom *atom, const char *name) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0 ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL)
        return false;
    const char *head = atom_name_cstr(atom->expr.elems[0]);
    /* Most expression heads are outside the rho namespace. Reject a
       different first byte without a full string comparison. */
    return head[0] == name[0] && strcmp(head, name) == 0;
}

static bool rhocalc_is_domain_proc_atom(Atom *atom);
static bool rhocalc_is_cost_term_atom(Atom *atom);

static bool rhocalc_is_cost_signature_atom(Atom *atom) {
    if (!atom) return false;
    if (atom->kind == ATOM_VAR) return false;
    if (atom->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr(atom);
        return strncmp(name, "rho:", 4) != 0;
    }
    if (rho_expr_head_named(atom, "rho:cost:sig-mul")) {
        if (atom->expr.len < 3) return false;
        for (uint32_t i = 1; i < atom->expr.len; i++) {
            if (!rhocalc_is_cost_signature_atom(atom->expr.elems[i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

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

static bool rhocalc_is_cost_name_atom(Atom *atom) {
    if (!atom) return false;
    if (atom->kind == ATOM_VAR) return true;
    if (rho_expr_head_named(atom, "rho:quote") && atom->expr.len == 2) {
        return rhocalc_is_cost_term_atom(atom->expr.elems[1]);
    }
    return rhocalc_is_cost_signature_atom(atom);
}

static bool rhocalc_is_cost_proc_atom(Atom *atom) {
    if (atom_is_symbol(atom, "rho:nil")) return true;
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) return false;

    if (rho_expr_head_named(atom, "rho:par")) {
        for (uint32_t i = 1; i < atom->expr.len; i++) {
            if (!rhocalc_is_cost_proc_atom(atom->expr.elems[i])) return false;
        }
        return true;
    }
    if (rho_expr_head_named(atom, "rho:send") && atom->expr.len == 3) {
        return rhocalc_is_cost_name_atom(atom->expr.elems[1]) &&
               rhocalc_is_cost_term_atom(atom->expr.elems[2]);
    }
    if (rho_expr_head_named(atom, "rho:recv") && atom->expr.len == 4) {
        return rhocalc_is_cost_name_atom(atom->expr.elems[1]) &&
               atom->expr.elems[2]->kind == ATOM_VAR &&
               rhocalc_is_cost_term_atom(atom->expr.elems[3]);
    }
    return false;
}

static bool rhocalc_is_cost_term_atom(Atom *atom) {
    if (atom_is_symbol(atom, "rho:cost:nil")) return true;
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) return false;
    if (rho_expr_head_named(atom, "rho:cost:par")) {
        for (uint32_t i = 1; i < atom->expr.len; i++) {
            if (!rhocalc_is_cost_term_atom(atom->expr.elems[i])) return false;
        }
        return true;
    }
    if (rho_expr_head_named(atom, "rho:cost:signed") && atom->expr.len == 3) {
        return rhocalc_is_cost_proc_atom(atom->expr.elems[1]) &&
               rhocalc_is_cost_signature_atom(atom->expr.elems[2]);
    }
    if (rho_expr_head_named(atom, "rho:drop") && atom->expr.len == 2) {
        return rhocalc_is_cost_name_atom(atom->expr.elems[1]);
    }
    if (rho_expr_head_named(atom, "rho:cost:purse") && atom->expr.len == 3) {
        Atom *stack = atom->expr.elems[2];
        if (!rhocalc_is_cost_name_atom(atom->expr.elems[1])) return false;
        while (rho_expr_head_named(stack, "rho:cost:stack-cons") &&
               stack->expr.len == 3) {
            if (!rhocalc_is_cost_signature_atom(stack->expr.elems[1])) return false;
            stack = stack->expr.elems[2];
        }
        return atom_is_symbol(stack, "rho:cost:stack-empty");
    }
    return false;
}

bool rhocalc_is_domain_atom(Atom *atom) {
    return rhocalc_is_domain_proc_atom(atom) ||
           rhocalc_is_cost_term_atom(atom);
}

static bool rp_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' ||
           c == '\f' || c == '\r';
}

static bool rp_ascii_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static bool rp_ascii_digit(char c) {
    return c >= '0' && c <= '9';
}

static void rp_skip_ws(RhoParser *parser) {
    for (;;) {
        while (rp_ascii_space(parser->text[parser->pos])) {
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
            size_t comment_start = parser->pos;
            parser->pos += 2;
            while (parser->text[parser->pos] &&
                   !(parser->text[parser->pos] == '*' &&
                     parser->text[parser->pos + 1] == '/')) {
                parser->pos++;
            }
            if (!parser->text[parser->pos]) {
                parser->pos = comment_start;
                rp_error(parser, "unterminated block comment");
                return;
            }
            parser->pos += 2;
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
    return c == '\0' || !(rp_ascii_alpha(c) || rp_ascii_digit(c) ||
                           c == '_' || c == '-');
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
    return rp_ascii_alpha(c) || c == '_';
}

static bool rp_ident_char(char c) {
    return rp_ascii_alpha(c) || rp_ascii_digit(c) || c == '_' ||
           c == '-' || c == '?';
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

static Atom *rp_parse_proc(RhoParser *parser);
static Atom *rc_parse_term(RhoParser *parser);

/* @{...}: a quote of what BODY parses.  Scope is the elaborator's. */
static Atom *rp_parse_quote(RhoParser *parser, Atom *(*body)(RhoParser *),
                            const char *close_error) {
    Atom *inner;
    parser->pos++;
    if (!rp_consume_char(parser, '{')) {
        rp_error(parser, "expected '{' after '@'");
        return NULL;
    }
    inner = body(parser);
    if (!inner) return NULL;
    if (!rp_consume_char(parser, '}')) {
        rp_error(parser, close_error);
        return NULL;
    }
    return rho_unary(parser->arena, "rho:quote", inner);
}

/* $x: bound by the receive in scope, free otherwise. */
static Atom *rp_parse_dollar_name(RhoParser *parser) {
    char *name;
    parser->pos++;
    name = rp_parse_ident(parser);
    if (!name) {
        rp_error(parser, "expected variable name after '$'");
        return NULL;
    }
    return atom_var(parser->arena, name);
}

/* A name written without '$'.  The elaborator binds it to the receive in
 * scope; the byte after it locates the error when none does. */
static Atom *rp_bare_name(RhoParser *parser, const char *name) {
    Atom *elems[3];
    elems[0] = atom_symbol(parser->arena, "rho:bare");
    elems[1] = atom_var(parser->arena, name);
    elems[2] = atom_int(parser->arena, (int64_t)parser->pos);
    return atom_expr(parser->arena, elems, 3);
}

static Atom *rp_parse_name(RhoParser *parser) {
    rp_skip_ws(parser);
    if (parser->text[parser->pos] == '@')
        return rp_parse_quote(parser, rp_parse_proc,
                              "expected '}' after quoted process");
    if (parser->text[parser->pos] == '$')
        return rp_parse_dollar_name(parser);
    if (rp_ident_start(parser->text[parser->pos]))
        return rp_bare_name(parser, rp_parse_ident(parser));
    rp_error(parser, "expected rho name");
    return NULL;
}

/* item (SEP item)*: one item as it is; several, the expression (HEAD item ...). */
static Atom *rp_parse_joined(RhoParser *parser, Atom *(*item)(RhoParser *), char sep,
                             const char *head) {
    RhoSyntaxVec items;
    Atom *out;
    rho_vec_init(&items);
    do {
        Atom *next = item(parser);
        if (!next) {
            rho_vec_free(&items);
            return NULL;
        }
        (void)rho_vec_push(&items, next);
    } while (rp_consume_char(parser, sep));
    out = items.len == 1 ? items.items[0]
                         : rho_call(parser->arena, head, items.items, items.len);
    rho_vec_free(&items);
    return out;
}

static Atom *rp_parse_proc_atom(RhoParser *parser);

static Atom *rp_parse_proc(RhoParser *parser) {
    return rp_parse_joined(parser, rp_parse_proc_atom, '|', "rho:par");
}

static Atom *rc_parse_term(RhoParser *parser);
static Atom *rc_parse_name(RhoParser *parser);

static bool rc_is_signature_start(RhoParser *parser) {
    rp_skip_ws(parser);
    return rp_ident_start(parser->text[parser->pos]);
}

static Atom *rc_parse_signature_atom(RhoParser *parser) {
    char *name;
    rp_skip_ws(parser);
    name = rp_parse_ident(parser);
    if (!name) {
        rp_error(parser, "expected ground signature");
        return NULL;
    }
    return atom_symbol(parser->arena, name);
}

static Atom *rc_parse_signature(RhoParser *parser) {
    return rp_parse_joined(parser, rc_parse_signature_atom, '*', "rho:cost:sig-mul");
}

static Atom *rc_parse_name(RhoParser *parser) {
    rp_skip_ws(parser);
    if (parser->text[parser->pos] == '@')
        return rp_parse_quote(parser, rc_parse_term,
                              "expected '}' after quoted signed term");
    if (parser->text[parser->pos] == '$')
        return rp_parse_dollar_name(parser);
    if (rp_ident_start(parser->text[parser->pos])) {
        /* A product a*b is a signature; a single word is a bound name when a
           receive binds it and a signature otherwise. */
        size_t start = parser->pos;
        Atom *bare = rp_bare_name(parser, rp_parse_ident(parser));
        if (!rp_consume_char(parser, '*')) return bare;
        parser->pos = start;
        return rc_parse_signature(parser);
    }
    rp_error(parser, "expected cost-rho name");
    return NULL;
}

static Atom *rc_parse_proc(RhoParser *parser);

/* The pure and cost text grammars read processes alike, but for how a channel
 * is read, what a send carries and a receive runs, and whether *x is a
 * process (in the cost grammar it is a signed term). */
typedef struct {
    Atom *(*name)(RhoParser *);
    Atom *(*payload)(RhoParser *);
    Atom *(*proc)(RhoParser *);
    bool drop_is_process;
} RhoTextGrammar;

static Atom *rho_parse_proc_atom(RhoParser *parser, const RhoTextGrammar *g) {
    rp_skip_ws(parser);
    if (rp_consume_char(parser, '{')) {
        Atom *inner = g->proc(parser);
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
        Atom *channel;
        Atom *body;
        if (!rp_consume_char(parser, '(')) {
            rp_error(parser, "expected '(' after for");
            return NULL;
        }
        (void)rp_consume_char(parser, '$');   /* $x and x name one binder */
        binder_name = rp_parse_ident(parser);
        if (!binder_name) {
            rp_error(parser, "expected input binder");
            return NULL;
        }
        if (!rp_consume_span(parser, "<-")) {
            rp_error(parser, "expected '<-' in input");
            return NULL;
        }
        channel = g->name(parser);
        if (!channel) return NULL;
        if (!rp_consume_char(parser, ')')) {
            rp_error(parser, "expected ')' after input channel");
            return NULL;
        }
        if (!rp_consume_char(parser, '{')) {
            rp_error(parser, "expected input body block");
            return NULL;
        }
        Atom *binder = atom_var(parser->arena, binder_name);
        body = g->payload(parser);
        if (!body) return NULL;
        if (!rp_consume_char(parser, '}')) {
            rp_error(parser, "expected '}' after input body");
            return NULL;
        }
        return rho_ternary(parser->arena, "rho:recv", channel, binder, body);
    }
    if (g->drop_is_process && rp_consume_char(parser, '*')) {
        Atom *name = g->name(parser);
        if (!name) return NULL;
        return rho_unary(parser->arena, "rho:drop", name);
    }
    Atom *channel = g->name(parser);
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
    payload = g->payload(parser);
    if (!payload) return NULL;
    if (!rp_consume_char(parser, ')')) {
        rp_error(parser, "expected ')' after output payload");
        return NULL;
    }
    return rho_binary(parser->arena, "rho:send", channel, payload);
}

static Atom *rp_parse_proc_atom(RhoParser *parser) {
    static const RhoTextGrammar pure = {rp_parse_name, rp_parse_proc, rp_parse_proc, true};
    return rho_parse_proc_atom(parser, &pure);
}

static Atom *rc_parse_proc_atom(RhoParser *parser) {
    static const RhoTextGrammar cost = {rc_parse_name, rc_parse_term, rc_parse_proc, false};
    return rho_parse_proc_atom(parser, &cost);
}

static Atom *rc_parse_proc(RhoParser *parser) {
    return rp_parse_joined(parser, rc_parse_proc_atom, '|', "rho:par");
}

static Atom *rc_parse_stack(RhoParser *parser) {
    Atom *sig;
    Atom *rest;
    if (rp_consume_span(parser, "()")) {
        return rho_cost_stack_empty(parser->arena);
    }
    sig = rc_parse_signature(parser);
    if (!sig) return NULL;
    if (!rp_consume_char(parser, ':')) {
        rp_error(parser, "expected ':' in token stack");
        return NULL;
    }
    rest = rc_parse_stack(parser);
    if (!rest) return NULL;
    return rho_cost_stack_cons(parser->arena, sig, rest);
}

static Atom *rc_parse_term_atom(RhoParser *parser) {
    rp_skip_ws(parser);
    if (strncmp(parser->text + parser->pos, "()", 2) == 0) {
        parser->pos += 2;
        return rho_cost_nil(parser->arena);
    }
    if (rp_consume_keyword(parser, "purse")) {
        Atom *location = rc_parse_name(parser);
        Atom *stack;
        if (!location) return NULL;
        if (!rp_consume_char(parser, '{')) {
            rp_error(parser, "expected '{' after purse location");
            return NULL;
        }
        stack = rc_parse_stack(parser);
        if (!stack) return NULL;
        if (!rp_consume_char(parser, '}')) {
            rp_error(parser, "expected '}' after purse stack");
            return NULL;
        }
        return rho_cost_purse(parser->arena, location, stack);
    }
    if (rp_consume_char(parser, '*')) {
        Atom *name = rc_parse_name(parser);
        if (!name) return NULL;
        return rho_unary(parser->arena, "rho:drop", name);
    }
    if (parser->text[parser->pos] == '{') {
        Atom *proc;
        Atom *sig;
        parser->pos++;
        proc = rc_parse_proc(parser);
        if (!proc) return NULL;
        if (!rp_consume_char(parser, '}')) {
            rp_error(parser, "expected '}' after signed process body");
            return NULL;
        }
        if (!rc_is_signature_start(parser)) {
            rp_error(parser, "expected signature after signed process");
            return NULL;
        }
        sig = rc_parse_signature(parser);
        if (!sig) return NULL;
        return rho_cost_signed(parser->arena, proc, sig);
    }
    rp_error(parser, "expected cost-rho signed term");
    return NULL;
}

static Atom *rc_parse_term(RhoParser *parser) {
    return rp_parse_joined(parser, rc_parse_term_atom, '|', "rho:cost:par");
}

/* Reads Rholang-style text: the pure grammar, or the cost grammar.  The
 * elaborator links a receive to its occurrences by spelling, which takes one
 * variable identity per spelling for the whole text; a caller without an
 * intern table gets one for the read. */
static int rhocalc_parse_written(const char *text, Arena *arena,
                                 Atom ***out_atoms, RhoReader reader) {
    RhoParser parser = {0};
    VarInternTable local;
    bool own_intern = g_var_intern == NULL;
    Atom *atom;
    int n = -1;
    rho_parse_clear_error();
    *out_atoms = NULL;
    parser.text = text ? text : "";
    parser.arena = arena;
    if (own_intern) {
        var_intern_init(&local);
        g_var_intern = &local;
    }
    atom = reader == RHO_READER_RHO_COST ? rc_parse_term(&parser)
                                         : rp_parse_proc(&parser);
    rp_skip_ws(&parser);
    if (!atom || parser.error[0] || parser.text[parser.pos] != '\0') {
        if (!parser.error[0]) rp_error(&parser, "unexpected trailing input");
        rho_parse_store_error(&parser);
    } else if (rhocalc_elaborate_atoms(arena, &atom, 1, reader) == 1) {
        *out_atoms = cetta_malloc(sizeof(Atom *));
        (*out_atoms)[0] = atom;
        n = 1;
    }
    if (own_intern) {
        g_var_intern = NULL;
        var_intern_free(&local);
    }
    return n;
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

/* The receive binder in scope for a name: bindings below the floor belong to
 * the scope a quote sealed off. */
static Atom *rho_elab_lookup(RhoElaborator *elab, SymbolId spelling,
                             VarId source_var_id) {
    for (uint32_t i = elab->binding_len; i > elab->floor; i--) {
        if (elab->bindings[i - 1].spelling == spelling &&
            var_id_eq(elab->bindings[i - 1].source_var_id, source_var_id)) {
            return elab->bindings[i - 1].var;
        }
    }
    return NULL;
}

static bool rho_elab_push(RhoElaborator *elab, SymbolId spelling,
                          VarId source_var_id, Atom *var) {
    RhoElabBinding binding = {spelling, source_var_id, var};
    if (!rho_syntax_push_binding(rho_syntax_resize, &elab->bindings, &elab->binding_len,
                                 &elab->binding_cap, binding)) {
        elab->failed = true;
        return false;
    }
    return true;
}

/* Counts PROC's parallel components up to P | 0 = P, keeping the last one,
 * and stops at two.  PAR and UNIT are the calculus's own (pure or cost). */
static void rho_quote_components(Atom *proc, const char *par, const char *unit,
                                 Atom **last, uint32_t *count) {
    if (*count > 1u || atom_is_symbol(proc, unit)) return;
    if (rho_expr_head_named(proc, par)) {
        for (uint32_t i = 1; i < proc->expr.len && *count <= 1u; i++)
            rho_quote_components(proc->expr.elems[i], par, unit, last, count);
        return;
    }
    *last = proc;
    (*count)++;
}

/* Quote-drop (Meredith and Radestock 2005, section 2.4): @*x is the name x.
 * A quote whose process is a single drop, up to P | 0 = P, is the dropped
 * name; any other quote is a closed name.  Returns the dropped name or NULL. */
static Atom *rho_quote_dropped_name(Atom *quote) {
    Atom *body = quote->expr.elems[1];
    bool cost = rho_expr_head_named(body, "rho:cost:par") ||
                atom_is_symbol(body, "rho:cost:nil");
    Atom *only = NULL;
    uint32_t count = 0u;
    rho_quote_components(body, cost ? "rho:cost:par" : "rho:par",
                         cost ? "rho:cost:nil" : "rho:nil", &only, &count);
    return count == 1u && rho_expr_head_named(only, "rho:drop") &&
                   only->expr.len == 2u
               ? only->expr.elems[1]
               : NULL;
}

/* A name written without '$' in the written syntax: the receive in scope
 * that binds its spelling.  Unbound, the cost reader reads the signature of
 * that spelling; the pure reader has no such name. */
static Atom *rho_elab_bare_name(RhoElaborator *elab, Atom *bare) {
    Atom *source = bare->expr.elems[1];
    Atom *bound = rho_elab_lookup(elab, source->sym_id, source->var_id);
    if (bound) return bound;
    if (elab->reader == RHO_READER_RHO_COST)
        return atom_symbol_id(elab->arena, source->sym_id);
    if (!elab->failed)
        snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
                 "bare names are not core rho names; use @{...} near byte %lld",
                 (long long)bare->expr.elems[2]->ground.ival);
    elab->failed = true;
    elab->reported = true;
    return bare;
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
            /* A quote that is a dropped name is that name, read where it is
               written.  Any other quote is static data: receive binders
               outside it do not reach in (section 2.0.4, impervious to
               substitution), and binders inside it bind within it. */
            if (rho_quote_dropped_name(atom))
                return rho_elab_expr_generic(elab, atom);
            uint32_t floor = elab->floor;
            uint32_t mark = rho_elab_mark(elab);
            Atom *elems[2];
            elab->floor = mark;
            elems[0] = atom->expr.elems[0];
            elems[1] = rho_elab_atom(elab, atom->expr.elems[1]);
            rho_elab_pop(elab, mark);
            elab->floor = floor;
            return atom_expr(elab->arena, elems, 2);
        }
        if (elab->reader != RHO_READER_MRHO &&
            rho_expr_head_named(atom, "rho:bare") && atom->expr.len == 3)
            return rho_elab_bare_name(elab, atom);
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

static int rhocalc_elaborate_atoms(Arena *arena, Atom **atoms, int count,
                                   RhoReader reader) {
    RhoElaborator elab = {0};
    elab.arena = arena;
    elab.reader = reader;
    for (int i = 0; i < count; i++) {
        atoms[i] = rho_elab_atom(&elab, atoms[i]);
        elab.binding_len = 0;
        if (elab.failed) break;
    }
    free(elab.bindings);
    if (elab.failed) {
        if (!elab.reported)
            snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
                     "could not elaborate rhocalc/mrho binders");
        return -1;
    }
    return count;
}

Atom *rhocalc_elaborate_mrho_atom(Arena *arena, Atom *atom) {
    rho_parse_clear_error();
    if (rhocalc_elaborate_atoms(arena, &atom, 1, RHO_READER_MRHO) < 0)
        return NULL;
    return atom;
}

/* Reads a source, the text itself or (is_path) a file's, in a syntax. */
static int rhocalc_parse_source(const char *source, bool is_path,
                                RhocalcSemanticProfileId semantic_profile,
                                CettaSyntaxId syntax, Arena *arena, Atom ***out_atoms) {
    if (!out_atoms) return -1;
    *out_atoms = NULL;
    rho_parse_clear_error();
    if (!rhocalc_semantic_profile_parse_supported(semantic_profile)) {
        return -1;
    }
    if (syntax == CETTA_SYNTAX_METTA || syntax == CETTA_SYNTAX_MRHO) {
        int n = is_path ? parse_metta_file(source, arena, out_atoms)
                        : parse_metta_text(source, arena, out_atoms);
        if (syntax == CETTA_SYNTAX_METTA &&
            semantic_profile == RHOCALC_SEMANTIC_PROFILE_RHOMETTA)
            return n;
        if (n < 0) return n;
        return rhocalc_elaborate_atoms(arena, *out_atoms, n, RHO_READER_MRHO);
    }
    if (syntax == CETTA_SYNTAX_RHO) {
        char *text = is_path ? rhocalc_read_file(source) : NULL;
        if (is_path && !text) {
            snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
                     "could not read %s", source ? source : "<null>");
            return -1;
        }
        int n = rhocalc_parse_written(
            is_path ? text : source, arena, out_atoms,
            semantic_profile == RHOCALC_SEMANTIC_PROFILE_COST
                ? RHO_READER_RHO_COST : RHO_READER_RHO);
        free(text);
        return n;
    }
    snprintf(g_rhocalc_parse_error, sizeof(g_rhocalc_parse_error),
             "unsupported rhocalc syntax '%s'", cetta_syntax_name(syntax));
    return -1;
}

int rhocalc_parse_text(const char *text, RhocalcSemanticProfileId semantic_profile,
                       CettaSyntaxId syntax, Arena *arena, Atom ***out_atoms) {
    return rhocalc_parse_source(text, false, semantic_profile, syntax, arena, out_atoms);
}

int rhocalc_parse_file(const char *path, RhocalcSemanticProfileId semantic_profile,
                       CettaSyntaxId syntax, Arena *arena, Atom ***out_atoms) {
    return rhocalc_parse_source(path, true, semantic_profile, syntax, arena, out_atoms);
}

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
    return rho_syntax_push_var(rho_syntax_resize, &stack->items, &stack->len, &stack->cap, id);
}

static bool rho_id_stack_contains(RhoIdStack *stack, VarId id) {
    for (uint32_t i = stack->len; i > 0; i--) {
        if (var_id_eq(stack->items[i - 1], id)) return true;
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
    if (!rho_syntax_push_symbol(rho_syntax_resize, &ctx->free_syms, &ctx->free_len,
                                &ctx->free_cap, sym))
        ctx->failed = true;
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

    /* A dropped-name quote is read in the surrounding scope; any other quote
       is closed. */
    if (rho_expr_head_named(atom, "rho:quote") && atom->expr.len == 2 &&
        !rho_quote_dropped_name(atom)) {
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
        if (var_id_eq(ctx->bindings[i - 1].id, id)) return ctx->bindings[i - 1].name;
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
    RhoPrintBinding binding = {var->var_id, name, owned};
    if (!rho_syntax_push_print_binding(rho_syntax_resize, &ctx->bindings, &ctx->binding_len,
                                       &ctx->binding_cap, binding)) {
        if (owned) free((char *)name);
        ctx->failed = true;
        return false;
    }
    *out_name = name;
    return true;
}

static void rho_print_ctx_free(RhoPrintCtx *ctx) {
    rho_print_binding_pop(ctx, 0);
    free(ctx->bindings);
    free(ctx->free_syms);
}

static void rho_print_mrho_atom(RhoPrintCtx *ctx, Atom *atom, FILE *out);
static void rho_print_syntax_signature(RhoPrintCtx *ctx, Atom *atom, FILE *out);
static void rho_print_syntax_name(RhoPrintCtx *ctx, Atom *atom, FILE *out);
static void rho_print_syntax_proc(RhoPrintCtx *ctx, Atom *atom, FILE *out);
static void rho_print_syntax_stack(RhoPrintCtx *ctx, Atom *atom, FILE *out);
static void rho_print_syntax_term(RhoPrintCtx *ctx, Atom *atom, FILE *out);

/* What a send carries or a receive runs: a signed term, or a process. */
static void rho_print_syntax_body(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    if (rhocalc_is_cost_term_atom(atom)) rho_print_syntax_term(ctx, atom, out);
    else rho_print_syntax_proc(ctx, atom, out);
}

/* A closed quote is printed in a scope of its own: binders outside it do not
 * reach in, and its fresh names continue the outer count. */
static void rho_print_scope_open(const RhoPrintCtx *ctx, RhoPrintCtx *inner) {
    *inner = *ctx;
    inner->bindings = NULL;
    inner->binding_len = 0;
    inner->binding_cap = 0;
}

static void rho_print_scope_close(RhoPrintCtx *ctx, RhoPrintCtx *inner) {
    ctx->fresh_counter = inner->fresh_counter;
    if (inner->failed) ctx->failed = true;
    rho_print_binding_pop(inner, 0);
    free(inner->bindings);
}

static void rho_print_syntax_signature(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    (void)ctx;
    if (atom && atom->kind == ATOM_SYMBOL) {
        atom_print(atom, out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:cost:sig-mul")) {
        for (uint32_t i = 1; i < atom->expr.len; i++) {
            if (i > 1) fputs(" * ", out);
            rho_print_syntax_signature(ctx, atom->expr.elems[i], out);
        }
        return;
    }
    rho_print_mrho_atom(ctx, atom, out);
}

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
    if (rho_expr_head_named(atom, "rho:quote") && atom->expr.len == 2 &&
        !rho_quote_dropped_name(atom)) {
        RhoPrintCtx inner;
        rho_print_scope_open(ctx, &inner);
        fputs("(rho:quote ", out);
        rho_print_mrho_atom(&inner, atom->expr.elems[1], out);
        rho_print_scope_close(ctx, &inner);
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

static void rho_print_syntax_name(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    if (atom->kind == ATOM_VAR) {
        rho_print_mrho_var(ctx, atom, out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:quote") && atom->expr.len == 2) {
        bool closed = !rho_quote_dropped_name(atom);
        RhoPrintCtx inner;
        if (closed) rho_print_scope_open(ctx, &inner);
        fputs("@{", out);
        rho_print_syntax_body(closed ? &inner : ctx, atom->expr.elems[1], out);
        if (closed) rho_print_scope_close(ctx, &inner);
        fputs("}", out);
        return;
    }
    if (rhocalc_is_cost_signature_atom(atom)) {
        rho_print_syntax_signature(ctx, atom, out);
        return;
    }
    rho_print_mrho_atom(ctx, atom, out);
}

static void rho_print_syntax_term(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    if (atom_is_symbol(atom, "rho:cost:nil")) {
        fputs("()", out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:drop") && atom->expr.len == 2) {
        fputc('*', out);
        rho_print_syntax_name(ctx, atom->expr.elems[1], out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:cost:purse") && atom->expr.len == 3) {
        fputs("purse ", out);
        rho_print_syntax_name(ctx, atom->expr.elems[1], out);
        fputs(" {", out);
        rho_print_syntax_stack(ctx, atom->expr.elems[2], out);
        fputc('}', out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:cost:signed") && atom->expr.len == 3) {
        fputc('{', out);
        rho_print_syntax_proc(ctx, atom->expr.elems[1], out);
        fputc('}', out);
        rho_print_syntax_signature(ctx, atom->expr.elems[2], out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:cost:par")) {
        for (uint32_t i = 1; i < atom->expr.len; i++) {
            if (i > 1) fputs(" | ", out);
            rho_print_syntax_term(ctx, atom->expr.elems[i], out);
        }
        return;
    }
    rho_print_mrho_atom(ctx, atom, out);
}

static void rho_print_syntax_stack(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    if (atom_is_symbol(atom, "rho:cost:stack-empty")) {
        fputs("()", out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:cost:stack-cons") && atom->expr.len == 3) {
        rho_print_syntax_signature(ctx, atom->expr.elems[1], out);
        fputs(" : ", out);
        rho_print_syntax_stack(ctx, atom->expr.elems[2], out);
        return;
    }
    rho_print_mrho_atom(ctx, atom, out);
}

static void rho_print_syntax_proc(RhoPrintCtx *ctx, Atom *atom, FILE *out) {
    if (atom_is_symbol(atom, "rho:nil")) {
        fputs("0", out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:par")) {
        fputc('{', out);
        for (uint32_t i = 1; i < atom->expr.len; i++) {
            if (i > 1) fputs(" | ", out);
            rho_print_syntax_proc(ctx, atom->expr.elems[i], out);
        }
        fputc('}', out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:send") && atom->expr.len == 3) {
        rho_print_syntax_name(ctx, atom->expr.elems[1], out);
        fputs("!(", out);
        rho_print_syntax_body(ctx, atom->expr.elems[2], out);
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
        rho_print_syntax_name(ctx, atom->expr.elems[1], out);
        fputs(") {", out);
        rho_print_syntax_body(ctx, atom->expr.elems[3], out);
        rho_print_binding_pop(ctx, mark);
        fputc('}', out);
        return;
    }
    if (rho_expr_head_named(atom, "rho:drop") && atom->expr.len == 2) {
        fputc('*', out);
        rho_print_syntax_name(ctx, atom->expr.elems[1], out);
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
        rho_print_syntax_body(&ctx, atom, out);
    } else {
        rho_print_mrho_atom(&ctx, atom, out);
    }
    rho_print_ctx_free(&ctx);
}

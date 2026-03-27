#include "native/metamath/parser.h"
#include "text_source.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CETTA_MM_MAX_INCLUDE_DEPTH 64
#define CETTA_MM_MAX_COMPLETED_FILES 256

typedef struct {
    char text[PATH_MAX];
    size_t len;
    size_t cap;
} CettaMMStringBuf;

typedef CettaTextSource CettaMMSource;

struct CettaMetamathParser {
    bool pverify_shape;
    bool dag_format;
    int scope_depth;
    CettaMMSource sources[CETTA_MM_MAX_INCLUDE_DEPTH];
    uint32_t source_len;
    char completed_paths[CETTA_MM_MAX_COMPLETED_FILES][PATH_MAX];
    uint32_t completed_len;
};

typedef struct {
    char *text;
} CettaMMToken;

static void mm_set_error(char *errbuf, size_t errbuf_sz, const char *message) {
    if (!errbuf || errbuf_sz == 0) return;
    snprintf(errbuf, errbuf_sz, "%s", message ? message : "metamath-error");
}

static bool mm_copy_text(char *dst, size_t dst_sz, const char *src) {
    size_t len;
    if (!dst || dst_sz == 0 || !src) return false;
    len = strlen(src);
    if (len >= dst_sz) return false;
    memmove(dst, src, len + 1);
    return true;
}

static char *mm_strdup(const char *text) {
    size_t len = strlen(text);
    char *copy = cetta_malloc(len + 1);
    memcpy(copy, text, len + 1);
    return copy;
}

static void mm_sb_init(CettaMMStringBuf *sb) {
    sb->text[0] = '\0';
    sb->len = 0;
    sb->cap = sizeof(sb->text);
}

static bool mm_sb_append_char(CettaMMStringBuf *sb, char ch) {
    if (sb->len + 1 >= sb->cap) return false;
    sb->text[sb->len++] = ch;
    sb->text[sb->len] = '\0';
    return true;
}

static bool mm_is_whitespace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f';
}

static bool mm_is_printable(int ch) {
    return ch >= 33 && ch <= 126;
}

static bool mm_is_valid_char(int ch) {
    return mm_is_printable(ch) || mm_is_whitespace(ch);
}

static bool mm_is_keyword_char(int ch) {
    switch (ch) {
    case 'c':
    case 'v':
    case 'f':
    case 'e':
    case 'a':
    case 'p':
    case 'd':
    case '{':
    case '}':
    case '=':
    case '.':
    case '[':
    case ']':
        return true;
    default:
        return false;
    }
}

static bool mm_is_label_char(int ch) {
    return isalnum((unsigned char)ch) || ch == '-' || ch == '_' || ch == '.';
}

static bool mm_valid_label(const char *label) {
    if (!label || !*label) return false;
    for (const unsigned char *p = (const unsigned char *)label; *p; p++) {
        if (!mm_is_label_char(*p)) return false;
    }
    return true;
}

static bool mm_valid_math_symbol(const char *symbol) {
    if (!symbol || !*symbol) return false;
    for (const unsigned char *p = (const unsigned char *)symbol; *p; p++) {
        if (!mm_is_printable(*p) || *p == '$') return false;
    }
    return true;
}

static CettaMMSource *mm_current_source(CettaMetamathParser *parser) {
    if (!parser || parser->source_len == 0) return NULL;
    return &parser->sources[parser->source_len - 1];
}

static void mm_source_advance(CettaMMSource *src, size_t count) {
    cetta_text_source_advance(src, count);
}

static int mm_source_peek(CettaMMSource *src, size_t offset) {
    int ch = -1;
    if (!src) return -1;
    if (!cetta_text_source_peek(src, offset, &ch, NULL, 0)) return -1;
    return ch;
}

static bool mm_completed_contains(CettaMetamathParser *parser, const char *path) {
    for (uint32_t i = 0; i < parser->completed_len; i++) {
        if (strcmp(parser->completed_paths[i], path) == 0) return true;
    }
    return false;
}

static bool mm_active_contains(CettaMetamathParser *parser, const char *path) {
    for (uint32_t i = 0; i < parser->source_len; i++) {
        if (strcmp(parser->sources[i].path, path) == 0) return true;
    }
    return false;
}

static void mm_mark_completed(CettaMetamathParser *parser, const char *path) {
    if (!parser || !path || !*path) return;
    if (mm_completed_contains(parser, path)) return;
    if (parser->completed_len >= CETTA_MM_MAX_COMPLETED_FILES) return;
    if (!mm_copy_text(parser->completed_paths[parser->completed_len],
                      sizeof(parser->completed_paths[parser->completed_len]),
                      path)) {
        return;
    }
    parser->completed_len++;
}

static void mm_pop_source(CettaMetamathParser *parser) {
    CettaMMSource *src = mm_current_source(parser);
    if (!src) return;
    mm_mark_completed(parser, src->path);
    cetta_text_source_close(src);
    parser->source_len--;
}

static bool mm_push_source(CettaMetamathParser *parser, const char *path,
                           char *errbuf, size_t errbuf_sz) {
    CettaMMSource *src;
    char resolved[PATH_MAX];

    if (!parser || !path) return false;
    if (parser->source_len >= CETTA_MM_MAX_INCLUDE_DEPTH) {
        mm_set_error(errbuf, errbuf_sz, "include-depth");
        return false;
    }
    if (!cetta_text_path_resolve(parser->source_len > 0 ? mm_current_source(parser)->dir : ".",
                                 path, resolved, sizeof(resolved))) {
        mm_set_error(errbuf, errbuf_sz, "path-too-long");
        return false;
    }
    if (mm_active_contains(parser, resolved)) {
        mm_set_error(errbuf, errbuf_sz, "circular-include");
        return false;
    }
    if (mm_completed_contains(parser, resolved)) {
        return true;
    }

    src = &parser->sources[parser->source_len];
    memset(src, 0, sizeof(*src));
    if (!cetta_text_source_open(src, parser->source_len > 0 ? mm_current_source(parser)->dir : ".",
                                path, errbuf, errbuf_sz)) {
        return false;
    }
    parser->source_len++;
    return true;
}

static bool mm_skip_comment(CettaMetamathParser *parser, char *errbuf, size_t errbuf_sz) {
    CettaMMSource *src = mm_current_source(parser);
    if (!src) {
        mm_set_error(errbuf, errbuf_sz, "unclosed-comment");
        return false;
    }
    mm_source_advance(src, 2);
    while (true) {
        int ch0 = mm_source_peek(src, 0);
        int ch1 = mm_source_peek(src, 1);
        if (ch0 < 0) {
            mm_set_error(errbuf, errbuf_sz, "unclosed-comment");
            return false;
        }
        if (!mm_is_valid_char(ch0)) {
            mm_set_error(errbuf, errbuf_sz, "invalid-character");
            return false;
        }
        if (ch0 == '$' && ch1 == '(') {
            mm_set_error(errbuf, errbuf_sz, "nested-comment");
            return false;
        }
        if (ch0 == '$' && ch1 == ')') {
            mm_source_advance(src, 2);
            ch0 = mm_source_peek(src, 0);
            if (ch0 >= 0 && !mm_is_whitespace(ch0)) {
                mm_set_error(errbuf, errbuf_sz, "missing-whitespace-after-comment");
                return false;
            }
            return true;
        }
        mm_source_advance(src, 1);
    }
}

static bool mm_skip_ws_and_comments(CettaMetamathParser *parser,
                                    char *errbuf, size_t errbuf_sz) {
    while (true) {
        CettaMMSource *src = mm_current_source(parser);
        int ch0;
        int ch1;
        if (!src) return true;
        ch0 = mm_source_peek(src, 0);
        if (ch0 < 0) {
            mm_pop_source(parser);
            continue;
        }
        if (!mm_is_valid_char(ch0)) {
            mm_set_error(errbuf, errbuf_sz, "invalid-character");
            return false;
        }
        if (mm_is_whitespace(ch0)) {
            mm_source_advance(src, 1);
            continue;
        }
        ch1 = mm_source_peek(src, 1);
        if (ch0 == '$' && ch1 == '(') {
            if (!mm_skip_comment(parser, errbuf, errbuf_sz)) return false;
            continue;
        }
        return true;
    }
}

static void mm_token_free(CettaMMToken *tok) {
    if (!tok) return;
    free(tok->text);
    tok->text = NULL;
}

static bool mm_read_token(CettaMetamathParser *parser, CettaMMToken *tok,
                          bool *eof_out, char *errbuf, size_t errbuf_sz) {
    CettaMMSource *src;
    CettaMMStringBuf sb;
    int ch0;
    int ch1;

    tok->text = NULL;
    *eof_out = false;
    if (!mm_skip_ws_and_comments(parser, errbuf, errbuf_sz)) return false;
    src = mm_current_source(parser);
    if (!src) {
        *eof_out = true;
        return true;
    }

    ch0 = mm_source_peek(src, 0);
    if (ch0 < 0) {
        *eof_out = true;
        return true;
    }
    if (ch0 == '$') {
        ch1 = mm_source_peek(src, 1);
        if (ch1 < 0) {
            mm_set_error(errbuf, errbuf_sz, "dangling-dollar");
            return false;
        }
        if (!mm_is_keyword_char(ch1)) {
            mm_set_error(errbuf, errbuf_sz, "dangling-dollar");
            return false;
        }
        mm_sb_init(&sb);
        if (!mm_sb_append_char(&sb, '$') || !mm_sb_append_char(&sb, (char)ch1)) {
            mm_set_error(errbuf, errbuf_sz, "token-too-long");
            return false;
        }
        mm_source_advance(src, 2);
        ch0 = mm_source_peek(src, 0);
        if (ch0 >= 0 && !mm_is_whitespace(ch0)) {
            mm_set_error(errbuf, errbuf_sz, "missing-whitespace-after-keyword");
            return false;
        }
        tok->text = mm_strdup(sb.text);
        return true;
    }

    mm_sb_init(&sb);
    while (true) {
        ch0 = mm_source_peek(src, 0);
        if (ch0 < 0 || mm_is_whitespace(ch0) || ch0 == '$') break;
        if (!mm_is_printable(ch0)) {
            mm_set_error(errbuf, errbuf_sz, "invalid-character");
            return false;
        }
        if (!mm_sb_append_char(&sb, (char)ch0)) {
            mm_set_error(errbuf, errbuf_sz, "token-too-long");
            return false;
        }
        mm_source_advance(src, 1);
    }
    if (sb.len == 0) {
        mm_set_error(errbuf, errbuf_sz, "empty-token");
        return false;
    }
    tok->text = mm_strdup(sb.text);
    return true;
}

static bool mm_is_stmt_keyword(const char *tok) {
    return tok &&
           (strcmp(tok, "$c") == 0 || strcmp(tok, "$v") == 0 ||
            strcmp(tok, "$f") == 0 || strcmp(tok, "$e") == 0 ||
            strcmp(tok, "$a") == 0 || strcmp(tok, "$p") == 0 ||
            strcmp(tok, "$d") == 0);
}

static bool mm_push_string(char ***items, uint32_t *len, uint32_t *cap, char *item) {
    if (*len >= *cap) {
        uint32_t next = *cap ? (*cap * 2) : 8;
        char **grown = cetta_realloc(*items, sizeof(char *) * next);
        *items = grown;
        *cap = next;
    }
    (*items)[(*len)++] = item;
    return true;
}

static void mm_free_string_vec(char **items, uint32_t len) {
    if (!items) return;
    for (uint32_t i = 0; i < len; i++) free(items[i]);
    free(items);
}

static Atom *mm_token_atom(Arena *a, const char *tok, bool as_string) {
    const char *mapped = tok;
    if (strcmp(tok, "=") == 0) mapped = "mm.=";
    else if (strcmp(tok, "+") == 0) mapped = "mm.+";
    else if (strcmp(tok, "-") == 0) mapped = "mm.-";
    else if (strcmp(tok, "*") == 0) mapped = "mm.*";
    else if (strcmp(tok, "/") == 0) mapped = "mm./";
    else if (strcmp(tok, "<") == 0) mapped = "mm.<";
    else if (strcmp(tok, ">") == 0) mapped = "mm.>";
    else if (strcmp(tok, "->") == 0) mapped = "mm.->";
    return as_string ? atom_string(a, mapped) : atom_symbol(a, mapped);
}

static Atom *mm_tokens_expr(Arena *a, char **items, uint32_t len, bool as_string) {
    Atom **atoms = arena_alloc(a, sizeof(Atom *) * (len ? len : 1));
    for (uint32_t i = 0; i < len; i++) {
        atoms[i] = mm_token_atom(a, items[i], as_string);
    }
    return atom_expr(a, atoms, len);
}

static bool mm_decode_compressed(Arena *a, const char *joined, bool dag_format,
                                 Atom **steps_out, char *errbuf, size_t errbuf_sz) {
    Atom **steps = NULL;
    uint32_t len = 0, cap = 0;
    int64_t acc = 0;

    for (const unsigned char *p = (const unsigned char *)joined; *p; p++) {
        Atom *step = NULL;
        if (*p == '?') {
            step = dag_format ? atom_symbol(a, "incomplete") : atom_symbol(a, "?");
            acc = 0;
        } else if (*p == 'Z') {
            step = dag_format ? atom_symbol(a, "save") : atom_int(a, -1);
            acc = 0;
        } else if (*p >= 'A' && *p <= 'T') {
            int64_t n = 20 * acc + (int64_t)(*p - 'A');
            step = atom_int(a, n);
            acc = 0;
        } else if (*p >= 'U' && *p <= 'Y') {
            acc = 5 * acc + (int64_t)(*p - 'U' + 1);
            continue;
        } else {
            free(steps);
            mm_set_error(errbuf, errbuf_sz, "invalid-compressed-proof-char");
            return false;
        }

        if (len >= cap) {
            uint32_t next = cap ? cap * 2 : 8;
            steps = cetta_realloc(steps, sizeof(Atom *) * next);
            cap = next;
        }
        steps[len++] = step;
    }

    *steps_out = atom_expr(a, steps, len);
    free(steps);
    return true;
}

static Atom *mm_build_stmt(Arena *a, bool pverify_shape, const char *type_str,
                           const char *label, char **body, uint32_t nbody,
                           Atom *proof_expr, char *errbuf, size_t errbuf_sz) {
    Atom *type_atom;
    Atom *math_expr;

    if (!pverify_shape) {
        Atom *body_expr = mm_tokens_expr(a, body, nbody, false);
        if (proof_expr) {
            return atom_expr(a, (Atom *[]){
                atom_symbol(a, "mm-stmt"),
                atom_symbol(a, type_str),
                atom_symbol(a, label ? label : ""),
                body_expr,
                proof_expr
            }, 5);
        }
        return atom_expr(a, (Atom *[]){
            atom_symbol(a, "mm-stmt"),
            atom_symbol(a, type_str),
            atom_symbol(a, label ? label : ""),
            body_expr
        }, 4);
    }

    if (type_str[0] == 'c' || type_str[0] == 'v' || type_str[0] == 'd') {
        Atom *body_expr = mm_tokens_expr(a, body, nbody, true);
        return atom_expr(a, (Atom *[]){
            atom_symbol(a, type_str),
            body_expr
        }, 2);
    }

    if (type_str[0] == 'f') {
        if (nbody != 2) {
            mm_set_error(errbuf, errbuf_sz, "bad-f-statement");
            return NULL;
        }
        return atom_expr(a, (Atom *[]){
            atom_symbol(a, "f"),
            atom_string(a, label ? label : ""),
            mm_token_atom(a, body[0], true),
            mm_token_atom(a, body[1], true)
        }, 4);
    }

    if (type_str[0] == 'e' || type_str[0] == 'a') {
        if (nbody < 1) {
            mm_set_error(errbuf, errbuf_sz, "bad-labeled-statement");
            return NULL;
        }
        type_atom = mm_token_atom(a, body[0], true);
        math_expr = mm_tokens_expr(a, nbody > 1 ? body + 1 : body, nbody > 0 ? nbody - 1 : 0, true);
        return atom_expr(a, (Atom *[]){
            atom_symbol(a, type_str),
            atom_string(a, label ? label : ""),
            type_atom,
            math_expr
        }, 4);
    }

    if (type_str[0] == 'p') {
        if (nbody < 1 || !proof_expr) {
            mm_set_error(errbuf, errbuf_sz, "bad-p-statement");
            return NULL;
        }
        type_atom = mm_token_atom(a, body[0], true);
        math_expr = mm_tokens_expr(a, nbody > 1 ? body + 1 : body, nbody > 0 ? nbody - 1 : 0, true);
        return atom_expr(a, (Atom *[]){
            atom_symbol(a, "p"),
            atom_string(a, label ? label : ""),
            type_atom,
            math_expr,
            proof_expr
        }, 5);
    }

    mm_set_error(errbuf, errbuf_sz, "unknown-statement-kind");
    return NULL;
}

static Atom *mm_parse_statement(CettaMetamathParser *parser, Arena *a,
                                const char *first_tok, char *errbuf,
                                size_t errbuf_sz) {
    CettaMMToken tok = {0};
    bool eof = false;
    const char *label = "";
    const char *kw = NULL;
    char **body = NULL;
    uint32_t nbody = 0, cbody = 0;
    Atom *proof_expr = NULL;
    Atom *result = NULL;
    char type_str[2] = {'\0', '\0'};

    if (mm_is_stmt_keyword(first_tok)) {
        kw = first_tok;
    } else {
        label = first_tok;
        if (!mm_valid_label(label)) {
            mm_set_error(errbuf, errbuf_sz, "invalid-label");
            return NULL;
        }
        if (!mm_read_token(parser, &tok, &eof, errbuf, errbuf_sz)) {
            return NULL;
        }
        if (eof || !mm_is_stmt_keyword(tok.text)) {
            mm_token_free(&tok);
            mm_set_error(errbuf, errbuf_sz, "unexpected-token");
            return NULL;
        }
        kw = tok.text;
    }

    type_str[0] = kw[1];

    if (type_str[0] == 'p') {
        while (true) {
            if (!mm_read_token(parser, &tok, &eof, errbuf, errbuf_sz)) goto cleanup;
            if (eof) {
                mm_set_error(errbuf, errbuf_sz, "unterminated-statement");
                goto cleanup;
            }
            if (strcmp(tok.text, "$=") == 0) {
                mm_token_free(&tok);
                break;
            }
            if (tok.text[0] == '$') {
                mm_set_error(errbuf, errbuf_sz, "unexpected-keyword-in-statement");
                goto cleanup;
            }
            if (!mm_valid_math_symbol(tok.text)) {
                mm_set_error(errbuf, errbuf_sz, "invalid-math-symbol");
                goto cleanup;
            }
            mm_push_string(&body, &nbody, &cbody, tok.text);
            tok.text = NULL;
        }

        if (!mm_read_token(parser, &tok, &eof, errbuf, errbuf_sz)) goto cleanup;
        if (eof) {
            mm_set_error(errbuf, errbuf_sz, "unterminated-proof");
            goto cleanup;
        }

        if (strcmp(tok.text, "(") == 0) {
            char **labels = NULL;
            uint32_t nlabels = 0, clabels = 0;
            char **proof_tokens = NULL;
            uint32_t nproof = 0, cproof = 0;
            CettaMMStringBuf joined;
            Atom *labels_expr;
            Atom *steps_expr;

            mm_token_free(&tok);
            while (true) {
                if (!mm_read_token(parser, &tok, &eof, errbuf, errbuf_sz)) {
                    mm_free_string_vec(labels, nlabels);
                    mm_free_string_vec(proof_tokens, nproof);
                    goto cleanup;
                }
                if (eof) {
                    mm_free_string_vec(labels, nlabels);
                    mm_free_string_vec(proof_tokens, nproof);
                    mm_set_error(errbuf, errbuf_sz, "unterminated-compressed-proof");
                    goto cleanup;
                }
                if (strcmp(tok.text, ")") == 0) {
                    mm_token_free(&tok);
                    break;
                }
                mm_push_string(&labels, &nlabels, &clabels, tok.text);
                tok.text = NULL;
            }

            while (true) {
                if (!mm_read_token(parser, &tok, &eof, errbuf, errbuf_sz)) {
                    mm_free_string_vec(labels, nlabels);
                    mm_free_string_vec(proof_tokens, nproof);
                    goto cleanup;
                }
                if (eof) {
                    mm_free_string_vec(labels, nlabels);
                    mm_free_string_vec(proof_tokens, nproof);
                    mm_set_error(errbuf, errbuf_sz, "unterminated-compressed-proof");
                    goto cleanup;
                }
                if (strcmp(tok.text, "$.") == 0) {
                    mm_token_free(&tok);
                    break;
                }
                mm_push_string(&proof_tokens, &nproof, &cproof, tok.text);
                tok.text = NULL;
            }

            mm_sb_init(&joined);
            for (uint32_t i = 0; i < nproof; i++) {
                const char *piece = proof_tokens[i];
                for (const char *p = piece; *p; p++) {
                    if (!mm_sb_append_char(&joined, *p)) {
                        mm_free_string_vec(labels, nlabels);
                        mm_free_string_vec(proof_tokens, nproof);
                        mm_set_error(errbuf, errbuf_sz, "compressed-proof-too-long");
                        goto cleanup;
                    }
                }
            }

            labels_expr = mm_tokens_expr(a, labels, nlabels, parser->pverify_shape);
            if (!mm_decode_compressed(a, joined.text, parser->dag_format, &steps_expr,
                                      errbuf, errbuf_sz)) {
                mm_free_string_vec(labels, nlabels);
                mm_free_string_vec(proof_tokens, nproof);
                goto cleanup;
            }
            proof_expr = atom_expr(a, (Atom *[]){
                atom_symbol(a, parser->dag_format ? "compressed_dag" : "compressed"),
                labels_expr,
                steps_expr
            }, 3);
            mm_free_string_vec(labels, nlabels);
            mm_free_string_vec(proof_tokens, nproof);
        } else {
            char **proof = NULL;
            uint32_t nproof = 0, cproof = 0;
            mm_push_string(&proof, &nproof, &cproof, tok.text);
            tok.text = NULL;
            while (true) {
                if (!mm_read_token(parser, &tok, &eof, errbuf, errbuf_sz)) {
                    mm_free_string_vec(proof, nproof);
                    goto cleanup;
                }
                if (eof) {
                    mm_free_string_vec(proof, nproof);
                    mm_set_error(errbuf, errbuf_sz, "unterminated-proof");
                    goto cleanup;
                }
                if (strcmp(tok.text, "$.") == 0) {
                    mm_token_free(&tok);
                    break;
                }
                mm_push_string(&proof, &nproof, &cproof, tok.text);
                tok.text = NULL;
            }
            proof_expr = mm_tokens_expr(a, proof, nproof, parser->pverify_shape);
            mm_free_string_vec(proof, nproof);
        }
    } else {
        while (true) {
            if (!mm_read_token(parser, &tok, &eof, errbuf, errbuf_sz)) goto cleanup;
            if (eof) {
                mm_set_error(errbuf, errbuf_sz, "unterminated-statement");
                goto cleanup;
            }
            if (strcmp(tok.text, "$.") == 0) {
                mm_token_free(&tok);
                break;
            }
            if (tok.text[0] == '$') {
                mm_set_error(errbuf, errbuf_sz, "unexpected-keyword-in-statement");
                goto cleanup;
            }
            if (!mm_valid_math_symbol(tok.text)) {
                mm_set_error(errbuf, errbuf_sz, "invalid-math-symbol");
                goto cleanup;
            }
            mm_push_string(&body, &nbody, &cbody, tok.text);
            tok.text = NULL;
        }
    }

    result = mm_build_stmt(a, parser->pverify_shape, type_str, label, body, nbody,
                           proof_expr, errbuf, errbuf_sz);

cleanup:
    mm_token_free(&tok);
    mm_free_string_vec(body, nbody);
    return result;
}

CettaMetamathParser *cetta_metamath_parser_open(const char *path,
                                                bool pverify_shape,
                                                bool dag_format,
                                                char *errbuf,
                                                size_t errbuf_sz) {
    CettaMetamathParser *parser = cetta_malloc(sizeof(CettaMetamathParser));
    memset(parser, 0, sizeof(*parser));
    parser->pverify_shape = pverify_shape;
    parser->dag_format = dag_format;
    parser->scope_depth = 0;
    if (!mm_push_source(parser, path, errbuf, errbuf_sz)) {
        free(parser);
        return NULL;
    }
    return parser;
}

void cetta_metamath_parser_close(CettaMetamathParser *parser) {
    if (!parser) return;
    while (parser->source_len > 0) {
        mm_pop_source(parser);
    }
    free(parser);
}

CettaMetamathNextStatus cetta_metamath_parser_next_stmt(CettaMetamathParser *parser,
                                                        Arena *a,
                                                        Atom **stmt_out,
                                                        char *errbuf,
                                                        size_t errbuf_sz) {
    CettaMMToken tok = {0};
    bool eof = false;

    *stmt_out = NULL;
    while (true) {
        CettaMMSource *src;
        if (!mm_read_token(parser, &tok, &eof, errbuf, errbuf_sz)) {
            mm_token_free(&tok);
            return CETTA_MM_NEXT_ERROR;
        }
        if (eof) {
            return CETTA_MM_NEXT_EOF;
        }

        if (strcmp(tok.text, "$[") == 0) {
            CettaMMToken include_file = {0};
            CettaMMToken close_tok = {0};
            char include_path[PATH_MAX];

            if (parser->scope_depth != 0) {
                mm_token_free(&tok);
                mm_set_error(errbuf, errbuf_sz, "include-in-block");
                return CETTA_MM_NEXT_ERROR;
            }
            src = mm_current_source(parser);
            if (!mm_read_token(parser, &include_file, &eof, errbuf, errbuf_sz)) {
                mm_token_free(&tok);
                return CETTA_MM_NEXT_ERROR;
            }
            if (eof || include_file.text[0] == '$') {
                mm_token_free(&tok);
                mm_token_free(&include_file);
                mm_set_error(errbuf, errbuf_sz, "malformed-include");
                return CETTA_MM_NEXT_ERROR;
            }
            if (!mm_read_token(parser, &close_tok, &eof, errbuf, errbuf_sz)) {
                mm_token_free(&tok);
                mm_token_free(&include_file);
                return CETTA_MM_NEXT_ERROR;
            }
            if (eof || strcmp(close_tok.text, "$]") != 0) {
                mm_token_free(&tok);
                mm_token_free(&include_file);
                mm_token_free(&close_tok);
                mm_set_error(errbuf, errbuf_sz, "malformed-include");
                return CETTA_MM_NEXT_ERROR;
            }
            if (!cetta_text_path_resolve(src ? src->dir : ".", include_file.text,
                                         include_path, sizeof(include_path))) {
                mm_token_free(&tok);
                mm_token_free(&include_file);
                mm_token_free(&close_tok);
                mm_set_error(errbuf, errbuf_sz, "path-too-long");
                return CETTA_MM_NEXT_ERROR;
            }
            if (!mm_completed_contains(parser, include_path)) {
                if (!mm_push_source(parser, include_path, errbuf, errbuf_sz)) {
                    mm_token_free(&tok);
                    mm_token_free(&include_file);
                    mm_token_free(&close_tok);
                    return CETTA_MM_NEXT_ERROR;
                }
            }
            mm_token_free(&tok);
            mm_token_free(&include_file);
            mm_token_free(&close_tok);
            continue;
        }

        if (strcmp(tok.text, "${") == 0) {
            parser->scope_depth++;
            *stmt_out = parser->pverify_shape
                            ? atom_expr(a, (Atom *[]){ atom_symbol(a, "open_frame") }, 1)
                            : atom_expr(a, (Atom *[]){
                                  atom_symbol(a, "mm-stmt"),
                                  atom_symbol(a, "open"),
                                  atom_symbol(a, "()"),
                                  atom_symbol(a, "()")
                              }, 4);
            mm_token_free(&tok);
            return CETTA_MM_NEXT_STMT;
        }

        if (strcmp(tok.text, "$}") == 0) {
            if (parser->scope_depth <= 0) {
                mm_token_free(&tok);
                mm_set_error(errbuf, errbuf_sz, "bad-frame-close");
                return CETTA_MM_NEXT_ERROR;
            }
            parser->scope_depth--;
            *stmt_out = parser->pverify_shape
                            ? atom_expr(a, (Atom *[]){ atom_symbol(a, "close_frame") }, 1)
                            : atom_expr(a, (Atom *[]){
                                  atom_symbol(a, "mm-stmt"),
                                  atom_symbol(a, "close"),
                                  atom_symbol(a, "()"),
                                  atom_symbol(a, "()")
                              }, 4);
            mm_token_free(&tok);
            return CETTA_MM_NEXT_STMT;
        }

        *stmt_out = mm_parse_statement(parser, a, tok.text, errbuf, errbuf_sz);
        mm_token_free(&tok);
        if (!*stmt_out) return CETTA_MM_NEXT_ERROR;
        return CETTA_MM_NEXT_STMT;
    }
}

Atom *cetta_metamath_collect_file(Arena *a,
                                  const char *path,
                                  bool pverify_shape,
                                  bool dag_format,
                                  char *errbuf,
                                  size_t errbuf_sz) {
    CettaMetamathParser *parser = cetta_metamath_parser_open(path, pverify_shape,
                                                             dag_format, errbuf, errbuf_sz);
    Atom **stmts = NULL;
    uint32_t nstmts = 0;
    uint32_t cstmts = 0;
    Atom *result;

    if (!parser) return NULL;

    while (true) {
        Atom *stmt = NULL;
        CettaMetamathNextStatus status =
            cetta_metamath_parser_next_stmt(parser, a, &stmt, errbuf, errbuf_sz);
        if (status == CETTA_MM_NEXT_EOF) break;
        if (status == CETTA_MM_NEXT_ERROR) {
            free(stmts);
            cetta_metamath_parser_close(parser);
            return NULL;
        }
        if (nstmts >= cstmts) {
            uint32_t next = cstmts ? cstmts * 2 : 16;
            stmts = cetta_realloc(stmts, sizeof(Atom *) * next);
            cstmts = next;
        }
        stmts[nstmts++] = stmt;
    }

    result = atom_expr(a, stmts, nstmts);
    free(stmts);
    cetta_metamath_parser_close(parser);
    return result;
}

Atom *cetta_metamath_read_tokens_file(Arena *a,
                                      const char *path,
                                      char *errbuf,
                                      size_t errbuf_sz) {
    CettaMetamathParser *parser = cetta_metamath_parser_open(path, false, false,
                                                             errbuf, errbuf_sz);
    CettaMMToken tok = {0};
    bool eof = false;
    Atom **tokens = NULL;
    uint32_t ntokens = 0;
    uint32_t ctokens = 0;
    Atom *result = NULL;

    if (!parser) return NULL;

    while (true) {
        if (!mm_read_token(parser, &tok, &eof, errbuf, errbuf_sz)) goto cleanup;
        if (eof) break;
        if (ntokens >= ctokens) {
            uint32_t next = ctokens ? ctokens * 2 : 32;
            tokens = cetta_realloc(tokens, sizeof(Atom *) * next);
            ctokens = next;
        }
        if (strcmp(tok.text, "$c") == 0) tokens[ntokens++] = atom_symbol(a, "kw.c");
        else if (strcmp(tok.text, "$v") == 0) tokens[ntokens++] = atom_symbol(a, "kw.v");
        else if (strcmp(tok.text, "$f") == 0) tokens[ntokens++] = atom_symbol(a, "kw.f");
        else if (strcmp(tok.text, "$e") == 0) tokens[ntokens++] = atom_symbol(a, "kw.e");
        else if (strcmp(tok.text, "$a") == 0) tokens[ntokens++] = atom_symbol(a, "kw.a");
        else if (strcmp(tok.text, "$p") == 0) tokens[ntokens++] = atom_symbol(a, "kw.p");
        else if (strcmp(tok.text, "$d") == 0) tokens[ntokens++] = atom_symbol(a, "kw.d");
        else if (strcmp(tok.text, "${") == 0) tokens[ntokens++] = atom_symbol(a, "kw.open");
        else if (strcmp(tok.text, "$}") == 0) tokens[ntokens++] = atom_symbol(a, "kw.close");
        else if (strcmp(tok.text, "$=") == 0) tokens[ntokens++] = atom_symbol(a, "kw.eq");
        else if (strcmp(tok.text, "$.") == 0) tokens[ntokens++] = atom_symbol(a, "kw.end");
        else tokens[ntokens++] = atom_symbol(a, tok.text);
        mm_token_free(&tok);
    }

    result = atom_symbol(a, "Nil");
    for (int ti = (int)ntokens - 1; ti >= 0; ti--) {
        result = atom_expr3(a, atom_symbol(a, "Cons"), tokens[ti], result);
    }

cleanup:
    mm_token_free(&tok);
    free(tokens);
    cetta_metamath_parser_close(parser);
    return result;
}

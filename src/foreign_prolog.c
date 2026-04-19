#define _GNU_SOURCE

#include "foreign_internal.h"

#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} PrologStringBuf;

typedef enum {
    CETTA_PROLOG_REPLY_INVALID = 0,
    CETTA_PROLOG_REPLY_OK,
    CETTA_PROLOG_REPLY_ERROR,
    CETTA_PROLOG_REPLY_RESULTS,
    CETTA_PROLOG_REPLY_BOOL
} CettaPrologReplyKind;

typedef struct {
    CettaPrologReplyKind kind;
    char *error_text;
    char **result_texts;
    uint32_t result_len;
    bool bool_value;
} CettaPrologReply;

static void psb_init(PrologStringBuf *sb) {
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void psb_free(PrologStringBuf *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static bool psb_reserve(PrologStringBuf *sb, size_t extra) {
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap)
        return true;
    size_t next = sb->cap ? sb->cap * 2u : 128u;
    while (next < need)
        next *= 2u;
    char *buf = cetta_realloc(sb->buf, next);
    if (!buf)
        return false;
    sb->buf = buf;
    sb->cap = next;
    return true;
}

static bool psb_append_n(PrologStringBuf *sb, const char *text, size_t len) {
    if (!psb_reserve(sb, len))
        return false;
    memcpy(sb->buf + sb->len, text, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
    return true;
}

static bool psb_append(PrologStringBuf *sb, const char *text) {
    return psb_append_n(sb, text, strlen(text));
}

static bool psb_appendf(PrologStringBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(ap);
        return false;
    }
    if (!psb_reserve(sb, (size_t)needed)) {
        va_end(ap);
        return false;
    }
    vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap);
    va_end(ap);
    sb->len += (size_t)needed;
    return true;
}

static bool prolog_escape_double_quoted(PrologStringBuf *sb, const char *text) {
    if (!psb_append(sb, "\""))
        return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        switch (*p) {
        case '\\':
            if (!psb_append(sb, "\\\\"))
                return false;
            break;
        case '"':
            if (!psb_append(sb, "\\\""))
                return false;
            break;
        case '\n':
            if (!psb_append(sb, "\\n"))
                return false;
            break;
        case '\r':
            if (!psb_append(sb, "\\r"))
                return false;
            break;
        case '\t':
            if (!psb_append(sb, "\\t"))
                return false;
            break;
        default:
            if (!psb_append_n(sb, (const char *)p, 1))
                return false;
            break;
        }
    }
    return psb_append(sb, "\"");
}

static const char *string_like_atom(Atom *atom) {
    if (!atom)
        return NULL;
    if (atom->kind == ATOM_SYMBOL)
        return atom_name_cstr(atom);
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING)
        return atom->ground.sval;
    return NULL;
}

static bool resolve_exec_relative_path(CettaForeignRuntime *rt,
                                       const char *raw,
                                       char *out,
                                       size_t out_sz) {
    if (!raw || !*raw || !out || out_sz == 0)
        return false;
    if (raw[0] == '/') {
        snprintf(out, out_sz, "%s", raw);
        return access(out, R_OK) == 0;
    }
    if (rt && rt->prolog.exec_root[0]) {
        int n = snprintf(out, out_sz, "%s/%s", rt->prolog.exec_root, raw);
        if (n > 0 && (size_t)n < out_sz && access(out, R_OK) == 0)
            return true;
    }
    snprintf(out, out_sz, "%s", raw);
    return access(out, R_OK) == 0;
}

static CettaForeignRegisteredSymbol *registered_symbol_find(CettaForeignRuntime *rt,
                                                            SymbolId sym_id) {
    if (!rt || sym_id == SYMBOL_ID_NONE)
        return NULL;
    for (CettaForeignRegisteredSymbol *cur = rt->registered_symbols; cur;
         cur = cur->next) {
        if (cur->sym_id == sym_id)
            return cur;
    }
    return NULL;
}

static bool registered_symbol_add(CettaForeignRuntime *rt,
                                  SymbolId sym_id,
                                  const char *name,
                                  bool unwrap) {
    if (!rt || sym_id == SYMBOL_ID_NONE || !name || !*name)
        return false;
    CettaForeignRegisteredSymbol *existing = registered_symbol_find(rt, sym_id);
    if (existing) {
        existing->backend = CETTA_FOREIGN_BACKEND_PROLOG;
        existing->unwrap = unwrap;
        char *dup = cetta_malloc(strlen(name) + 1u);
        if (!dup)
            return false;
        strcpy(dup, name);
        free(existing->name);
        existing->name = dup;
        return true;
    }
    CettaForeignRegisteredSymbol *entry =
        cetta_malloc(sizeof(CettaForeignRegisteredSymbol));
    if (!entry)
        return false;
    char *dup = cetta_malloc(strlen(name) + 1u);
    if (!dup) {
        free(entry);
        return false;
    }
    strcpy(dup, name);
    entry->sym_id = sym_id;
    entry->backend = CETTA_FOREIGN_BACKEND_PROLOG;
    entry->unwrap = unwrap;
    entry->name = dup;
    entry->next = rt->registered_symbols;
    rt->registered_symbols = entry;
    return true;
}

static CettaForeignValue *foreign_new_prolog_value(CettaForeignRuntime *rt,
                                                   const char *name,
                                                   bool callable,
                                                   bool unwrap) {
    if (!rt || !name || !*name)
        return NULL;
    CettaForeignValue *value = cetta_malloc(sizeof(CettaForeignValue));
    if (!value)
        return NULL;
    char *dup = cetta_malloc(strlen(name) + 1u);
    if (!dup) {
        free(value);
        return NULL;
    }
    strcpy(dup, name);
    value->backend = CETTA_FOREIGN_BACKEND_PROLOG;
    value->callable = callable;
    value->unwrap = unwrap;
    value->handle.prolog_name = dup;
    value->next = rt->values;
    rt->values = value;
    return value;
}

static Atom *prolog_error_atom(Arena *a, const char *message) {
    return atom_symbol(a, message ? message : "prolog foreign error");
}

static bool parse_reply_quoted_string(const char *text, size_t *pos, char **out) {
    if (!text || !pos || !out || text[*pos] != '"')
        return false;
    (*pos)++;
    PrologStringBuf sb;
    psb_init(&sb);
    while (text[*pos]) {
        char ch = text[*pos];
        if (ch == '"') {
            (*pos)++;
            if (!sb.buf)
                sb.buf = strdup("");
            *out = sb.buf;
            return true;
        }
        if (ch == '\\') {
            (*pos)++;
            ch = text[*pos];
            if (!ch) {
                psb_free(&sb);
                return false;
            }
            switch (ch) {
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case '\\':
            case '"':
                break;
            default:
                break;
            }
        }
        if (!psb_append_n(&sb, &ch, 1)) {
            psb_free(&sb);
            return false;
        }
        (*pos)++;
    }
    psb_free(&sb);
    return false;
}

static bool parse_reply_string_alloc(const char *text, size_t *pos, char **out) {
    char *tmp = NULL;
    if (!parse_reply_quoted_string(text, pos, &tmp))
        return false;
    if (!tmp)
        tmp = strdup("");
    *out = tmp;
    return *out != NULL;
}

static void prolog_reply_free(CettaPrologReply *reply) {
    if (!reply)
        return;
    free(reply->error_text);
    reply->error_text = NULL;
    if (reply->result_texts) {
        for (uint32_t i = 0; i < reply->result_len; i++)
            free(reply->result_texts[i]);
        free(reply->result_texts);
    }
    reply->result_texts = NULL;
    reply->result_len = 0;
    reply->kind = CETTA_PROLOG_REPLY_INVALID;
}

static void skip_reply_ws(const char *text, size_t *pos) {
    while (text[*pos] && isspace((unsigned char)text[*pos]))
        (*pos)++;
}

static bool parse_prolog_reply(const char *line, CettaPrologReply *reply) {
    memset(reply, 0, sizeof(*reply));
    if (!line)
        return false;
    size_t pos = 0;
    skip_reply_ws(line, &pos);
    if (strncmp(line + pos, "ok", 2) == 0) {
        reply->kind = CETTA_PROLOG_REPLY_OK;
        return true;
    }
    if (strncmp(line + pos, "bool(", 5) == 0) {
        pos += 5;
        if (strncmp(line + pos, "true", 4) == 0) {
            reply->kind = CETTA_PROLOG_REPLY_BOOL;
            reply->bool_value = true;
            return true;
        }
        if (strncmp(line + pos, "false", 5) == 0) {
            reply->kind = CETTA_PROLOG_REPLY_BOOL;
            reply->bool_value = false;
            return true;
        }
        return false;
    }
    if (strncmp(line + pos, "error(", 6) == 0) {
        pos += 6;
        char *msg = NULL;
        if (!parse_reply_string_alloc(line, &pos, &msg))
            return false;
        reply->kind = CETTA_PROLOG_REPLY_ERROR;
        reply->error_text = msg;
        return true;
    }
    if (strncmp(line + pos, "results([", 9) == 0) {
        pos += 9;
        reply->kind = CETTA_PROLOG_REPLY_RESULTS;
        while (line[pos] && line[pos] != ']') {
            skip_reply_ws(line, &pos);
            char *text = NULL;
            if (!parse_reply_string_alloc(line, &pos, &text)) {
                prolog_reply_free(reply);
                return false;
            }
            char **next = cetta_realloc(reply->result_texts,
                                        sizeof(char *) * (reply->result_len + 1u));
            if (!next) {
                free(text);
                prolog_reply_free(reply);
                return false;
            }
            reply->result_texts = next;
            reply->result_texts[reply->result_len++] = text;
            skip_reply_ws(line, &pos);
            if (line[pos] == ',')
                pos++;
        }
        return true;
    }
    return false;
}

static Atom *parse_result_text_atom(Arena *a, const char *text) {
    size_t pos = 0;
    Atom *atom = parse_sexpr(a, text, &pos);
    if (!atom)
        return NULL;
    while (text[pos] && isspace((unsigned char)text[pos]))
        pos++;
    if (text[pos] != '\0')
        return NULL;
    return atom;
}

static bool encode_atom_to_prolog(PrologStringBuf *sb, Atom *atom);

static bool encode_expr_to_prolog(PrologStringBuf *sb, Atom *atom) {
    if (!psb_append(sb, "expr(["))
        return false;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (i > 0 && !psb_append(sb, ","))
            return false;
        if (!encode_atom_to_prolog(sb, atom->expr.elems[i]))
            return false;
    }
    return psb_append(sb, "])");
}

static bool encode_atom_to_prolog(PrologStringBuf *sb, Atom *atom) {
    if (!atom)
        return psb_append(sb, "sym(\"Empty\")");
    switch (atom->kind) {
    case ATOM_SYMBOL: {
        const char *name = atom_name_cstr(atom);
        if (!psb_append(sb, "sym("))
            return false;
        if (!prolog_escape_double_quoted(sb, name ? name : ""))
            return false;
        return psb_append(sb, ")");
    }
    case ATOM_VAR: {
        const char *name = atom_name_cstr(atom);
        if (!psb_append(sb, "var("))
            return false;
        if (!prolog_escape_double_quoted(sb, name ? name : "_"))
            return false;
        return psb_append(sb, ")");
    }
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
            return psb_appendf(sb, "int(%lld)", (long long)atom->ground.ival);
        case GV_FLOAT:
            return psb_appendf(sb, "float(%.17g)", atom->ground.fval);
        case GV_BOOL:
            return psb_append(sb, atom->ground.bval ? "bool(true)" : "bool(false)");
        case GV_STRING:
            if (!psb_append(sb, "str("))
                return false;
            if (!prolog_escape_double_quoted(sb, atom->ground.sval ? atom->ground.sval : ""))
                return false;
            return psb_append(sb, ")");
        default:
            return false;
        }
    case ATOM_EXPR:
        return encode_expr_to_prolog(sb, atom);
    }
    return false;
}

static bool read_reply_line(CettaForeignPrologSession *session, char **line_out) {
    *line_out = NULL;
    if (!session || !session->from_child)
        return false;
    size_t cap = 0;
    ssize_t len = getline(line_out, &cap, session->from_child);
    if (len < 0) {
        free(*line_out);
        *line_out = NULL;
        return false;
    }
    while (len > 0 &&
           ((*line_out)[len - 1] == '\n' || (*line_out)[len - 1] == '\r')) {
        (*line_out)[--len] = '\0';
    }
    return true;
}

static bool send_command_and_read_reply(CettaForeignRuntime *rt,
                                        const char *command,
                                        CettaPrologReply *reply,
                                        Atom **error_out,
                                        Arena *a) {
    if (!rt || !rt->prolog.to_child || !rt->prolog.from_child) {
        if (error_out)
            *error_out = prolog_error_atom(a, "prolog bridge is not running");
        return false;
    }
    fputs(command, rt->prolog.to_child);
    fflush(rt->prolog.to_child);
    char *line = NULL;
    if (!read_reply_line(&rt->prolog, &line)) {
        if (error_out)
            *error_out = prolog_error_atom(a, "failed to read reply from prolog bridge");
        return false;
    }
    bool ok = parse_prolog_reply(line, reply);
    if (!ok && error_out) {
        *error_out = prolog_error_atom(a, "malformed reply from prolog bridge");
    }
    free(line);
    return ok;
}

static bool ensure_bridge_path(CettaForeignRuntime *rt) {
    if (!rt)
        return false;
    if (rt->prolog.bridge_path[0] && access(rt->prolog.bridge_path, R_OK) == 0)
        return true;
    if (rt->prolog.exec_root[0]) {
        int n = snprintf(rt->prolog.bridge_path, sizeof(rt->prolog.bridge_path),
                         "%s/runtime/foreign_prolog_bridge.pl",
                         rt->prolog.exec_root);
        if (n > 0 &&
            (size_t)n < sizeof(rt->prolog.bridge_path) &&
            access(rt->prolog.bridge_path, R_OK) == 0) {
            return true;
        }
    }
    snprintf(rt->prolog.bridge_path, sizeof(rt->prolog.bridge_path),
             "runtime/foreign_prolog_bridge.pl");
    return access(rt->prolog.bridge_path, R_OK) == 0;
}

static bool ensure_prolog_session(CettaForeignRuntime *rt,
                                  Arena *a,
                                  Atom **error_out) {
    if (!rt)
        return false;
    if (rt->prolog.started && rt->prolog.to_child && rt->prolog.from_child)
        return true;
    if (!ensure_bridge_path(rt)) {
        if (error_out)
            *error_out = prolog_error_atom(a, "missing runtime/foreign_prolog_bridge.pl");
        return false;
    }

    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        if (error_out)
            *error_out = prolog_error_atom(a, "failed to create prolog bridge pipes");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        if (error_out)
            *error_out = prolog_error_atom(a, "failed to fork prolog bridge");
        return false;
    }

    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        execlp("swipl", "swipl", "-q", "-f", "none",
               "-s", rt->prolog.bridge_path,
               "-g", "cetta_foreign_bridge_main",
               "-t", "halt",
               (char *)NULL);
        _exit(127);
    }

    close(to_child[0]);
    close(from_child[1]);
    FILE *to_fp = fdopen(to_child[1], "w");
    FILE *from_fp = fdopen(from_child[0], "r");
    if (!to_fp || !from_fp) {
        if (to_fp)
            fclose(to_fp);
        if (from_fp)
            fclose(from_fp);
        close(to_child[1]);
        close(from_child[0]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        if (error_out)
            *error_out = prolog_error_atom(a, "failed to open prolog bridge streams");
        return false;
    }
    rt->prolog.pid = pid;
    rt->prolog.to_child = to_fp;
    rt->prolog.from_child = from_fp;
    rt->prolog.started = true;
    return true;
}

static bool prolog_consult_like(CettaForeignRuntime *rt,
                                Arena *a,
                                Atom *path_atom,
                                bool use_module,
                                Atom **error_out) {
    const char *raw = string_like_atom(path_atom);
    if (!raw) {
        if (error_out)
            *error_out = prolog_error_atom(a, "prolog path must be a symbol or string");
        return false;
    }
    char resolved[PATH_MAX];
    const char *path = raw;
    if (!use_module && resolve_exec_relative_path(rt, raw, resolved, sizeof(resolved)))
        path = resolved;
    if (!ensure_prolog_session(rt, a, error_out))
        return false;

    PrologStringBuf cmd;
    psb_init(&cmd);
    bool ok = psb_append(&cmd, use_module ? "use_module_spec(" : "consult_file(") &&
              prolog_escape_double_quoted(&cmd, path) &&
              psb_append(&cmd, ").\n");
    if (!ok) {
        psb_free(&cmd);
        if (error_out)
            *error_out = prolog_error_atom(a, "failed to encode prolog bridge command");
        return false;
    }

    CettaPrologReply reply;
    ok = send_command_and_read_reply(rt, cmd.buf, &reply, error_out, a);
    psb_free(&cmd);
    if (!ok)
        return false;
    if (reply.kind == CETTA_PROLOG_REPLY_ERROR) {
        if (error_out)
            *error_out = prolog_error_atom(a, reply.error_text);
        prolog_reply_free(&reply);
        return false;
    }
    prolog_reply_free(&reply);
    return true;
}

static bool prolog_call_registered(CettaForeignRuntime *rt,
                                   Arena *a,
                                   const char *name,
                                   Atom **args,
                                   uint32_t nargs,
                                   ResultSet *rs,
                                   Atom **error_out) {
    if (!ensure_prolog_session(rt, a, error_out))
        return false;
    PrologStringBuf cmd;
    psb_init(&cmd);
    bool ok = psb_append(&cmd, "call_registered(") &&
              prolog_escape_double_quoted(&cmd, name) &&
              psb_append(&cmd, ",[");
    for (uint32_t i = 0; ok && i < nargs; i++) {
        if (i > 0)
            ok = psb_append(&cmd, ",");
        if (ok)
            ok = encode_atom_to_prolog(&cmd, args[i]);
    }
    ok = ok && psb_append(&cmd, "]).\n");
    if (!ok) {
        psb_free(&cmd);
        if (error_out)
            *error_out = prolog_error_atom(a, "failed to encode prolog call");
        return false;
    }
    CettaPrologReply reply;
    ok = send_command_and_read_reply(rt, cmd.buf, &reply, error_out, a);
    psb_free(&cmd);
    if (!ok)
        return false;
    if (reply.kind == CETTA_PROLOG_REPLY_ERROR) {
        if (error_out)
            *error_out = prolog_error_atom(a, reply.error_text);
        prolog_reply_free(&reply);
        return false;
    }
    if (reply.kind != CETTA_PROLOG_REPLY_RESULTS) {
        prolog_reply_free(&reply);
        if (error_out)
            *error_out = prolog_error_atom(a, "unexpected reply from prolog callable");
        return false;
    }
    for (uint32_t i = 0; i < reply.result_len; i++) {
        Atom *result = parse_result_text_atom(a, reply.result_texts[i]);
        if (!result) {
            prolog_reply_free(&reply);
            if (error_out)
                *error_out = prolog_error_atom(a, "failed to parse prolog result as MeTTa atom");
            return false;
        }
        result_set_add(rs, result);
    }
    prolog_reply_free(&reply);
    return true;
}

static bool prolog_call_goal_like(CettaForeignRuntime *rt,
                                  Arena *a,
                                  Atom *goal_atom,
                                  const char *command_name,
                                  bool *bool_out,
                                  Atom **error_out) {
    if (!goal_atom || goal_atom->kind != ATOM_EXPR) {
        if (error_out)
            *error_out = prolog_error_atom(a, "prolog goal must be an expression");
        return false;
    }
    if (!ensure_prolog_session(rt, a, error_out))
        return false;
    PrologStringBuf cmd;
    psb_init(&cmd);
    bool ok = psb_append(&cmd, command_name) &&
              psb_append(&cmd, "(") &&
              encode_atom_to_prolog(&cmd, goal_atom) &&
              psb_append(&cmd, ").\n");
    if (!ok) {
        psb_free(&cmd);
        if (error_out)
            *error_out = prolog_error_atom(a, "failed to encode prolog goal command");
        return false;
    }
    CettaPrologReply reply;
    ok = send_command_and_read_reply(rt, cmd.buf, &reply, error_out, a);
    psb_free(&cmd);
    if (!ok)
        return false;
    if (reply.kind == CETTA_PROLOG_REPLY_ERROR) {
        if (error_out)
            *error_out = prolog_error_atom(a, reply.error_text);
        prolog_reply_free(&reply);
        return false;
    }
    if (reply.kind != CETTA_PROLOG_REPLY_BOOL) {
        prolog_reply_free(&reply);
        if (error_out)
            *error_out = prolog_error_atom(a, "unexpected reply from prolog goal command");
        return false;
    }
    *bool_out = reply.bool_value;
    prolog_reply_free(&reply);
    return true;
}

void cetta_foreign_prolog_runtime_init(CettaForeignRuntime *rt) {
    if (!rt)
        return;
    rt->registered_symbols = NULL;
    rt->prolog.pid = 0;
    rt->prolog.to_child = NULL;
    rt->prolog.from_child = NULL;
    rt->prolog.started = false;
    rt->prolog.exec_root[0] = '\0';
    rt->prolog.bridge_path[0] = '\0';
}

void cetta_foreign_prolog_runtime_cleanup(CettaForeignRuntime *rt) {
    if (!rt)
        return;
    CettaForeignRegisteredSymbol *cur = rt->registered_symbols;
    while (cur) {
        CettaForeignRegisteredSymbol *next = cur->next;
        free(cur->name);
        free(cur);
        cur = next;
    }
    rt->registered_symbols = NULL;
    if (rt->prolog.started) {
        if (rt->prolog.to_child) {
            fputs("shutdown.\n", rt->prolog.to_child);
            fflush(rt->prolog.to_child);
            fclose(rt->prolog.to_child);
        }
        if (rt->prolog.from_child)
            fclose(rt->prolog.from_child);
        waitpid(rt->prolog.pid, NULL, 0);
    }
    rt->prolog.pid = 0;
    rt->prolog.to_child = NULL;
    rt->prolog.from_child = NULL;
    rt->prolog.started = false;
}

void cetta_foreign_prolog_set_exec_root(CettaForeignRuntime *rt,
                                        const char *root_dir) {
    if (!rt)
        return;
    if (!root_dir || !*root_dir) {
        rt->prolog.exec_root[0] = '\0';
        return;
    }
    snprintf(rt->prolog.exec_root, sizeof(rt->prolog.exec_root), "%s",
             root_dir);
    rt->prolog.bridge_path[0] = '\0';
}

bool cetta_foreign_prolog_is_callable_head(CettaForeignRuntime *rt, Atom *head) {
    if (!rt || !head)
        return false;
    if (head->kind == ATOM_GROUNDED && head->ground.gkind == GV_FOREIGN) {
        CettaForeignValue *value = (CettaForeignValue *)head->ground.ptr;
        return value && value->callable &&
               value->backend == CETTA_FOREIGN_BACKEND_PROLOG;
    }
    if (head->kind == ATOM_SYMBOL)
        return registered_symbol_find(rt, head->sym_id) != NULL;
    return false;
}

bool cetta_foreign_prolog_call(CettaForeignRuntime *rt,
                               Space *space,
                               Arena *a,
                               Atom *callable,
                               Atom **args,
                               uint32_t nargs,
                               ResultSet *rs,
                               Atom **error_out,
                               bool *handled_out) {
    (void)space;
    if (handled_out)
        *handled_out = false;
    if (!rt || !callable)
        return false;
    const char *name = NULL;
    if (callable->kind == ATOM_GROUNDED && callable->ground.gkind == GV_FOREIGN) {
        CettaForeignValue *value = (CettaForeignValue *)callable->ground.ptr;
        if (!value || !value->callable ||
            value->backend != CETTA_FOREIGN_BACKEND_PROLOG) {
            return false;
        }
        name = value->handle.prolog_name;
    } else if (callable->kind == ATOM_SYMBOL) {
        CettaForeignRegisteredSymbol *entry =
            registered_symbol_find(rt, callable->sym_id);
        if (!entry || entry->backend != CETTA_FOREIGN_BACKEND_PROLOG)
            return false;
        name = entry->name;
    } else {
        return false;
    }
    if (handled_out)
        *handled_out = true;
    return prolog_call_registered(rt, a, name, args, nargs, rs, error_out);
}

static Atom *prolog_native_bool_atom(Arena *a, bool value) {
    return value ? atom_true(a) : atom_false(a);
}

static Atom *native_incorrect_arity(Arena *a, Atom *head, Atom **args,
                                    uint32_t nargs);

static Atom *prolog_build_predicate_term(Arena *a, Atom **args, uint32_t nargs) {
    if (!a || !args || nargs == 0)
        return NULL;
    if (nargs == 1)
        return args[0];
    Atom **elems = arena_alloc(a, sizeof(Atom *) * nargs);
    for (uint32_t i = 0; i < nargs; i++)
        elems[i] = args[i];
    return atom_expr(a, elems, nargs);
}

static Atom *prolog_normalize_goal_atom(Arena *a, Atom *goal_atom) {
    if (!goal_atom || goal_atom->kind != ATOM_EXPR || goal_atom->expr.len == 0)
        return goal_atom;
    Atom *head = goal_atom->expr.elems[0];
    if (head && atom_is_symbol_id(head, g_builtin_syms.predicate_ctor)) {
        if (goal_atom->expr.len == 2)
            return prolog_normalize_goal_atom(a, goal_atom->expr.elems[1]);
        return atom_expr(a, goal_atom->expr.elems + 1, goal_atom->expr.len - 1);
    }
    if (head && atom_is_symbol_id(head, g_builtin_syms.quote) &&
        goal_atom->expr.len == 2) {
        return prolog_normalize_goal_atom(a, goal_atom->expr.elems[1]);
    }
    return goal_atom;
}

static Atom *prolog_goal_from_native_args(Arena *a, Atom *head,
                                          Atom **args, uint32_t nargs) {
    if (nargs == 0)
        return NULL;
    Atom *goal = NULL;
    if (nargs == 1) {
        goal = args[0];
    } else {
        goal = prolog_build_predicate_term(a, args, nargs);
    }
    if (!goal)
        return native_incorrect_arity(a, head, args, nargs);
    return prolog_normalize_goal_atom(a, goal);
}

static Atom *native_incorrect_arity(Arena *a, Atom *head, Atom **args,
                                    uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1u));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++)
        elems[i + 1u] = args[i];
    return atom_error(a, atom_expr(a, elems, nargs + 1u),
                      atom_symbol(a, "IncorrectNumberOfArguments"));
}

Atom *cetta_foreign_prolog_dispatch_native(CettaForeignRuntime *rt,
                                           Space *space,
                                           Arena *a,
                                           Atom *head,
                                           Atom **args,
                                           uint32_t nargs) {
    (void)space;
    if (!rt || !head || head->kind != ATOM_SYMBOL)
        return NULL;
    const char *head_name = atom_name_cstr(head);
    if (!head_name)
        return NULL;

    if (strcmp(head_name, "pl-consult") == 0) {
        if (nargs != 1)
            return native_incorrect_arity(a, head, args, nargs);
        Atom *error = NULL;
        if (!prolog_consult_like(rt, a, args[0], false, &error))
            return error ? error : prolog_error_atom(a, "pl-consult failed");
        return atom_unit(a);
    }
    if (strcmp(head_name, "pl-use-module") == 0) {
        if (nargs != 1)
            return native_incorrect_arity(a, head, args, nargs);
        Atom *error = NULL;
        if (!prolog_consult_like(rt, a, args[0], true, &error))
            return error ? error : prolog_error_atom(a, "pl-use-module failed");
        return atom_unit(a);
    }
    if (strcmp(head_name, "pl-atom") == 0) {
        if (nargs != 1)
            return native_incorrect_arity(a, head, args, nargs);
        const char *name = string_like_atom(args[0]);
        if (!name)
            return prolog_error_atom(a, "pl-atom expects a symbol or string predicate name");
        CettaForeignValue *value =
            foreign_new_prolog_value(rt, name, true, true);
        if (!value)
            return prolog_error_atom(a, "failed to allocate prolog callable");
        return atom_foreign(a, value);
    }
    if (strcmp(head_name, "pl-import") == 0 ||
        strcmp(head_name, "import_prolog_function") == 0) {
        if (nargs != 1)
            return native_incorrect_arity(a, head, args, nargs);
        const char *name = string_like_atom(args[0]);
        if (!name)
            return prolog_error_atom(a, "import_prolog_function expects a symbol or string predicate name");
        SymbolId sym_id = symbol_intern_cstr(g_symbols, name);
        if (!registered_symbol_add(rt, sym_id, name, true))
            return prolog_error_atom(a, "failed to register prolog predicate");
        return atom_unit(a);
    }
    if (strcmp(head_name, "Predicate") == 0) {
        if (nargs == 0)
            return native_incorrect_arity(a, head, args, nargs);
        return prolog_build_predicate_term(a, args, nargs);
    }
    if (strcmp(head_name, "pl-call") == 0 ||
        strcmp(head_name, "callPredicate") == 0 ||
        strcmp(head_name, "assertzPredicate") == 0 ||
        strcmp(head_name, "assertaPredicate") == 0 ||
        strcmp(head_name, "retractPredicate") == 0) {
        const char *cmd = "call_goal";
        if (strcmp(head_name, "assertzPredicate") == 0)
            cmd = "assertz_goal";
        else if (strcmp(head_name, "assertaPredicate") == 0)
            cmd = "asserta_goal";
        else if (strcmp(head_name, "retractPredicate") == 0)
            cmd = "retract_goal";
        Atom *goal_atom = prolog_goal_from_native_args(a, head, args, nargs);
        if (!goal_atom)
            return native_incorrect_arity(a, head, args, nargs);
        bool ok = false;
        Atom *error = NULL;
        if (!prolog_call_goal_like(rt, a, goal_atom, cmd, &ok, &error))
            return error ? error : prolog_error_atom(a, "prolog goal invocation failed");
        return prolog_native_bool_atom(a, ok);
    }

    return NULL;
}

#include "mm2_lower.h"

#include "symbol.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    MM2_CTX_GENERAL = 0,
    MM2_CTX_PATTERN,
    MM2_CTX_TEMPLATE
} Mm2LowerContext;

typedef struct {
    SymbolId surf_exec;
    SymbolId surf_btm;
    SymbolId surf_act;
    SymbolId surf_sink_seq;
    SymbolId surf_neq;
    SymbolId surf_z3;

    SymbolId ir_exec;
    SymbolId ir_pattern_and;
    SymbolId ir_pattern_btm;
    SymbolId ir_pattern_act;
    SymbolId ir_guard_eq;
    SymbolId ir_guard_neq;
    SymbolId ir_sink_seq;
    SymbolId ir_sink_add;
    SymbolId ir_sink_remove;
    SymbolId ir_sink_act;
    SymbolId ir_sink_z3;
} Mm2LowerSyms;

static Mm2LowerSyms mm2_lower_syms(void) {
    Mm2LowerSyms syms;
    syms.surf_exec = symbol_intern_cstr(g_symbols, "exec");
    syms.surf_btm = symbol_intern_cstr(g_symbols, "BTM");
    syms.surf_act = symbol_intern_cstr(g_symbols, "ACT");
    syms.surf_sink_seq = symbol_intern_cstr(g_symbols, "O");
    syms.surf_neq = symbol_intern_cstr(g_symbols, "!=");
    syms.surf_z3 = symbol_intern_cstr(g_symbols, "z3");

    syms.ir_exec = symbol_intern_cstr(g_symbols, "mm2_exec");
    syms.ir_pattern_and = symbol_intern_cstr(g_symbols, "mm2_pattern_and");
    syms.ir_pattern_btm = symbol_intern_cstr(g_symbols, "mm2_pattern_btm");
    syms.ir_pattern_act = symbol_intern_cstr(g_symbols, "mm2_pattern_act");
    syms.ir_guard_eq = symbol_intern_cstr(g_symbols, "mm2_guard_eq");
    syms.ir_guard_neq = symbol_intern_cstr(g_symbols, "mm2_guard_neq");
    syms.ir_sink_seq = symbol_intern_cstr(g_symbols, "mm2_sink_seq");
    syms.ir_sink_add = symbol_intern_cstr(g_symbols, "mm2_sink_add");
    syms.ir_sink_remove = symbol_intern_cstr(g_symbols, "mm2_sink_remove");
    syms.ir_sink_act = symbol_intern_cstr(g_symbols, "mm2_sink_act");
    syms.ir_sink_z3 = symbol_intern_cstr(g_symbols, "mm2_sink_z3");
    return syms;
}

bool cetta_mm2_atom_is_exec_rule(Atom *atom) {
    Mm2LowerSyms syms = mm2_lower_syms();
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) return false;
    Atom *head = atom->expr.elems[0];
    if (head->kind != ATOM_SYMBOL) return false;
    return atom_is_symbol_id(head, syms.surf_exec) ||
           atom_is_symbol_id(head, syms.ir_exec);
}

bool cetta_mm2_atoms_have_top_level_eval(Atom **atoms, int n) {
    if (!atoms || n <= 0) return false;
    for (int i = 0; i < n; i++) {
        if (atom_is_symbol_id(atoms[i], g_builtin_syms.bang)) {
            return true;
        }
    }
    return false;
}

static Atom *mm2_raise_atom_impl(Arena *a, Atom *atom, const Mm2LowerSyms *syms);

static Atom *mm2_raise_expr_with_head(Arena *a, Atom *atom, SymbolId head_id,
                                      const Mm2LowerSyms *syms) {
    bool changed = false;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    elems[0] = atom_symbol_id(a, head_id);
    if (elems[0] != atom->expr.elems[0]) changed = true;
    for (uint32_t i = 1; i < atom->expr.len; i++) {
        elems[i] = mm2_raise_atom_impl(a, atom->expr.elems[i], syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_raise_plain_expr(Arena *a, Atom *atom, const Mm2LowerSyms *syms) {
    bool changed = false;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        elems[i] = mm2_raise_atom_impl(a, atom->expr.elems[i], syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_raise_atom_impl(Arena *a, Atom *atom, const Mm2LowerSyms *syms) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) return atom;

    Atom *head = atom->expr.elems[0];
    if (head->kind != ATOM_SYMBOL) {
        return mm2_raise_plain_expr(a, atom, syms);
    }
    if (atom_is_symbol_id(head, g_builtin_syms.quote)) {
        return atom;
    }

    if (atom_is_symbol_id(head, syms->ir_exec)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_exec, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_and)) {
        return mm2_raise_expr_with_head(a, atom, g_builtin_syms.comma, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_btm)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_btm, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_act) ||
        atom_is_symbol_id(head, syms->ir_sink_act)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_act, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_guard_eq)) {
        return mm2_raise_expr_with_head(a, atom, g_builtin_syms.op_eq, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_guard_neq)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_neq, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_seq)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_sink_seq, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_add)) {
        return mm2_raise_expr_with_head(a, atom, g_builtin_syms.op_plus, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_remove)) {
        return mm2_raise_expr_with_head(a, atom, g_builtin_syms.op_minus, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_z3)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_z3, syms);
    }

    return mm2_raise_plain_expr(a, atom, syms);
}

static Atom *mm2_lower_atom(Arena *a, Atom *atom, Mm2LowerContext ctx,
                            const Mm2LowerSyms *syms);

static Atom *mm2_lower_expr_with_head(Arena *a, Atom *atom, Atom *new_head,
                                      uint32_t start_idx,
                                      Mm2LowerContext child_ctx,
                                      const Mm2LowerSyms *syms) {
    bool changed = (new_head != atom->expr.elems[0]);
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    elems[0] = new_head;
    for (uint32_t i = 1; i < atom->expr.len; i++) {
        Mm2LowerContext next_ctx = (i >= start_idx) ? child_ctx : MM2_CTX_GENERAL;
        elems[i] = mm2_lower_atom(a, atom->expr.elems[i], next_ctx, syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_lower_expr_mixed(Arena *a, Atom *atom, Atom *new_head,
                                  const Mm2LowerContext *child_ctxs,
                                  const Mm2LowerSyms *syms) {
    bool changed = (new_head != atom->expr.elems[0]);
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    elems[0] = new_head;
    for (uint32_t i = 1; i < atom->expr.len; i++) {
        elems[i] = mm2_lower_atom(a, atom->expr.elems[i], child_ctxs[i - 1], syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_lower_plain_expr(Arena *a, Atom *atom, const Mm2LowerSyms *syms) {
    bool changed = false;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        Mm2LowerContext next_ctx = (i == 0) ? MM2_CTX_GENERAL : MM2_CTX_GENERAL;
        elems[i] = mm2_lower_atom(a, atom->expr.elems[i], next_ctx, syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_lower_atom(Arena *a, Atom *atom, Mm2LowerContext ctx,
                            const Mm2LowerSyms *syms) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) return atom;

    Atom *head = atom->expr.elems[0];
    if (head->kind != ATOM_SYMBOL) {
        return mm2_lower_plain_expr(a, atom, syms);
    }
    if (atom_is_symbol_id(head, g_builtin_syms.quote)) {
        return atom;
    }

    if (atom_is_symbol_id(head, syms->surf_exec) && atom->expr.len == 4) {
        Mm2LowerContext child_ctxs[3] = {
            MM2_CTX_GENERAL, MM2_CTX_PATTERN, MM2_CTX_TEMPLATE
        };
        return mm2_lower_expr_mixed(a, atom, atom_symbol_id(a, syms->ir_exec),
                                    child_ctxs, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_exec) && atom->expr.len == 4) {
        Mm2LowerContext child_ctxs[3] = {
            MM2_CTX_GENERAL, MM2_CTX_PATTERN, MM2_CTX_TEMPLATE
        };
        return mm2_lower_expr_mixed(a, atom, head, child_ctxs, syms);
    }

    if (atom_is_symbol_id(head, g_builtin_syms.comma) &&
        atom->expr.len >= 3 &&
        (ctx == MM2_CTX_PATTERN || ctx == MM2_CTX_GENERAL)) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_pattern_and),
                                        1, MM2_CTX_PATTERN, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_and) && atom->expr.len >= 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_PATTERN, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_btm) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_pattern_btm),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_btm) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_act) && atom->expr.len == 3) {
        SymbolId target = (ctx == MM2_CTX_TEMPLATE) ? syms->ir_sink_act : syms->ir_pattern_act;
        return mm2_lower_expr_with_head(a, atom, atom_symbol_id(a, target),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if ((atom_is_symbol_id(head, syms->ir_pattern_act) ||
         atom_is_symbol_id(head, syms->ir_sink_act)) &&
        atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, g_builtin_syms.op_eq) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_guard_eq),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_guard_eq) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_neq) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_guard_neq),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_guard_neq) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_sink_seq) &&
        atom->expr.len >= 2) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_sink_seq),
                                        1, MM2_CTX_TEMPLATE, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_seq) && atom->expr.len >= 2) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_TEMPLATE, syms);
    }

    if (atom_is_symbol_id(head, g_builtin_syms.op_plus) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_sink_add),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_add) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, g_builtin_syms.op_minus) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_sink_remove),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_remove) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_z3) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_sink_z3),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_z3) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    return mm2_lower_plain_expr(a, atom, syms);
}

void cetta_mm2_lower_atoms(Arena *a, Atom **atoms, int n) {
    if (!a || !atoms || n <= 0) return;
    Mm2LowerSyms syms = mm2_lower_syms();
    for (int i = 0; i < n; i++) {
        atoms[i] = mm2_lower_atom(a, atoms[i], MM2_CTX_GENERAL, &syms);
    }
}

Atom *cetta_mm2_raise_atom(Arena *a, Atom *atom) {
    if (!a || !atom) return atom;
    Mm2LowerSyms syms = mm2_lower_syms();
    return mm2_raise_atom_impl(a, atom, &syms);
}

char *cetta_mm2_atom_to_surface_string(Arena *a, Atom *atom) {
    Atom *raised = cetta_mm2_raise_atom(a, atom);
    return atom_to_string(a, raised);
}

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} BridgeExprBuf;

typedef struct {
    const uint8_t *token;
    uint32_t len;
} BridgeVarSlot;

typedef struct {
    BridgeVarSlot slots[64];
    uint8_t len;
} BridgeVarMap;

static void bridge_expr_buf_init(BridgeExprBuf *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static bool bridge_expr_buf_reserve(BridgeExprBuf *buf, size_t extra) {
    if (buf->len + extra <= buf->cap)
        return true;
    size_t next_cap = buf->cap ? buf->cap : 128;
    while (next_cap < buf->len + extra)
        next_cap *= 2;
    buf->data = cetta_realloc(buf->data, next_cap);
    buf->cap = next_cap;
    return true;
}

static bool bridge_expr_buf_push_u8(BridgeExprBuf *buf, uint8_t byte) {
    if (!bridge_expr_buf_reserve(buf, 1))
        return false;
    buf->data[buf->len++] = byte;
    return true;
}

static bool bridge_expr_buf_push_bytes(BridgeExprBuf *buf,
                                       const uint8_t *bytes,
                                       size_t len) {
    if (!bridge_expr_buf_reserve(buf, len))
        return false;
    memcpy(buf->data + buf->len, bytes, len);
    buf->len += len;
    return true;
}

static bool bridge_expr_buf_push_symbol(BridgeExprBuf *buf,
                                        const uint8_t *token,
                                        uint32_t len,
                                        const char **out_error) {
    if (!token || len == 0) {
        if (out_error)
            *out_error = "MORK bridge tokens must not be empty";
        return false;
    }
    if (len >= 64) {
        if (out_error)
            *out_error = "MORK bridge tokens must be at most 63 bytes";
        return false;
    }
    return bridge_expr_buf_push_u8(buf, (uint8_t)(0xC0u | len)) &&
           bridge_expr_buf_push_bytes(buf, token, len);
}

static const uint8_t *bridge_var_token(Arena *a, Atom *atom, uint32_t *out_len) {
    const char *name = atom_name_cstr(atom);
    uint32_t name_len = (uint32_t)strlen(name);
    uint32_t epoch = var_epoch_suffix(atom->var_id);
    if (epoch == 0) {
        char *token = arena_alloc(a, (size_t)name_len + 2);
        token[0] = '$';
        memcpy(token + 1, name, name_len);
        token[name_len + 1] = '\0';
        if (out_len)
            *out_len = name_len + 1;
        return (const uint8_t *)token;
    }

    int digits = snprintf(NULL, 0, "#%u", epoch);
    if (digits < 0)
        return NULL;
    char *token = arena_alloc(a, (size_t)name_len + (size_t)digits + 2);
    token[0] = '$';
    memcpy(token + 1, name, name_len);
    snprintf(token + 1 + name_len, (size_t)digits + 1, "#%u", epoch);
    if (out_len)
        *out_len = name_len + 1 + (uint32_t)digits;
    return (const uint8_t *)token;
}

static bool bridge_var_map_index(BridgeVarMap *vars,
                                 const uint8_t *token,
                                 uint32_t len,
                                 uint8_t *out_index,
                                 bool *out_is_new) {
    for (uint8_t i = 0; i < vars->len; i++) {
        if (vars->slots[i].len == len &&
            memcmp(vars->slots[i].token, token, len) == 0) {
            if (out_index)
                *out_index = i;
            if (out_is_new)
                *out_is_new = false;
            return true;
        }
    }
    if (vars->len >= 64)
        return false;
    vars->slots[vars->len].token = token;
    vars->slots[vars->len].len = len;
    if (out_index)
        *out_index = vars->len;
    if (out_is_new)
        *out_is_new = true;
    vars->len++;
    return true;
}

static bool bridge_emit_float_token(BridgeExprBuf *buf,
                                    double value,
                                    const char **out_error) {
    char tmp[128];
    int len = 0;
    if (isnan(value)) {
        len = snprintf(tmp, sizeof(tmp), "NaN");
    } else if (isinf(value)) {
        len = snprintf(tmp, sizeof(tmp), "%s", signbit(value) ? "-inf" : "inf");
    } else if (isfinite(value) && floor(value) == value) {
        len = snprintf(tmp, sizeof(tmp), "%.1f", value);
    } else {
        len = snprintf(tmp, sizeof(tmp), "%.16g", value);
    }
    if (len <= 0 || (size_t)len >= sizeof(tmp)) {
        if (out_error)
            *out_error = "failed to format MORK bridge float token";
        return false;
    }
    return bridge_expr_buf_push_symbol(buf, (const uint8_t *)tmp,
                                       (uint32_t)len, out_error);
}

static bool bridge_emit_string_token(Arena *a,
                                     BridgeExprBuf *buf,
                                     const char *text,
                                     const char **out_error) {
    size_t text_len = strlen(text);
    char *quoted = arena_alloc(a, text_len * 2 + 3);
    size_t off = 0;
    quoted[off++] = '"';
    for (size_t i = 0; i < text_len; i++) {
        char c = text[i];
        if (c == '\n') {
            quoted[off++] = '\\';
            quoted[off++] = 'n';
        } else if (c == '"' || c == '\\') {
            quoted[off++] = '\\';
            quoted[off++] = c;
        } else {
            quoted[off++] = c;
        }
    }
    quoted[off++] = '"';
    quoted[off] = '\0';
    return bridge_expr_buf_push_symbol(buf, (const uint8_t *)quoted,
                                       (uint32_t)off, out_error);
}

static bool bridge_encode_atom_rec(Arena *a, Atom *atom, BridgeVarMap *vars,
                                   BridgeExprBuf *buf, const char **out_error) {
    if (!atom) {
        if (out_error)
            *out_error = "cannot encode null atom to MORK bridge expr bytes";
        return false;
    }

    switch (atom->kind) {
    case ATOM_SYMBOL: {
        const uint8_t *sym = (const uint8_t *)symbol_bytes(g_symbols, atom->sym_id);
        uint32_t len = symbol_len(g_symbols, atom->sym_id);
        return bridge_expr_buf_push_symbol(buf, sym, len, out_error);
    }
    case ATOM_VAR: {
        uint32_t token_len = 0;
        const uint8_t *token = bridge_var_token(a, atom, &token_len);
        uint8_t index = 0;
        bool is_new = false;
        if (!token || !bridge_var_map_index(vars, token, token_len, &index, &is_new)) {
            if (out_error)
                *out_error = "MORK bridge expressions support at most 64 distinct variables";
            return false;
        }
        return bridge_expr_buf_push_u8(
            buf, is_new ? 0xC0u : (uint8_t)(0x80u | index));
    }
    case ATOM_GROUNDED: {
        char tmp[128];
        int len = 0;
        switch (atom->ground.gkind) {
        case GV_INT:
            len = snprintf(tmp, sizeof(tmp), "%" PRId64, atom->ground.ival);
            if (len <= 0 || (size_t)len >= sizeof(tmp)) {
                if (out_error)
                    *out_error = "failed to format MORK bridge integer token";
                return false;
            }
            return bridge_expr_buf_push_symbol(buf, (const uint8_t *)tmp,
                                               (uint32_t)len, out_error);
        case GV_FLOAT:
            return bridge_emit_float_token(buf, atom->ground.fval, out_error);
        case GV_BOOL:
            return bridge_expr_buf_push_symbol(
                buf,
                (const uint8_t *)(atom->ground.bval ? "True" : "False"),
                atom->ground.bval ? 4u : 5u,
                out_error);
        case GV_STRING:
            return bridge_emit_string_token(a, buf, atom->ground.sval, out_error);
        case GV_SPACE:
            if (out_error)
                *out_error = "MORK bridge expr-byte ingress does not support grounded Space values";
            return false;
        case GV_STATE:
            if (out_error)
                *out_error = "MORK bridge expr-byte ingress does not support grounded State values";
            return false;
        case GV_CAPTURE:
            if (out_error)
                *out_error = "MORK bridge expr-byte ingress does not support capture values";
            return false;
        case GV_FOREIGN:
            if (out_error)
                *out_error = "MORK bridge expr-byte ingress does not support foreign grounded values";
            return false;
        }
        break;
    }
    case ATOM_EXPR:
        if (atom->expr.len >= 64) {
            if (out_error)
                *out_error = "MORK bridge expressions support arity at most 63";
            return false;
        }
        if (!bridge_expr_buf_push_u8(buf, (uint8_t)atom->expr.len))
            return false;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (!bridge_encode_atom_rec(a, atom->expr.elems[i], vars, buf, out_error))
                return false;
        }
        return true;
    }

    if (out_error)
        *out_error = "unknown atom kind while encoding MORK bridge expr bytes";
    return false;
}

bool cetta_mm2_atom_to_bridge_expr_bytes(Arena *a, Atom *atom,
                                         uint8_t **out_bytes,
                                         size_t *out_len,
                                         const char **out_error) {
    BridgeExprBuf buf;
    BridgeVarMap vars = {0};
    Atom *raised;

    if (out_bytes)
        *out_bytes = NULL;
    if (out_len)
        *out_len = 0;
    if (out_error)
        *out_error = NULL;
    if (!a || !atom) {
        if (out_error)
            *out_error = "cannot encode null atom to MORK bridge expr bytes";
        return false;
    }

    raised = cetta_mm2_raise_atom(a, atom);
    bridge_expr_buf_init(&buf);
    if (!bridge_encode_atom_rec(a, raised, &vars, &buf, out_error)) {
        free(buf.data);
        return false;
    }

    if (out_bytes)
        *out_bytes = buf.data;
    else
        free(buf.data);
    if (out_len)
        *out_len = buf.len;
    return true;
}

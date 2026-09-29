#include "parser_pack_table_snapshot_v1.h"

#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void snap_error(char *buf, size_t size, const char *msg) {
    if (buf && size)
        snprintf(buf, size, "%s", msg ? msg : "table snapshot failure");
}

static bool digest_ok(const char *d) {
    size_t i;
    if (!d || strlen(d) != 64u)
        return false;
    for (i = 0u; i < 64u; i++) {
        char c = d[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

static bool profile_ok(const char *profile) {
    size_t i;
    size_t len;
    if (!profile)
        return false;
    len = strlen(profile);
    if (len == 0u || len >= PP_TABLE_SNAPSHOT_V1_PROFILE_CAP)
        return false;
    for (i = 0u; i < len; i++) {
        char c = profile[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-'))
            return false;
    }
    return true;
}

void pp_table_snapshot_v1_init(PPTableSnapshotV1 *snap) {
    if (!snap)
        return;
    memset(snap, 0, sizeof(*snap));
    rsdfa_v1_program_init(&snap->dfa);
    cetta_lp_native_slr_program_init(&snap->slr);
}

void pp_table_snapshot_v1_free(PPTableSnapshotV1 *snap) {
    uint32_t i;
    if (!snap)
        return;
    rsdfa_v1_program_free(&snap->dfa);
    cetta_lp_native_slr_program_free(&snap->slr);
    if (snap->tag_names) {
        for (i = 0u; i < snap->tag_name_len; i++)
            free(snap->tag_names[i]);
        free(snap->tag_names);
    }
    free(snap->skip_tags);
    free(snap->origins);
    if (snap->tag_fixed_texts) {
        for (i = 0u; i < snap->tag_name_len; i++)
            free(snap->tag_fixed_texts[i]);
        free(snap->tag_fixed_texts);
    }
    free(snap->tag_carries_lexeme);
    free(snap->tag_symbol_ids);
    memset(snap, 0, sizeof(*snap));
}

static size_t utf8_encode_scalar(uint32_t scalar, char *out) {
    if (scalar < 0x80u) {
        out[0] = (char)scalar;
        return 1u;
    }
    if (scalar < 0x800u) {
        out[0] = (char)(0xC0u | (scalar >> 6));
        out[1] = (char)(0x80u | (scalar & 0x3Fu));
        return 2u;
    }
    if (scalar < 0x10000u) {
        out[0] = (char)(0xE0u | (scalar >> 12));
        out[1] = (char)(0x80u | ((scalar >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (scalar & 0x3Fu));
        return 3u;
    }
    out[0] = (char)(0xF0u | (scalar >> 18));
    out[1] = (char)(0x80u | ((scalar >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((scalar >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (scalar & 0x3Fu));
    return 4u;
}

bool pp_table_snapshot_v1_derive_tag_facts(
    PPTableSnapshotV1 *snap,
    char *error_buf,
    size_t error_buf_size) {
    enum { FIXED_CAP = 256u };
    uint32_t scalars[FIXED_CAP];
    uint32_t tag;

    if (!snap || snap->tag_name_len == 0u) {
        snap_error(error_buf, error_buf_size, "snapshot has no tags");
        return false;
    }
    snap->tag_carries_lexeme = calloc(snap->tag_name_len, sizeof(*snap->tag_carries_lexeme));
    snap->tag_fixed_texts = calloc(snap->tag_name_len, sizeof(*snap->tag_fixed_texts));
    snap->tag_symbol_ids = calloc(snap->tag_name_len, sizeof(*snap->tag_symbol_ids));
    if (!snap->tag_carries_lexeme || !snap->tag_fixed_texts || !snap->tag_symbol_ids) {
        snap_error(error_buf, error_buf_size, "tag facts allocation failed");
        return false;
    }
    for (tag = 0u; tag < snap->tag_name_len; tag++)
        snap->tag_symbol_ids[tag] = snap->tag_names && snap->tag_names[tag]
            ? symbol_intern_cstr(g_symbols, snap->tag_names[tag])
            : SYMBOL_ID_NONE;
    for (tag = 0u; tag < snap->tag_name_len && tag < snap->dfa.tag_len; tag++) {
        uint32_t count = 0u;
        uint32_t length = 0u;
        if (!rsdfa_v1_program_tag_language_size(
                &snap->dfa, tag, &count, scalars, FIXED_CAP, &length)) {
            snap_error(error_buf, error_buf_size, "tag language analysis failed");
            return false;
        }
        if (count >= 2u) {
            snap->tag_carries_lexeme[tag] = 1u;
        } else if (count == 1u) {
            char *text = malloc((size_t)length * 4u + 1u);
            size_t used = 0u;
            uint32_t k;
            if (!text) {
                snap_error(error_buf, error_buf_size, "tag text allocation failed");
                return false;
            }
            for (k = 0u; k < length; k++)
                used += utf8_encode_scalar(scalars[k], text + used);
            text[used] = '\0';
            snap->tag_fixed_texts[tag] = text;
        }
    }
    return true;
}

static void put_u32(uint8_t **cur, uint32_t v) {
    (*cur)[0] = (uint8_t)v;
    (*cur)[1] = (uint8_t)(v >> 8);
    (*cur)[2] = (uint8_t)(v >> 16);
    (*cur)[3] = (uint8_t)(v >> 24);
    *cur += 4u;
}

static bool get_u32(const uint8_t **cur, const uint8_t *end, uint32_t *out) {
    if (!cur || !*cur || !end || !out || *cur + 4u > end)
        return false;
    *out = (uint32_t)(*cur)[0] | ((uint32_t)(*cur)[1] << 8) |
           ((uint32_t)(*cur)[2] << 16) | ((uint32_t)(*cur)[3] << 24);
    *cur += 4u;
    return true;
}

static void put_bytes(uint8_t **cur, const void *p, size_t n) {
    if (n)
        memcpy(*cur, p, n);
    *cur += n;
}

static bool get_bytes(const uint8_t **cur, const uint8_t *end, void *p, size_t n) {
    if (!cur || !*cur || *cur + n > end)
        return false;
    if (n)
        memcpy(p, *cur, n);
    *cur += n;
    return true;
}

typedef struct {
    SymbolId *ids;
    char **names;
    uint32_t len;
    uint32_t cap;
} SnapNames;

static void names_init(SnapNames *n) {
    memset(n, 0, sizeof(*n));
}

static void names_free(SnapNames *n) {
    uint32_t i;
    if (!n)
        return;
    for (i = 0u; i < n->len; i++)
        free(n->names[i]);
    free(n->ids);
    free(n->names);
    memset(n, 0, sizeof(*n));
}

static int32_t names_find(const SnapNames *n, SymbolId id) {
    uint32_t i;
    for (i = 0u; i < n->len; i++) {
        if (n->ids[i] == id)
            return (int32_t)i;
    }
    return -1;
}

static bool names_add(SnapNames *n, SymbolId id) {
    const char *text;
    char *copy;
    size_t tlen;
    if (id == SYMBOL_ID_NONE)
        return true;
    if (names_find(n, id) >= 0)
        return true;
    text = symbol_bytes(g_symbols, id);
    if (!text)
        text = "";
    tlen = strlen(text);
    copy = malloc(tlen + 1u);
    if (!copy)
        return false;
    memcpy(copy, text, tlen + 1u);
    if (n->len == n->cap) {
        uint32_t ncap = n->cap ? n->cap * 2u : 16u;
        SymbolId *ids = realloc(n->ids, sizeof(*ids) * ncap);
        char **nms = realloc(n->names, sizeof(*nms) * ncap);
        if (!ids || !nms) {
            free(copy);
            free(ids);
            free(nms);
            return false;
        }
        n->ids = ids;
        n->names = nms;
        n->cap = ncap;
    }
    n->ids[n->len] = id;
    n->names[n->len] = copy;
    n->len++;
    return true;
}

static bool names_add_sym(SnapNames *n, const CettaLpNativeSymbol *s) {
    return names_add(n, s->name) && names_add(n, s->scope);
}

static bool collect_slr_names(const CettaLpNativeSlrProgram *slr, SnapNames *n) {
    uint32_t i;
    if (!names_add(n, slr->start_nonterminal))
        return false;
    for (i = 0u; i < slr->terminal_len; i++) {
        if (!names_add(n, slr->terminals[i]))
            return false;
    }
    for (i = 0u; i < slr->nonterminal_len; i++) {
        if (!names_add(n, slr->nonterminals[i]))
            return false;
    }
    for (i = 0u; i < slr->production_len; i++) {
        if (!names_add(n, slr->productions[i].label) ||
            !names_add(n, slr->productions[i].lhs))
            return false;
    }
    for (i = 0u; i < slr->rhs_len; i++) {
        if (!names_add_sym(n, &slr->rhs[i]))
            return false;
    }
    return true;
}

static uint32_t map_id(const SnapNames *n, SymbolId id) {
    int32_t f;
    if (id == SYMBOL_ID_NONE)
        return UINT32_MAX;
    f = names_find(n, id);
    return f < 0 ? UINT32_MAX : (uint32_t)f;
}

bool pp_table_snapshot_v1_size(
    const PPTableSnapshotV1 *snap,
    size_t *out_size,
    char *error_buf,
    size_t error_buf_size) {
    SnapNames names;
    size_t size;
    uint32_t i;
    char dfa_err[256] = {0};

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    if (out_size)
        *out_size = 0u;
    if (!snap || !out_size || !digest_ok(snap->syntax_digest) ||
        !digest_ok(snap->artifact_digest) || !profile_ok(snap->profile)) {
        snap_error(error_buf, error_buf_size, "invalid snapshot for size");
        return false;
    }
    if (!rsdfa_v1_program_validate(&snap->dfa, dfa_err, sizeof(dfa_err))) {
        snap_error(error_buf, error_buf_size,
                   dfa_err[0] ? dfa_err : "invalid DFA program");
        return false;
    }
    if (snap->slr.production_len != 0u ||
        snap->kernel == PP_TABLE_SNAPSHOT_V1_KERNEL_SLR) {
        if (!cetta_lp_native_slr_program_validate(
                &snap->slr, dfa_err, sizeof(dfa_err))) {
            snap_error(error_buf, error_buf_size,
                       dfa_err[0] ? dfa_err : "invalid SLR program");
            return false;
        }
    }
    if (snap->slr.glr_action_len > 0u && !snap->slr.glr_actions) {
        snap_error(error_buf, error_buf_size, "GLR actions missing");
        return false;
    }
    names_init(&names);
    if (!collect_slr_names(&snap->slr, &names)) {
        names_free(&names);
        snap_error(error_buf, error_buf_size, "cannot collect SLR names");
        return false;
    }
    if (snap->origin_len > 0u && !snap->origins) {
        names_free(&names);
        snap_error(error_buf, error_buf_size, "helper origins missing");
        return false;
    }
    for (i = 0u; i < snap->origin_len; i++) {
        if (names_find(&names, snap->origins[i].helper) < 0 ||
            names_find(&names, snap->origins[i].owner) < 0) {
            names_free(&names);
            snap_error(error_buf, error_buf_size, "helper origin names no parser symbol");
            return false;
        }
    }
    size = 4u + 4u + 64u + 64u + 4u + strlen(snap->profile) + 4u + 4u;
    size += 4u; /* tag_name_len */
    for (i = 0u; i < snap->tag_name_len; i++) {
        const char *nm = snap->tag_names && snap->tag_names[i]
                             ? snap->tag_names[i]
                             : "";
        size += 4u + strlen(nm);
    }
    size += 4u + (size_t)snap->skip_tag_len * 4u;
    size += 4u * 8u; /* dfa header fields */
    size += (size_t)snap->dfa.state_len * 4u * 6u;
    size += (size_t)snap->dfa.transition_len * 4u * 3u;
    size += (size_t)snap->dfa.accept_tag_len * 4u;
    size += (size_t)snap->dfa.eof_accept_tag_len * 4u;
    size += 4u; /* name count */
    for (i = 0u; i < names.len; i++)
        size += 4u + strlen(names.names[i]);
    size += 4u * 10u; /* slr counts + start + glr_action_len pad */
    size += (size_t)snap->slr.terminal_len * 4u;
    size += (size_t)snap->slr.nonterminal_len * 4u;
    size += (size_t)snap->slr.production_len * 4u * 5u;
    size += (size_t)snap->slr.rhs_len * 4u * 3u;
    size += (size_t)snap->slr.action_len * 4u * 2u;
    size += (size_t)snap->slr.goto_len * 4u;
    size += 4u * 6u; /* summary */
    size += (size_t)snap->slr.glr_action_len * 4u * 4u;
    size += 4u + (size_t)snap->origin_len * 4u * 4u;
    names_free(&names);
    *out_size = size;
    return true;
}

bool pp_table_snapshot_v1_write(
    const PPTableSnapshotV1 *snap,
    uint8_t *out_bytes,
    size_t out_size,
    size_t *out_written,
    char *error_buf,
    size_t error_buf_size) {
    size_t required = 0u;
    uint8_t *cur;
    SnapNames names;
    uint32_t i;

    if (out_written)
        *out_written = 0u;
    if (!pp_table_snapshot_v1_size(snap, &required, error_buf, error_buf_size))
        return false;
    if (!out_bytes || out_size < required) {
        snap_error(error_buf, error_buf_size, "snapshot buffer is too small");
        return false;
    }
    names_init(&names);
    if (!collect_slr_names(&snap->slr, &names)) {
        names_free(&names);
        snap_error(error_buf, error_buf_size, "cannot collect SLR names");
        return false;
    }
    cur = out_bytes;
    put_bytes(&cur, PP_TABLE_SNAPSHOT_V1_MAGIC, 4u);
    put_u32(&cur, PP_TABLE_SNAPSHOT_V1_VERSION);
    put_bytes(&cur, snap->syntax_digest, 64u);
    put_bytes(&cur, snap->artifact_digest, 64u);
    put_u32(&cur, (uint32_t)strlen(snap->profile));
    put_bytes(&cur, snap->profile, strlen(snap->profile));
    put_u32(&cur, (uint32_t)snap->kernel);
    put_u32(&cur, snap->conflict_len);
    put_u32(&cur, snap->tag_name_len);
    for (i = 0u; i < snap->tag_name_len; i++) {
        const char *nm = snap->tag_names && snap->tag_names[i]
                             ? snap->tag_names[i]
                             : "";
        uint32_t nlen = (uint32_t)strlen(nm);
        put_u32(&cur, nlen);
        put_bytes(&cur, nm, nlen);
    }
    put_u32(&cur, snap->skip_tag_len);
    for (i = 0u; i < snap->skip_tag_len; i++)
        put_u32(&cur, snap->skip_tags[i]);
    put_u32(&cur, snap->dfa.state_len);
    put_u32(&cur, snap->dfa.transition_len);
    put_u32(&cur, snap->dfa.tag_len);
    put_u32(&cur, snap->dfa.accept_tag_len);
    put_u32(&cur, snap->dfa.eof_accept_tag_len);
    put_u32(&cur, snap->dfa.start_state);
    put_u32(&cur, 0u);
    put_u32(&cur, 0u);
    for (i = 0u; i < snap->dfa.state_len; i++) {
        const RSDFAV1ProgramState *st = &snap->dfa.states[i];
        put_u32(&cur, st->transition_begin);
        put_u32(&cur, st->transition_len);
        put_u32(&cur, st->accept_begin);
        put_u32(&cur, st->accept_len);
        put_u32(&cur, st->eof_accept_begin);
        put_u32(&cur, st->eof_accept_len);
    }
    for (i = 0u; i < snap->dfa.transition_len; i++) {
        const RSDFAV1ProgramTransition *tr = &snap->dfa.transitions[i];
        put_u32(&cur, tr->low);
        put_u32(&cur, tr->high);
        put_u32(&cur, tr->target);
    }
    for (i = 0u; i < snap->dfa.accept_tag_len; i++)
        put_u32(&cur, snap->dfa.accept_tags[i]);
    for (i = 0u; i < snap->dfa.eof_accept_tag_len; i++)
        put_u32(&cur, snap->dfa.eof_accept_tags[i]);

    put_u32(&cur, names.len);
    for (i = 0u; i < names.len; i++) {
        uint32_t nlen = (uint32_t)strlen(names.names[i]);
        put_u32(&cur, nlen);
        put_bytes(&cur, names.names[i], nlen);
    }
    put_u32(&cur, map_id(&names, snap->slr.start_nonterminal));
    put_u32(&cur, snap->slr.terminal_len);
    put_u32(&cur, snap->slr.nonterminal_len);
    put_u32(&cur, snap->slr.production_len);
    put_u32(&cur, snap->slr.authored_production_len);
    put_u32(&cur, snap->slr.rhs_len);
    put_u32(&cur, snap->slr.action_len);
    put_u32(&cur, snap->slr.goto_len);
    put_u32(&cur, snap->slr.glr_action_len);
    put_u32(&cur, 0u);
    for (i = 0u; i < snap->slr.terminal_len; i++)
        put_u32(&cur, map_id(&names, snap->slr.terminals[i]));
    for (i = 0u; i < snap->slr.nonterminal_len; i++)
        put_u32(&cur, map_id(&names, snap->slr.nonterminals[i]));
    for (i = 0u; i < snap->slr.production_len; i++) {
        const CettaLpNativeSlrProgramProduction *p = &snap->slr.productions[i];
        put_u32(&cur, map_id(&names, p->label));
        put_u32(&cur, map_id(&names, p->lhs));
        put_u32(&cur, p->rhs_begin);
        put_u32(&cur, p->rhs_len);
        put_u32(&cur, (p->authored ? 1u : 0u) | (p->avoided ? 2u : 0u));
    }
    for (i = 0u; i < snap->slr.rhs_len; i++) {
        const CettaLpNativeSymbol *s = &snap->slr.rhs[i];
        put_u32(&cur, (uint32_t)s->kind);
        put_u32(&cur, map_id(&names, s->name));
        put_u32(&cur, map_id(&names, s->scope));
    }
    for (i = 0u; i < snap->slr.action_len; i++) {
        put_u32(&cur, (uint32_t)snap->slr.actions[i].kind);
        put_u32(&cur, (uint32_t)snap->slr.actions[i].value);
    }
    for (i = 0u; i < snap->slr.goto_len; i++)
        put_u32(&cur, snap->slr.gotos[i]);
    put_u32(&cur, snap->slr.summary.state_len);
    put_u32(&cur, snap->slr.summary.shift_len);
    put_u32(&cur, snap->slr.summary.goto_len);
    put_u32(&cur, snap->slr.summary.reduce_len);
    put_u32(&cur, snap->slr.summary.accept_len);
    put_u32(&cur, snap->slr.summary.conflict_len);
    for (i = 0u; i < snap->slr.glr_action_len; i++) {
        const CettaLpNativeGlrAction *a = &snap->slr.glr_actions[i];
        put_u32(&cur, a->state);
        put_u32(&cur, a->token_idx);
        put_u32(&cur, (uint32_t)a->kind);
        put_u32(&cur, (uint32_t)a->value);
    }
    put_u32(&cur, snap->origin_len);
    for (i = 0u; i < snap->origin_len; i++) {
        put_u32(&cur, map_id(&names, snap->origins[i].helper));
        put_u32(&cur, map_id(&names, snap->origins[i].owner));
        put_u32(&cur, snap->origins[i].kind);
        put_u32(&cur, snap->origins[i].repeat);
    }
    names_free(&names);
    if ((size_t)(cur - out_bytes) != required) {
        snap_error(error_buf, error_buf_size, "snapshot size mismatch");
        return false;
    }
    *out_written = required;
    return true;
}

static bool alloc_u32(uint32_t **out, uint32_t n) {
    if (n == 0u) {
        *out = NULL;
        return true;
    }
    *out = calloc(n, sizeof(uint32_t));
    return *out != NULL;
}

bool pp_table_snapshot_v1_read(
    PPTableSnapshotV1 *out,
    const uint8_t *bytes,
    size_t size,
    char *error_buf,
    size_t error_buf_size) {
    const uint8_t *cur;
    const uint8_t *end;
    uint32_t version = 0u;
    uint32_t kernel = 0u;
    uint32_t i;
    uint32_t name_len = 0u;
    uint32_t profile_len = 0u;
    char **sym_names = NULL;
    SymbolId *sym_ids = NULL;
    PPTableSnapshotV1 snap;
    char mag[4];

    if (error_buf && error_buf_size)
        error_buf[0] = '\0';
    pp_table_snapshot_v1_init(&snap);
    if (!out || !bytes || size < 8u) {
        snap_error(error_buf, error_buf_size, "truncated snapshot");
        return false;
    }
    cur = bytes;
    end = bytes + size;
    if (!get_bytes(&cur, end, mag, 4u) || memcmp(mag, PP_TABLE_SNAPSHOT_V1_MAGIC, 4u) != 0) {
        snap_error(error_buf, error_buf_size, "bad snapshot magic");
        return false;
    }
    if (!get_u32(&cur, end, &version) || version != PP_TABLE_SNAPSHOT_V1_VERSION) {
        snap_error(error_buf, error_buf_size, "unsupported snapshot version");
        return false;
    }
    if (!get_bytes(&cur, end, snap.syntax_digest, 64u))
        goto fail;
    snap.syntax_digest[64] = '\0';
    if (!get_bytes(&cur, end, snap.artifact_digest, 64u))
        goto fail;
    snap.artifact_digest[64] = '\0';
    if (!get_u32(&cur, end, &profile_len) ||
        profile_len == 0u || profile_len >= sizeof(snap.profile) ||
        !get_bytes(&cur, end, snap.profile, profile_len))
        goto fail;
    snap.profile[profile_len] = '\0';
    if (!digest_ok(snap.syntax_digest) ||
        !digest_ok(snap.artifact_digest) || !profile_ok(snap.profile) ||
        !get_u32(&cur, end, &kernel) ||
        !get_u32(&cur, end, &snap.conflict_len))
        goto fail;
    snap.kernel = (PPTableSnapshotV1Kernel)kernel;
    if (!get_u32(&cur, end, &snap.tag_name_len))
        goto fail;
    if (snap.tag_name_len) {
        snap.tag_names = calloc(snap.tag_name_len, sizeof(*snap.tag_names));
        if (!snap.tag_names)
            goto fail;
    }
    for (i = 0u; i < snap.tag_name_len; i++) {
        uint32_t nlen = 0u;
        if (!get_u32(&cur, end, &nlen) || cur + nlen > end)
            goto fail;
        snap.tag_names[i] = malloc(nlen + 1u);
        if (!snap.tag_names[i])
            goto fail;
        memcpy(snap.tag_names[i], cur, nlen);
        snap.tag_names[i][nlen] = '\0';
        cur += nlen;
    }
    if (!get_u32(&cur, end, &snap.skip_tag_len))
        goto fail;
    if (!alloc_u32(&snap.skip_tags, snap.skip_tag_len))
        goto fail;
    for (i = 0u; i < snap.skip_tag_len; i++) {
        if (!get_u32(&cur, end, &snap.skip_tags[i]))
            goto fail;
    }
    if (!get_u32(&cur, end, &snap.dfa.state_len) ||
        !get_u32(&cur, end, &snap.dfa.transition_len) ||
        !get_u32(&cur, end, &snap.dfa.tag_len) ||
        !get_u32(&cur, end, &snap.dfa.accept_tag_len) ||
        !get_u32(&cur, end, &snap.dfa.eof_accept_tag_len) ||
        !get_u32(&cur, end, &snap.dfa.start_state))
        goto fail;
    {
        uint32_t pad0, pad1;
        if (!get_u32(&cur, end, &pad0) || !get_u32(&cur, end, &pad1))
            goto fail;
    }
    if (snap.dfa.state_len) {
        snap.dfa.states = calloc(snap.dfa.state_len, sizeof(*snap.dfa.states));
        if (!snap.dfa.states)
            goto fail;
    }
    for (i = 0u; i < snap.dfa.state_len; i++) {
        if (!get_u32(&cur, end, &snap.dfa.states[i].transition_begin) ||
            !get_u32(&cur, end, &snap.dfa.states[i].transition_len) ||
            !get_u32(&cur, end, &snap.dfa.states[i].accept_begin) ||
            !get_u32(&cur, end, &snap.dfa.states[i].accept_len) ||
            !get_u32(&cur, end, &snap.dfa.states[i].eof_accept_begin) ||
            !get_u32(&cur, end, &snap.dfa.states[i].eof_accept_len))
            goto fail;
    }
    if (snap.dfa.transition_len) {
        snap.dfa.transitions =
            calloc(snap.dfa.transition_len, sizeof(*snap.dfa.transitions));
        if (!snap.dfa.transitions)
            goto fail;
    }
    for (i = 0u; i < snap.dfa.transition_len; i++) {
        if (!get_u32(&cur, end, &snap.dfa.transitions[i].low) ||
            !get_u32(&cur, end, &snap.dfa.transitions[i].high) ||
            !get_u32(&cur, end, &snap.dfa.transitions[i].target))
            goto fail;
    }
    if (!alloc_u32(&snap.dfa.accept_tags, snap.dfa.accept_tag_len))
        goto fail;
    for (i = 0u; i < snap.dfa.accept_tag_len; i++) {
        if (!get_u32(&cur, end, &snap.dfa.accept_tags[i]))
            goto fail;
    }
    if (!alloc_u32(&snap.dfa.eof_accept_tags, snap.dfa.eof_accept_tag_len))
        goto fail;
    for (i = 0u; i < snap.dfa.eof_accept_tag_len; i++) {
        if (!get_u32(&cur, end, &snap.dfa.eof_accept_tags[i]))
            goto fail;
    }
    if (!get_u32(&cur, end, &name_len))
        goto fail;
    if (name_len) {
        sym_names = calloc(name_len, sizeof(*sym_names));
        sym_ids = calloc(name_len, sizeof(*sym_ids));
        if (!sym_names || !sym_ids)
            goto fail;
    }
    for (i = 0u; i < name_len; i++) {
        uint32_t nlen = 0u;
        if (!get_u32(&cur, end, &nlen) || cur + nlen > end)
            goto fail;
        sym_names[i] = malloc(nlen + 1u);
        if (!sym_names[i])
            goto fail;
        memcpy(sym_names[i], cur, nlen);
        sym_names[i][nlen] = '\0';
        cur += nlen;
        if (!g_symbols) {
            snap_error(error_buf, error_buf_size, "snapshot load needs a symbol table");
            goto fail;
        }
        sym_ids[i] = symbol_intern_cstr(g_symbols, sym_names[i]);
        if (sym_ids[i] == SYMBOL_ID_NONE) {
            snap_error(error_buf, error_buf_size, "cannot intern snapshot symbol");
            goto fail;
        }
    }
#define MAP(idx) (((idx) == UINT32_MAX) ? SYMBOL_ID_NONE : \
        (((idx) < name_len) ? sym_ids[(idx)] : SYMBOL_ID_NONE))
    {
        uint32_t start_idx = 0u;
        if (!get_u32(&cur, end, &start_idx))
            goto fail;
        snap.slr.start_nonterminal = MAP(start_idx);
    }
    if (!get_u32(&cur, end, &snap.slr.terminal_len) ||
        !get_u32(&cur, end, &snap.slr.nonterminal_len) ||
        !get_u32(&cur, end, &snap.slr.production_len) ||
        !get_u32(&cur, end, &snap.slr.authored_production_len) ||
        !get_u32(&cur, end, &snap.slr.rhs_len) ||
        !get_u32(&cur, end, &snap.slr.action_len) ||
        !get_u32(&cur, end, &snap.slr.goto_len))
        goto fail;
    {
        uint32_t glr_len = 0u, pad1 = 0u;
        if (!get_u32(&cur, end, &glr_len) || !get_u32(&cur, end, &pad1))
            goto fail;
        snap.slr.glr_action_len = glr_len;
    }
    if (snap.slr.terminal_len) {
        snap.slr.terminals = calloc(snap.slr.terminal_len, sizeof(*snap.slr.terminals));
        if (!snap.slr.terminals)
            goto fail;
    }
    for (i = 0u; i < snap.slr.terminal_len; i++) {
        uint32_t idx = 0u;
        if (!get_u32(&cur, end, &idx))
            goto fail;
        snap.slr.terminals[i] = MAP(idx);
    }
    if (snap.slr.nonterminal_len) {
        snap.slr.nonterminals =
            calloc(snap.slr.nonterminal_len, sizeof(*snap.slr.nonterminals));
        if (!snap.slr.nonterminals)
            goto fail;
    }
    for (i = 0u; i < snap.slr.nonterminal_len; i++) {
        uint32_t idx = 0u;
        if (!get_u32(&cur, end, &idx))
            goto fail;
        snap.slr.nonterminals[i] = MAP(idx);
    }
    if (snap.slr.production_len) {
        snap.slr.productions =
            calloc(snap.slr.production_len, sizeof(*snap.slr.productions));
        if (!snap.slr.productions)
            goto fail;
    }
    for (i = 0u; i < snap.slr.production_len; i++) {
        uint32_t lab = 0u, lhs = 0u, flags = 0u;
        if (!get_u32(&cur, end, &lab) || !get_u32(&cur, end, &lhs) ||
            !get_u32(&cur, end, &snap.slr.productions[i].rhs_begin) ||
            !get_u32(&cur, end, &snap.slr.productions[i].rhs_len) ||
            !get_u32(&cur, end, &flags) || flags > 3u)
            goto fail;
        snap.slr.productions[i].label = MAP(lab);
        snap.slr.productions[i].lhs = MAP(lhs);
        snap.slr.productions[i].authored = (flags & 1u) != 0u;
        snap.slr.productions[i].avoided = (flags & 2u) != 0u;
    }
    if (snap.slr.rhs_len) {
        snap.slr.rhs = calloc(snap.slr.rhs_len, sizeof(*snap.slr.rhs));
        if (!snap.slr.rhs)
            goto fail;
    }
    for (i = 0u; i < snap.slr.rhs_len; i++) {
        uint32_t kind = 0u, name = 0u, scope = 0u;
        if (!get_u32(&cur, end, &kind) || !get_u32(&cur, end, &name) ||
            !get_u32(&cur, end, &scope))
            goto fail;
        snap.slr.rhs[i].kind = (CettaLpNativeSymbolKind)kind;
        snap.slr.rhs[i].name = MAP(name);
        snap.slr.rhs[i].scope = MAP(scope);
    }
    if (snap.slr.action_len) {
        snap.slr.actions = calloc(snap.slr.action_len, sizeof(*snap.slr.actions));
        if (!snap.slr.actions)
            goto fail;
    }
    for (i = 0u; i < snap.slr.action_len; i++) {
        uint32_t kind = 0u, value = 0u;
        if (!get_u32(&cur, end, &kind) || !get_u32(&cur, end, &value))
            goto fail;
        snap.slr.actions[i].kind = (CettaLpNativeSlrProgramActionKind)kind;
        snap.slr.actions[i].value = (int32_t)value;
    }
    if (!alloc_u32(&snap.slr.gotos, snap.slr.goto_len))
        goto fail;
    for (i = 0u; i < snap.slr.goto_len; i++) {
        if (!get_u32(&cur, end, &snap.slr.gotos[i]))
            goto fail;
    }
    if (!get_u32(&cur, end, &snap.slr.summary.state_len) ||
        !get_u32(&cur, end, &snap.slr.summary.shift_len) ||
        !get_u32(&cur, end, &snap.slr.summary.goto_len) ||
        !get_u32(&cur, end, &snap.slr.summary.reduce_len) ||
        !get_u32(&cur, end, &snap.slr.summary.accept_len) ||
        !get_u32(&cur, end, &snap.slr.summary.conflict_len))
        goto fail;
    if (snap.slr.glr_action_len) {
        snap.slr.glr_actions =
            calloc(snap.slr.glr_action_len, sizeof(*snap.slr.glr_actions));
        if (!snap.slr.glr_actions)
            goto fail;
    }
    for (i = 0u; i < snap.slr.glr_action_len; i++) {
        uint32_t state = 0u, tok = 0u, kind = 0u, value = 0u;
        if (!get_u32(&cur, end, &state) || !get_u32(&cur, end, &tok) ||
            !get_u32(&cur, end, &kind) || !get_u32(&cur, end, &value))
            goto fail;
        snap.slr.glr_actions[i].state = state;
        snap.slr.glr_actions[i].token_idx = tok;
        snap.slr.glr_actions[i].kind = (CettaLpNativeSlrProgramActionKind)kind;
        snap.slr.glr_actions[i].value = (int32_t)value;
    }
    if (!get_u32(&cur, end, &snap.origin_len))
        goto fail;
    if (snap.origin_len) {
        if (snap.origin_len > (uint32_t)((size_t)(end - cur) / 16u))
            goto fail;
        snap.origins = calloc(snap.origin_len, sizeof(*snap.origins));
        if (!snap.origins)
            goto fail;
    }
    for (i = 0u; i < snap.origin_len; i++) {
        uint32_t helper = 0u, owner = 0u;
        if (!get_u32(&cur, end, &helper) || !get_u32(&cur, end, &owner) ||
            !get_u32(&cur, end, &snap.origins[i].kind) ||
            !get_u32(&cur, end, &snap.origins[i].repeat))
            goto fail;
        snap.origins[i].helper = MAP(helper);
        snap.origins[i].owner = MAP(owner);
    }
#undef MAP
    if (cur != end) {
        snap_error(error_buf, error_buf_size, "snapshot trailing bytes");
        goto fail;
    }
    if (sym_names) {
        for (i = 0u; i < name_len; i++)
            free(sym_names[i]);
        free(sym_names);
    }
    free(sym_ids);
    sym_ids = NULL;
    sym_names = NULL;
    if (!pp_table_snapshot_v1_derive_tag_facts(&snap, error_buf, error_buf_size))
        goto fail;
    pp_table_snapshot_v1_free(out);
    *out = snap;
    return true;

fail:
    if (sym_names) {
        for (i = 0u; i < name_len; i++)
            free(sym_names[i]);
        free(sym_names);
    }
    free(sym_ids);
    pp_table_snapshot_v1_free(&snap);
    if (error_buf && error_buf_size && error_buf[0] == '\0')
        snap_error(error_buf, error_buf_size, "snapshot read failed");
    return false;
}

bool pp_table_snapshot_v1_write_path(
    const PPTableSnapshotV1 *snap,
    const char *path,
    char *error_buf,
    size_t error_buf_size) {
    size_t size = 0u;
    size_t written = 0u;
    uint8_t *buf;
    FILE *file;
    bool ok = false;

    if (!pp_table_snapshot_v1_size(snap, &size, error_buf, error_buf_size))
        return false;
    buf = malloc(size);
    if (!buf) {
        snap_error(error_buf, error_buf_size, "out of memory writing snapshot");
        return false;
    }
    if (!pp_table_snapshot_v1_write(snap, buf, size, &written, error_buf,
                                    error_buf_size)) {
        free(buf);
        return false;
    }
    file = fopen(path, "wb");
    if (!file) {
        free(buf);
        snap_error(error_buf, error_buf_size, "cannot open snapshot for write");
        return false;
    }
    ok = fwrite(buf, 1u, written, file) == written;
    fclose(file);
    free(buf);
    if (!ok)
        snap_error(error_buf, error_buf_size, "snapshot write short");
    return ok;
}

bool pp_table_snapshot_v1_read_path(
    PPTableSnapshotV1 *out,
    const char *path,
    char *error_buf,
    size_t error_buf_size) {
    FILE *file;
    struct stat st;
    uint8_t *buf;
    bool ok;

    if (!path || stat(path, &st) != 0 || st.st_size < 0) {
        snap_error(error_buf, error_buf_size, "cannot stat snapshot");
        return false;
    }
    file = fopen(path, "rb");
    if (!file) {
        snap_error(error_buf, error_buf_size, "cannot open snapshot");
        return false;
    }
    buf = malloc((size_t)st.st_size);
    if (!buf) {
        fclose(file);
        snap_error(error_buf, error_buf_size, "out of memory reading snapshot");
        return false;
    }
    if (fread(buf, 1u, (size_t)st.st_size, file) != (size_t)st.st_size) {
        fclose(file);
        free(buf);
        snap_error(error_buf, error_buf_size, "snapshot read short");
        return false;
    }
    fclose(file);
    ok = pp_table_snapshot_v1_read(out, buf, (size_t)st.st_size, error_buf,
                                   error_buf_size);
    free(buf);
    return ok;
}

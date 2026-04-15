#include "term_universe.h"
#include "match.h"
#include "stats.h"
#include "term_canon.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CETTA_TERM_ENTRY_ALIGN 8u
#define CETTA_TERM_HDR_HAS_VARS 0x80000000u
#define CETTA_TERM_HDR_DATA_MASK 0x7fffffffu

static inline uint32_t term_universe_aux_make(uint32_t data, bool has_vars) {
    return (data & CETTA_TERM_HDR_DATA_MASK) |
           (has_vars ? CETTA_TERM_HDR_HAS_VARS : 0u);
}

static inline uint32_t term_universe_aux_data(const CettaTermHdr *hdr) {
    return hdr ? (hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) : 0u;
}

static inline bool term_universe_hdr_has_vars(const CettaTermHdr *hdr) {
    return hdr && (hdr->aux32 & CETTA_TERM_HDR_HAS_VARS) != 0;
}

static inline uint32_t term_universe_align_up(uint32_t n) {
    return (n + (CETTA_TERM_ENTRY_ALIGN - 1u)) &
           ~(CETTA_TERM_ENTRY_ALIGN - 1u);
}

static bool term_universe_atom_contains_epoch_var(Atom *atom) {
    if (!atom)
        return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return var_epoch_suffix(atom->var_id) != 0;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (term_universe_atom_contains_epoch_var(atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static bool term_universe_atom_is_stable(Atom *atom) {
    if (!atom)
        return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BOOL:
        case GV_STRING:
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        return false;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (!term_universe_atom_is_stable(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static void term_universe_clear_storage(TermUniverse *universe) {
    if (!universe)
        return;
    free(universe->blob_pool);
    free(universe->entries);
    free(universe->intern_slots);
    free(universe->ptr_slots);
    universe->blob_pool = NULL;
    universe->blob_len = 0;
    universe->blob_cap = 0;
    universe->entries = NULL;
    universe->len = 0;
    universe->cap = 0;
    universe->intern_slots = NULL;
    universe->intern_mask = 0;
    universe->intern_used = 0;
    universe->ptr_slots = NULL;
    universe->ptr_mask = 0;
    universe->ptr_used = 0;
}

void term_universe_init(TermUniverse *universe) {
    if (!universe)
        return;
    universe->persistent_arena = NULL;
    universe->blob_pool = NULL;
    universe->blob_len = 0;
    universe->blob_cap = 0;
    universe->entries = NULL;
    universe->len = 0;
    universe->cap = 0;
    universe->intern_slots = NULL;
    universe->intern_mask = 0;
    universe->intern_used = 0;
    universe->ptr_slots = NULL;
    universe->ptr_mask = 0;
    universe->ptr_used = 0;
}

void term_universe_free(TermUniverse *universe) {
    if (!universe)
        return;
    term_universe_clear_storage(universe);
    universe->persistent_arena = NULL;
}

void term_universe_set_persistent_arena(TermUniverse *universe,
                                        Arena *persistent_arena) {
    if (!universe)
        return;
    if (universe->persistent_arena == persistent_arena)
        return;
    term_universe_clear_storage(universe);
    universe->persistent_arena = persistent_arena;
}

typedef struct {
    CettaVarMap *src_to_canonical;
} TermUniverseCanonicalizeCtx;

static Atom *term_universe_create_canonical_var(Arena *dst, Atom *src_var,
                                                uint32_t ordinal, void *ctx) {
    (void)ctx;
    VarId canonical_id = (VarId)ordinal;
    if (canonical_id == VAR_ID_NONE)
        canonical_id = 1u;
    return atom_var_with_spelling(dst, src_var->sym_id, canonical_id);
}

static Atom *term_universe_rewrite_epochless_var(Arena *dst, Atom *src_var,
                                                 void *ctx) {
    TermUniverseCanonicalizeCtx *canon = ctx;
    if (!canon || !canon->src_to_canonical)
        return NULL;
    return cetta_var_map_get_or_add(canon->src_to_canonical, dst, src_var,
                                    term_universe_create_canonical_var, NULL);
}

Atom *term_universe_canonicalize_atom(Arena *dst, Atom *src) {
    if (!term_universe_atom_contains_epoch_var(src))
        return atom_deep_copy(dst, src);
    CettaVarMap src_to_canonical;
    cetta_var_map_init(&src_to_canonical);
    TermUniverseCanonicalizeCtx canon = {
        .src_to_canonical = &src_to_canonical,
    };
    Atom *canonical =
        cetta_atom_rewrite_vars(dst, src,
                                term_universe_rewrite_epochless_var,
                                &canon, true);
    cetta_var_map_free(&src_to_canonical);
    return canonical;
}

static bool term_universe_reserve_entries(TermUniverse *universe,
                                          uint32_t needed) {
    if (!universe)
        return false;
    if (needed <= universe->cap)
        return true;
    uint32_t old_cap = universe->cap;
    uint32_t next_cap = universe->cap ? universe->cap * 2 : 64;
    while (next_cap < needed)
        next_cap *= 2;
    TermEntry *next =
        cetta_realloc(universe->entries, sizeof(TermEntry) * next_cap);
    if (!next)
        return false;
    universe->entries = next;
    for (uint32_t i = old_cap; i < next_cap; i++) {
        universe->entries[i].byte_off = CETTA_TERM_ENTRY_BLOB_NONE;
        universe->entries[i].byte_len = 0;
        universe->entries[i].decoded_cache = NULL;
    }
    universe->cap = next_cap;
    return true;
}

static bool term_universe_blob_reserve(TermUniverse *universe,
                                       uint32_t needed) {
    if (!universe)
        return false;
    if (needed <= universe->blob_cap)
        return true;
    uint32_t next_cap = universe->blob_cap ? universe->blob_cap * 2 : 1024;
    while (next_cap < needed)
        next_cap *= 2;
    uint8_t *next = cetta_realloc(universe->blob_pool, next_cap);
    if (!next)
        return false;
    universe->blob_pool = next;
    universe->blob_cap = next_cap;
    return true;
}

static const TermEntry *term_universe_entry(const TermUniverse *universe,
                                            AtomId id) {
    if (!universe || id >= universe->len)
        return NULL;
    return &universe->entries[id];
}

static bool term_universe_entry_has_blob(const TermEntry *entry) {
    return entry && entry->byte_off != CETTA_TERM_ENTRY_BLOB_NONE &&
           entry->byte_len >= sizeof(CettaTermHdr);
}

static const uint8_t *term_universe_entry_bytes(const TermUniverse *universe,
                                                AtomId id) {
    const TermEntry *entry = term_universe_entry(universe, id);
    if (!term_universe_entry_has_blob(entry))
        return NULL;
    if (!universe->blob_pool || entry->byte_off > universe->blob_len ||
        entry->byte_len > universe->blob_len - entry->byte_off) {
        return NULL;
    }
    return universe->blob_pool + entry->byte_off;
}

const CettaTermHdr *tu_hdr(const TermUniverse *universe, AtomId id) {
    const uint8_t *bytes = term_universe_entry_bytes(universe, id);
    return bytes ? (const CettaTermHdr *)bytes : NULL;
}

static const uint8_t *term_universe_payload(const TermUniverse *universe,
                                            AtomId id) {
    const uint8_t *bytes = term_universe_entry_bytes(universe, id);
    return bytes ? bytes + sizeof(CettaTermHdr) : NULL;
}

static uint64_t term_universe_load_u64(const uint8_t *src) {
    uint64_t value = 0;
    memcpy(&value, src, sizeof(value));
    return value;
}

static int64_t term_universe_load_i64(const uint8_t *src) {
    int64_t value = 0;
    memcpy(&value, src, sizeof(value));
    return value;
}

static double term_universe_load_double(const uint8_t *src) {
    double value = 0.0;
    memcpy(&value, src, sizeof(value));
    return value;
}

static AtomId term_universe_load_atom_id(const uint8_t *src) {
    AtomId value = CETTA_ATOM_ID_NONE;
    memcpy(&value, src, sizeof(value));
    return value;
}

static void term_universe_store_u64(uint8_t *dst, uint64_t value) {
    memcpy(dst, &value, sizeof(value));
}

static void term_universe_store_i64(uint8_t *dst, int64_t value) {
    memcpy(dst, &value, sizeof(value));
}

static void term_universe_store_double(uint8_t *dst, double value) {
    memcpy(dst, &value, sizeof(value));
}

static void term_universe_store_atom_id_bytes(uint8_t *dst, AtomId value) {
    memcpy(dst, &value, sizeof(value));
}

AtomKind tu_kind(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return (AtomKind)hdr->tag;
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache) ? entry->decoded_cache->kind
                                           : ATOM_SYMBOL;
}

uint32_t tu_arity(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return hdr->tag == ATOM_EXPR ? term_universe_aux_data(hdr) : 0u;
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_EXPR)
               ? entry->decoded_cache->expr.len
               : 0u;
}

uint32_t tu_hash32(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return hdr->hash32;
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache) ? atom_hash(entry->decoded_cache) : 0u;
}

SymbolId tu_sym(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_SYMBOL || hdr->tag == ATOM_VAR)
            return hdr->sym_or_head;
        return SYMBOL_ID_NONE;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    if (!entry || !entry->decoded_cache)
        return SYMBOL_ID_NONE;
    return (entry->decoded_cache->kind == ATOM_SYMBOL ||
            entry->decoded_cache->kind == ATOM_VAR)
               ? entry->decoded_cache->sym_id
               : SYMBOL_ID_NONE;
}

VarId tu_var_id(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_VAR)
            return (VarId)term_universe_load_u64(term_universe_payload(universe, id));
        return VAR_ID_NONE;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_VAR)
               ? entry->decoded_cache->var_id
               : VAR_ID_NONE;
}

SymbolId tu_head_sym(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_SYMBOL || hdr->tag == ATOM_EXPR)
            return hdr->sym_or_head;
        return SYMBOL_ID_NONE;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache)
               ? atom_head_symbol_id(entry->decoded_cache)
               : SYMBOL_ID_NONE;
}

GroundedKind tu_ground_kind(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return (GroundedKind)hdr->subtag;
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED)
               ? entry->decoded_cache->ground.gkind
               : GV_FOREIGN;
}

int64_t tu_int(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_INT) {
            return term_universe_load_i64(term_universe_payload(universe, id));
        }
        return 0;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED &&
            entry->decoded_cache->ground.gkind == GV_INT)
               ? entry->decoded_cache->ground.ival
               : 0;
}

double tu_float(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_FLOAT) {
            return term_universe_load_double(term_universe_payload(universe, id));
        }
        return 0.0;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED &&
            entry->decoded_cache->ground.gkind == GV_FLOAT)
               ? entry->decoded_cache->ground.fval
               : 0.0;
}

bool tu_bool(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_BOOL) {
            return term_universe_aux_data(hdr) != 0;
        }
        return false;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return entry && entry->decoded_cache &&
           entry->decoded_cache->kind == ATOM_GROUNDED &&
           entry->decoded_cache->ground.gkind == GV_BOOL &&
           entry->decoded_cache->ground.bval;
}

const char *tu_string_cstr(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_STRING) {
            return (const char *)term_universe_payload(universe, id);
        }
        return NULL;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED &&
            entry->decoded_cache->ground.gkind == GV_STRING)
               ? entry->decoded_cache->ground.sval
               : NULL;
}

AtomId tu_child(const TermUniverse *universe, AtomId id, uint32_t idx) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr || hdr->tag != ATOM_EXPR)
        return CETTA_ATOM_ID_NONE;
    uint32_t len = term_universe_aux_data(hdr);
    if (idx >= len)
        return CETTA_ATOM_ID_NONE;
    return term_universe_load_atom_id(term_universe_payload(universe, id) +
                                      idx * sizeof(AtomId));
}

bool tu_has_vars(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return term_universe_hdr_has_vars(hdr);
    const TermEntry *entry = term_universe_entry(universe, id);
    return entry && atom_has_vars(entry->decoded_cache);
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} TermUniverseStringBuilder;

static void term_universe_sb_reserve(TermUniverseStringBuilder *sb,
                                     size_t needed) {
    if (needed <= sb->cap)
        return;
    size_t next_cap = sb->cap ? sb->cap : 64u;
    while (next_cap < needed)
        next_cap *= 2u;
    sb->buf = cetta_realloc(sb->buf, next_cap);
    sb->cap = next_cap;
}

static void term_universe_sb_append_span(TermUniverseStringBuilder *sb,
                                         const char *text, size_t len) {
    if (!text || len == 0)
        return;
    term_universe_sb_reserve(sb, sb->len + len + 1u);
    memcpy(sb->buf + sb->len, text, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
}

static void term_universe_sb_append_cstr(TermUniverseStringBuilder *sb,
                                         const char *text) {
    term_universe_sb_append_span(sb, text, text ? strlen(text) : 0u);
}

static void term_universe_sb_append_char(TermUniverseStringBuilder *sb,
                                         char ch) {
    term_universe_sb_reserve(sb, sb->len + 2u);
    sb->buf[sb->len++] = ch;
    sb->buf[sb->len] = '\0';
}

static char *term_universe_sb_finish(TermUniverseStringBuilder *sb, Arena *dst) {
    char *out = arena_strdup(dst, sb->buf ? sb->buf : "");
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}

static void term_universe_sb_append_escaped_string(TermUniverseStringBuilder *sb,
                                                   const char *text) {
    term_universe_sb_append_char(sb, '"');
    for (const char *p = text; p && *p; p++) {
        switch (*p) {
        case '\n':
            term_universe_sb_append_cstr(sb, "\\n");
            break;
        case '"':
            term_universe_sb_append_cstr(sb, "\\\"");
            break;
        case '\\':
            term_universe_sb_append_cstr(sb, "\\\\");
            break;
        default:
            term_universe_sb_append_char(sb, *p);
            break;
        }
    }
    term_universe_sb_append_char(sb, '"');
}

static void term_universe_sb_append_atom_text(TermUniverseStringBuilder *sb,
                                              const TermUniverse *universe,
                                              AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr) {
        Atom *atom = term_universe_get_atom(universe, id);
        Arena scratch;
        arena_init(&scratch);
        char *text = atom_to_parseable_string(&scratch, atom);
        term_universe_sb_append_cstr(sb, text);
        arena_free(&scratch);
        return;
    }

    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        term_universe_sb_append_cstr(sb, symbol_bytes(g_symbols, hdr->sym_or_head));
        return;
    case ATOM_VAR: {
        char suffix_buf[16];
        VarId var_id = tu_var_id(universe, id);
        uint32_t epoch = var_epoch_suffix(var_id);
        term_universe_sb_append_char(sb, '$');
        term_universe_sb_append_cstr(sb, symbol_bytes(g_symbols, hdr->sym_or_head));
        if (epoch != 0) {
            int printed = snprintf(suffix_buf, sizeof(suffix_buf), "#%u", epoch);
            if (printed > 0)
                term_universe_sb_append_span(sb, suffix_buf, (size_t)printed);
        }
        return;
    }
    case ATOM_GROUNDED:
        switch ((GroundedKind)hdr->subtag) {
        case GV_INT: {
            char buf[32];
            int printed = snprintf(buf, sizeof(buf), "%" PRId64, tu_int(universe, id));
            if (printed > 0)
                term_universe_sb_append_span(sb, buf, (size_t)printed);
            return;
        }
        case GV_FLOAT: {
            char buf[64];
            double value = tu_float(universe, id);
            int printed = 0;
            if (isnan(value))
                printed = snprintf(buf, sizeof(buf), "NaN");
            else if (isinf(value))
                printed = snprintf(buf, sizeof(buf), "%s", signbit(value) ? "-inf" : "inf");
            else if (isfinite(value) && floor(value) == value)
                printed = snprintf(buf, sizeof(buf), "%.1f", value);
            else
                printed = snprintf(buf, sizeof(buf), "%.16g", value);
            if (printed > 0)
                term_universe_sb_append_span(sb, buf, (size_t)printed);
            return;
        }
        case GV_BOOL:
            term_universe_sb_append_cstr(sb, tu_bool(universe, id) ? "True" : "False");
            return;
        case GV_STRING:
            term_universe_sb_append_escaped_string(sb, tu_string_cstr(universe, id));
            return;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return;
        }
        return;
    case ATOM_EXPR:
        term_universe_sb_append_char(sb, '(');
        for (uint32_t i = 0; i < tu_arity(universe, id); i++) {
            if (i != 0)
                term_universe_sb_append_char(sb, ' ');
            term_universe_sb_append_atom_text(sb, universe, tu_child(universe, id, i));
        }
        term_universe_sb_append_char(sb, ')');
        return;
    }
}

static Atom *term_universe_copy_atom_impl(const TermUniverse *universe,
                                          Arena *dst, AtomId id,
                                          uint32_t epoch,
                                          bool rename_epoch_vars) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!universe || !dst || id == CETTA_ATOM_ID_NONE)
        return NULL;
    if (!hdr) {
        Atom *stored = term_universe_get_atom(universe, id);
        if (!stored)
            return NULL;
        return rename_epoch_vars ? rename_vars(dst, stored, epoch)
                                 : atom_deep_copy(dst, stored);
    }

    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        return atom_symbol_id(dst, hdr->sym_or_head);
    case ATOM_VAR: {
        VarId var_id = tu_var_id(universe, id);
        if (rename_epoch_vars)
            var_id = var_epoch_id(var_id, epoch);
        return atom_var_with_spelling(dst, hdr->sym_or_head, var_id);
    }
    case ATOM_GROUNDED:
        switch (tu_ground_kind(universe, id)) {
        case GV_INT:
            return atom_int(dst, tu_int(universe, id));
        case GV_FLOAT:
            return atom_float(dst, tu_float(universe, id));
        case GV_BOOL:
            return atom_bool(dst, tu_bool(universe, id));
        case GV_STRING:
            return atom_string(dst, tu_string_cstr(universe, id));
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return NULL;
        }
        return NULL;
    case ATOM_EXPR: {
        uint32_t len = tu_arity(universe, id);
        Atom **elems = arena_alloc(dst, sizeof(Atom *) * len);
        for (uint32_t i = 0; i < len; i++) {
            elems[i] = term_universe_copy_atom_impl(universe, dst,
                                                    tu_child(universe, id, i),
                                                    epoch, rename_epoch_vars);
            if (!elems[i])
                return NULL;
        }
        return atom_expr_shared(dst, elems, len);
    }
    }
    return NULL;
}

Atom *term_universe_copy_atom(const TermUniverse *universe, Arena *dst,
                              AtomId id) {
    return term_universe_copy_atom_impl(universe, dst, id, 0u, false);
}

Atom *term_universe_copy_atom_epoch(const TermUniverse *universe, Arena *dst,
                                    AtomId id, uint32_t epoch) {
    return term_universe_copy_atom_impl(universe, dst, id, epoch, true);
}

char *term_universe_atom_to_string(Arena *a, const TermUniverse *universe,
                                   AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!a || !universe || id == CETTA_ATOM_ID_NONE || id >= universe->len)
        return NULL;
    if (!hdr) {
        Atom *atom = term_universe_get_atom(universe, id);
        return atom ? atom_to_string(a, atom) : NULL;
    }
    if (hdr->tag == ATOM_GROUNDED && (GroundedKind)hdr->subtag == GV_STRING)
        return arena_strdup(a, tu_string_cstr(universe, id));

    TermUniverseStringBuilder sb = {0};
    term_universe_sb_append_atom_text(&sb, universe, id);
    return term_universe_sb_finish(&sb, a);
}

char *term_universe_atom_to_parseable_string(Arena *a,
                                             const TermUniverse *universe,
                                             AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!a || !universe || id == CETTA_ATOM_ID_NONE || id >= universe->len)
        return NULL;
    if (!hdr) {
        Atom *atom = term_universe_get_atom(universe, id);
        return atom ? atom_to_parseable_string(a, atom) : NULL;
    }

    TermUniverseStringBuilder sb = {0};
    term_universe_sb_append_atom_text(&sb, universe, id);
    return term_universe_sb_finish(&sb, a);
}

static bool term_universe_intern_reserve(TermUniverse *universe,
                                         uint32_t min_slots) {
    if (!universe)
        return false;
    if (universe->intern_slots && universe->intern_mask + 1 >= min_slots)
        return true;
    uint32_t size = 1024;
    while (size < min_slots)
        size <<= 1;
    uint32_t *next = cetta_malloc(sizeof(uint32_t) * size);
    if (!next)
        return false;
    memset(next, 0, sizeof(uint32_t) * size);
    uint32_t next_mask = size - 1;
    if (universe->intern_slots) {
        for (uint32_t i = 0; i <= universe->intern_mask; i++) {
            uint32_t slot = universe->intern_slots[i];
            if (slot == 0)
                continue;
            AtomId id = slot - 1;
            const CettaTermHdr *hdr = tu_hdr(universe, id);
            uint32_t h = hdr ? hdr->hash32 : 0u;
            for (uint32_t probe = 0; probe < size; probe++) {
                uint32_t idx = (h + probe) & next_mask;
                if (next[idx] == 0) {
                    next[idx] = slot;
                    break;
                }
            }
        }
        free(universe->intern_slots);
    }
    universe->intern_slots = next;
    universe->intern_mask = next_mask;
    return true;
}

static uint32_t term_universe_ptr_hash(Atom *atom) {
    uintptr_t bits = (uintptr_t)atom;
    bits ^= bits >> 17;
    bits *= UINT64_C(0xed5ad4bb);
    bits ^= bits >> 11;
    return (uint32_t)bits;
}

static bool term_universe_ptr_reserve(TermUniverse *universe,
                                      uint32_t min_slots) {
    if (!universe)
        return false;
    if (universe->ptr_slots && universe->ptr_mask + 1 >= min_slots)
        return true;
    uint32_t size = 1024;
    while (size < min_slots)
        size <<= 1;
    uint32_t *next = cetta_malloc(sizeof(uint32_t) * size);
    if (!next)
        return false;
    memset(next, 0, sizeof(uint32_t) * size);
    uint32_t next_mask = size - 1;
    if (universe->ptr_slots) {
        for (uint32_t i = 0; i <= universe->ptr_mask; i++) {
            uint32_t slot = universe->ptr_slots[i];
            if (slot == 0)
                continue;
            AtomId id = slot - 1;
            const TermEntry *entry = term_universe_entry(universe, id);
            Atom *atom = entry ? entry->decoded_cache : NULL;
            if (!atom)
                continue;
            uint32_t h = term_universe_ptr_hash(atom);
            for (uint32_t probe = 0; probe < size; probe++) {
                uint32_t idx = (h + probe) & next_mask;
                if (next[idx] == 0) {
                    next[idx] = slot;
                    break;
                }
            }
        }
        free(universe->ptr_slots);
    }
    universe->ptr_slots = next;
    universe->ptr_mask = next_mask;
    return true;
}

static AtomId term_universe_lookup_ptr_id(const TermUniverse *universe,
                                          Atom *src) {
    if (!universe || !universe->ptr_slots || !src)
        return CETTA_ATOM_ID_NONE;
    uint32_t h = term_universe_ptr_hash(src);
    for (uint32_t probe = 0; probe <= universe->ptr_mask; probe++) {
        uint32_t idx = (h + probe) & universe->ptr_mask;
        uint32_t slot = universe->ptr_slots[idx];
        if (slot == 0)
            return CETTA_ATOM_ID_NONE;
        AtomId id = slot - 1;
        const TermEntry *entry = term_universe_entry(universe, id);
        if (entry && entry->decoded_cache == src)
            return id;
    }
    return CETTA_ATOM_ID_NONE;
}

static bool term_universe_insert_ptr_id(TermUniverse *universe, AtomId id) {
    if (!universe || !universe->ptr_slots || id >= universe->len)
        return false;
    const TermEntry *entry = term_universe_entry(universe, id);
    Atom *atom = entry ? entry->decoded_cache : NULL;
    if (!atom)
        return false;
    uint32_t h = term_universe_ptr_hash(atom);
    for (uint32_t probe = 0; probe <= universe->ptr_mask; probe++) {
        uint32_t idx = (h + probe) & universe->ptr_mask;
        uint32_t slot = universe->ptr_slots[idx];
        if (slot == 0) {
            universe->ptr_slots[idx] = id + 1;
            universe->ptr_used++;
            return true;
        }
        if (slot - 1 == id)
            return true;
    }
    return false;
}

static bool term_universe_entry_eq_atom(const TermUniverse *universe, AtomId id,
                                        Atom *src) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!src)
        return false;
    if (!hdr) {
        const TermEntry *entry = term_universe_entry(universe, id);
        return entry && entry->decoded_cache &&
               atom_eq(entry->decoded_cache, src);
    }

    const uint8_t *payload = term_universe_payload(universe, id);
    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        return src->kind == ATOM_SYMBOL && src->sym_id == hdr->sym_or_head;
    case ATOM_VAR:
        return src->kind == ATOM_VAR &&
               src->var_id == (VarId)term_universe_load_u64(payload);
    case ATOM_GROUNDED:
        if (src->kind != ATOM_GROUNDED ||
            src->ground.gkind != (GroundedKind)hdr->subtag) {
            return false;
        }
        switch ((GroundedKind)hdr->subtag) {
        case GV_INT:
            return src->ground.ival == term_universe_load_i64(payload);
        case GV_FLOAT:
            return src->ground.fval == term_universe_load_double(payload);
        case GV_BOOL:
            return src->ground.bval == (term_universe_aux_data(hdr) != 0);
        case GV_STRING: {
            uint32_t len = term_universe_aux_data(hdr);
            return strlen(src->ground.sval) == len &&
                   memcmp(src->ground.sval, payload, len) == 0;
        }
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        return false;
    case ATOM_EXPR: {
        uint32_t len = term_universe_aux_data(hdr);
        if (src->kind != ATOM_EXPR || src->expr.len != len)
            return false;
        if (atom_head_symbol_id(src) != hdr->sym_or_head)
            return false;
        for (uint32_t i = 0; i < len; i++) {
            AtomId child_id = term_universe_load_atom_id(
                payload + i * sizeof(AtomId));
            if (!term_universe_entry_eq_atom(universe, child_id,
                                             src->expr.elems[i])) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

static AtomId term_universe_lookup_stable_id(const TermUniverse *universe,
                                             Atom *src) {
    if (!universe || !universe->intern_slots || !src)
        return CETTA_ATOM_ID_NONE;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LOOKUP);
    uint32_t h = atom_hash(src);
    for (uint32_t probe = 0; probe <= universe->intern_mask; probe++) {
        uint32_t idx = (h + probe) & universe->intern_mask;
        uint32_t slot = universe->intern_slots[idx];
        if (slot == 0)
            return CETTA_ATOM_ID_NONE;
        AtomId id = slot - 1;
        const CettaTermHdr *hdr = tu_hdr(universe, id);
        if (id < universe->len && hdr && hdr->hash32 == h &&
            term_universe_entry_eq_atom(universe, id, src)) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_HIT);
            return id;
        }
    }
    return CETTA_ATOM_ID_NONE;
}

AtomId term_universe_lookup_atom_id(const TermUniverse *universe, Atom *src) {
    if (!universe || !src)
        return CETTA_ATOM_ID_NONE;
    AtomId existing_ptr = term_universe_lookup_ptr_id(universe, src);
    if (existing_ptr != CETTA_ATOM_ID_NONE)
        return existing_ptr;
    if (!term_universe_atom_is_stable(src))
        return CETTA_ATOM_ID_NONE;
    return term_universe_lookup_stable_id(universe, src);
}

bool term_universe_atom_id_eq(const TermUniverse *universe, AtomId id,
                              Atom *src) {
    if (!universe || id == CETTA_ATOM_ID_NONE || !src)
        return false;
    return term_universe_entry_eq_atom(universe, id, src);
}

static bool term_universe_insert_stable_id(TermUniverse *universe, AtomId id) {
    if (!universe || !universe->intern_slots || id >= universe->len)
        return false;
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr)
        return false;
    uint32_t h = hdr->hash32;
    for (uint32_t probe = 0; probe <= universe->intern_mask; probe++) {
        uint32_t idx = (h + probe) & universe->intern_mask;
        uint32_t slot = universe->intern_slots[idx];
        if (slot == 0) {
            universe->intern_slots[idx] = id + 1;
            universe->intern_used++;
            return true;
        }
        if (slot - 1 == id)
            return true;
    }
    return false;
}

static Atom *term_universe_store_persistent_atom(TermUniverse *universe,
                                                 Atom *src) {
    Arena *dst = universe ? universe->persistent_arena : NULL;
    if (!dst || !src)
        return NULL;
    if (term_universe_atom_contains_epoch_var(src))
        return term_universe_canonicalize_atom(dst, src);
    if (term_universe_atom_is_stable(src))
        return atom_deep_copy_shared(dst, src);
    return atom_deep_copy(dst, src);
}

static bool term_universe_append_record(TermUniverse *universe, Atom *src,
                                        const AtomId *child_ids,
                                        TermEntry *out_entry) {
    if (!universe || !src || !out_entry)
        return false;

    CettaTermHdr hdr = {0};
    uint32_t payload_len = 0;
    hdr.tag = (uint8_t)src->kind;
    hdr.hash32 = atom_hash(src);

    switch (src->kind) {
    case ATOM_SYMBOL:
        hdr.sym_or_head = src->sym_id;
        hdr.aux32 = term_universe_aux_make(0u, false);
        break;
    case ATOM_VAR:
        hdr.sym_or_head = src->sym_id;
        hdr.aux32 = term_universe_aux_make(0u, true);
        payload_len = sizeof(uint64_t);
        break;
    case ATOM_GROUNDED:
        hdr.subtag = (uint8_t)src->ground.gkind;
        switch (src->ground.gkind) {
        case GV_INT:
            payload_len = sizeof(int64_t);
            hdr.aux32 = term_universe_aux_make(0u, false);
            break;
        case GV_FLOAT:
            payload_len = sizeof(double);
            hdr.aux32 = term_universe_aux_make(0u, false);
            break;
        case GV_BOOL:
            hdr.aux32 = term_universe_aux_make(src->ground.bval ? 1u : 0u,
                                               false);
            break;
        case GV_STRING: {
            size_t len_sz = strlen(src->ground.sval);
            if (len_sz > (size_t)UINT32_MAX - 1u)
                return false;
            uint32_t len = (uint32_t)len_sz;
            hdr.arity_or_len = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
            hdr.aux32 = term_universe_aux_make(len, false);
            payload_len = len + 1u;
            break;
        }
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        break;
    case ATOM_EXPR: {
        uint32_t len = src->expr.len;
        if (len > UINT32_MAX / sizeof(AtomId))
            return false;
        hdr.arity_or_len = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
        hdr.sym_or_head = atom_head_symbol_id(src);
        hdr.aux32 = term_universe_aux_make(len, atom_has_vars(src));
        payload_len = len * sizeof(AtomId);
        break;
    }
    }

    if (payload_len > UINT32_MAX - (uint32_t)sizeof(CettaTermHdr))
        return false;
    uint32_t raw_len = sizeof(CettaTermHdr) + payload_len;
    if (raw_len > UINT32_MAX - (CETTA_TERM_ENTRY_ALIGN - 1u))
        return false;
    uint32_t total_len = term_universe_align_up(raw_len);
    uint32_t off = universe->blob_len;
    if (off > UINT32_MAX - total_len)
        return false;
    if (!term_universe_blob_reserve(universe, off + total_len))
        return false;
    memset(universe->blob_pool + off, 0, total_len);
    memcpy(universe->blob_pool + off, &hdr, sizeof(hdr));
    uint8_t *payload = universe->blob_pool + off + sizeof(CettaTermHdr);

    switch (src->kind) {
    case ATOM_SYMBOL:
        break;
    case ATOM_VAR:
        term_universe_store_u64(payload, src->var_id);
        break;
    case ATOM_GROUNDED:
        switch (src->ground.gkind) {
        case GV_INT:
            term_universe_store_i64(payload, src->ground.ival);
            break;
        case GV_FLOAT:
            term_universe_store_double(payload, src->ground.fval);
            break;
        case GV_BOOL:
            break;
        case GV_STRING:
            memcpy(payload, src->ground.sval, payload_len);
            break;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        break;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < src->expr.len; i++) {
            term_universe_store_atom_id_bytes(payload + i * sizeof(AtomId),
                                              child_ids[i]);
        }
        break;
    }

    universe->blob_len += total_len;
    out_entry->byte_off = off;
    out_entry->byte_len = total_len;
    out_entry->decoded_cache = NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BYTE_ENTRY);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BLOB_BYTES,
                            total_len);
    return true;
}

static void term_universe_track_ptr_id(TermUniverse *universe, AtomId id) {
    const TermEntry *entry = term_universe_entry(universe, id);
    Atom *atom = entry ? entry->decoded_cache : NULL;
    if (!atom)
        return;
    if (term_universe_lookup_ptr_id(universe, atom) != CETTA_ATOM_ID_NONE)
        return;
    uint32_t needed = universe->ptr_slots ? (universe->ptr_mask + 1) : 0;
    if (needed == 0 || (universe->ptr_used + 1) * 10 > needed * 7) {
        if (!term_universe_ptr_reserve(universe, needed ? needed * 2 : 1024))
            return;
    }
    (void)term_universe_insert_ptr_id(universe, id);
}

static AtomId term_universe_store_prepared_atom_id(TermUniverse *universe,
                                                   Atom *src) {
    if (!universe || !src)
        return CETTA_ATOM_ID_NONE;

    AtomId existing_ptr = term_universe_lookup_ptr_id(universe, src);
    if (existing_ptr != CETTA_ATOM_ID_NONE)
        return existing_ptr;

    bool stable = term_universe_atom_is_stable(src);
    if (stable) {
        AtomId existing = term_universe_lookup_stable_id(universe, src);
        if (existing != CETTA_ATOM_ID_NONE)
            return existing;
    }

    AtomId *child_ids = NULL;
    if (stable && src->kind == ATOM_EXPR && src->expr.len != 0) {
        child_ids = cetta_malloc(sizeof(AtomId) * src->expr.len);
        if (!child_ids)
            return CETTA_ATOM_ID_NONE;
        for (uint32_t i = 0; i < src->expr.len; i++) {
            child_ids[i] =
                term_universe_store_prepared_atom_id(universe,
                                                     src->expr.elems[i]);
            if (child_ids[i] == CETTA_ATOM_ID_NONE) {
                free(child_ids);
                return CETTA_ATOM_ID_NONE;
            }
        }
    }

    if (!term_universe_reserve_entries(universe, universe->len + 1)) {
        free(child_ids);
        return CETTA_ATOM_ID_NONE;
    }

    TermEntry entry = {
        .byte_off = CETTA_TERM_ENTRY_BLOB_NONE,
        .byte_len = 0,
        .decoded_cache = NULL,
    };
    if (stable) {
        if (!term_universe_append_record(universe, src, child_ids, &entry)) {
            free(child_ids);
            return CETTA_ATOM_ID_NONE;
        }
    } else {
        entry.decoded_cache = term_universe_store_persistent_atom(universe, src);
        if (!entry.decoded_cache) {
            free(child_ids);
            return CETTA_ATOM_ID_NONE;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_FALLBACK_ENTRY);
    }
    free(child_ids);

    AtomId id = universe->len++;
    universe->entries[id] = entry;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT);

    if (stable) {
        uint32_t needed =
            universe->intern_slots ? (universe->intern_mask + 1) : 0;
        if (needed == 0 || (universe->intern_used + 1) * 10 > needed * 7) {
            if (!term_universe_intern_reserve(
                    universe, needed ? needed * 2 : 1024)) {
                return id;
            }
        }
        (void)term_universe_insert_stable_id(universe, id);
    }
    term_universe_track_ptr_id(universe, id);
    return id;
}

AtomId term_universe_store_atom_id(TermUniverse *universe, Arena *fallback,
                                   Atom *src) {
    if (!src)
        return CETTA_ATOM_ID_NONE;
    if (!universe || !universe->persistent_arena) {
        (void)fallback;
        return CETTA_ATOM_ID_NONE;
    }
    (void)fallback;

    Atom *lookup = src;
    Arena canonical_scratch;
    bool have_scratch = false;
    if (term_universe_atom_contains_epoch_var(src)) {
        arena_init(&canonical_scratch);
        arena_set_hashcons(&canonical_scratch, NULL);
        lookup = term_universe_canonicalize_atom(&canonical_scratch, src);
        have_scratch = true;
    }

    AtomId id = term_universe_store_prepared_atom_id(universe, lookup);
    if (have_scratch)
        arena_free(&canonical_scratch);
    return id;
}

static Atom *term_universe_decode_atom(TermUniverse *universe, AtomId id) {
    Arena *dst = universe ? universe->persistent_arena : NULL;
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    const uint8_t *payload = term_universe_payload(universe, id);
    if (!hdr || !payload || !dst)
        return NULL;

    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        return atom_symbol_id(dst, hdr->sym_or_head);
    case ATOM_VAR:
        return atom_var_with_spelling(dst, hdr->sym_or_head,
                                      (VarId)term_universe_load_u64(payload));
    case ATOM_GROUNDED:
        switch (tu_ground_kind(universe, id)) {
        case GV_INT:
            return atom_int(dst, term_universe_load_i64(payload));
        case GV_FLOAT:
            return atom_float(dst, term_universe_load_double(payload));
        case GV_BOOL:
            return atom_bool(dst, term_universe_aux_data(hdr) != 0);
        case GV_STRING:
            return atom_string(dst, (const char *)payload);
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return NULL;
        }
        return NULL;
    case ATOM_EXPR: {
        uint32_t len = term_universe_aux_data(hdr);
        Atom **elems = arena_alloc(dst, sizeof(Atom *) * len);
        for (uint32_t i = 0; i < len; i++) {
            AtomId child_id =
                term_universe_load_atom_id(payload + i * sizeof(AtomId));
            elems[i] = term_universe_get_atom(universe, child_id);
            if (!elems[i])
                return NULL;
        }
        return atom_expr_shared(dst, elems, len);
    }
    }
    return NULL;
}

Atom *term_universe_get_atom(const TermUniverse *universe, AtomId id) {
    if (!universe || id == CETTA_ATOM_ID_NONE || id >= universe->len)
        return NULL;
    TermUniverse *mutable_universe = (TermUniverse *)universe;
    TermEntry *entry = &mutable_universe->entries[id];
    if (entry->decoded_cache)
        return entry->decoded_cache;
    if (!term_universe_entry_has_blob(entry))
        return NULL;
    entry->decoded_cache = term_universe_decode_atom(mutable_universe, id);
    if (entry->decoded_cache)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE);
    term_universe_track_ptr_id(mutable_universe, id);
    return entry->decoded_cache;
}

Atom *term_universe_store_atom(TermUniverse *universe, Arena *fallback,
                               Atom *src) {
    Arena *persistent = universe ? universe->persistent_arena : NULL;
    if (!src)
        return NULL;
    if (!persistent)
        return fallback ? atom_deep_copy(fallback, src) : NULL;
    AtomId id = term_universe_store_atom_id(universe, fallback, src);
    Atom *stored = term_universe_get_atom(universe, id);
    return stored ? stored : term_universe_store_persistent_atom(universe, src);
}

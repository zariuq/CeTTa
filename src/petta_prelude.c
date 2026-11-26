#include "petta_prelude.h"

#include "match.h"
#include "petta_numeric.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CETTA_BUILD_WITH_GMP
#include <gmp.h>
#endif

/* ── Errors ─────────────────────────────────────────────────────────── */

/* A Prolog compound Name(A) or Name(A, B), as PeTTa reads SWI's terms: an
 * expression in a compound carrier, never a list (car-atom of an error's
 * formal term answers as for any non-list). */
static Atom *prelude_term(Arena *arena, const char *name, Atom *a, Atom *b) {
    Atom *head = atom_symbol(arena, name);
    Atom *body = b ? atom_expr3(arena, head, a, b) : atom_expr2(arena, head, a);
    return body ? atom_petta_prolog_compound(arena, body) : NULL;
}


/* error(Formal, context([system:]Name/Arity, _)). */
static Atom *prelude_error(Arena *arena, Atom *formal, bool system,
                           const char *name, int64_t arity) {
    Atom *indicator = prelude_term(arena, "/",
                                   atom_symbol(arena, name),
                                   atom_int(arena, arity));
    if (system)
        indicator = prelude_term(arena, ":",
                                 atom_symbol(arena, "system"), indicator);
    Atom *context = prelude_term(arena, "context",
                                 indicator,
                                 atom_var_with_id(arena, "_", fresh_var_id()));
    return atom_error(arena, formal, context);
}

static Atom *prelude_type_error(Arena *arena, const char *type, Atom *culprit,
                                bool system, const char *name,
                                int64_t arity) {
    Atom *formal = prelude_term(arena, "type_error",
                                atom_symbol(arena, type), culprit);
    return prelude_error(arena, formal, system, name, arity);
}

static Atom *prelude_domain_error(Arena *arena, const char *domain,
                                  Atom *culprit, bool system, const char *name,
                                  int64_t arity) {
    Atom *formal = prelude_term(arena, "domain_error",
                                atom_symbol(arena, domain), culprit);
    return prelude_error(arena, formal, system, name, arity);
}

static Atom *prelude_representation_error(Arena *arena, const char *limit,
                                          bool system, const char *name,
                                          int64_t arity) {
    Atom *formal = prelude_term(arena, "representation_error",
                                atom_symbol(arena, limit), NULL);
    return prelude_error(arena, formal, system, name, arity);
}

static Atom *prelude_instantiation_error(Arena *arena, const char *name,
                                         int64_t arity) {
    return prelude_error(arena, atom_symbol(arena, "instantiation_error"),
                         true, name, arity);
}

/* ── Text of an atomic term ─────────────────────────────────────────── */

/* The text SWI's text predicates read from an atomic MeTTa value: a
 * symbol's name, a string's characters, a number's written form.  NULL for
 * a variable or a compound. */
static const char *prelude_atomic_text(Arena *arena, Atom *atom,
                                       size_t *length) {
    if (atom->kind == ATOM_SYMBOL) {
        *length = symbol_len(g_symbols, atom->sym_id);
        return symbol_bytes(g_symbols, atom->sym_id);
    }
    if (atom->kind != ATOM_GROUNDED)
        return NULL;
    switch (atom->ground.gkind) {
    case GV_STRING:
        *length = atom->ground.slen;
        return atom->ground.sval;
    case GV_BOOL:
        *length = atom->ground.bval ? 4u : 5u;
        return atom->ground.bval ? "true" : "false";
    case GV_INT:
    case GV_BIGINT:
    case GV_FLOAT:
    case GV_RATIONAL: {
        char *text = cetta_petta_number_to_string(arena, atom);
        if (text)
            *length = strlen(text);
        return text;
    }
    case GV_SPACE: {
        CettaGeneratedName name = {0};
        if (!space_generated_name ||
            !space_generated_name(atom->ground.ptr, &name))
            return NULL;
        char buf[CETTA_GENERATED_NAME_SPELLING_MAX];
        size_t written = cetta_generated_name_spell(
            CETTA_NAME_KIND_SPACE, name, buf, sizeof(buf));
        if (written == 0u)
            return NULL;
        char *text = arena_alloc(arena, written + 1u);
        if (!text)
            return NULL;
        memcpy(text, buf, written + 1u);
        *length = written;
        return text;
    }
    default:
        return NULL;
    }
}

static Atom *prelude_symbol_of(Arena *arena, const char *bytes,
                               size_t length) {
    if (length > UINT32_MAX)
        return NULL;
    SymbolId id = symbol_intern_bytes(g_symbols, (const uint8_t *)bytes,
                                      (uint32_t)length);
    return id == SYMBOL_ID_NONE ? NULL : atom_symbol_id(arena, id);
}

/* atom_concat(+A, +B, -R). */
static Atom *prelude_atom_concat(Arena *arena, Atom *a, Atom *b,
                                 bool *raised) {
    Atom *parts[2] = {a, b};
    const char *text[2];
    size_t length[2];
    for (int k = 0; k < 2; k++) {
        if (parts[k]->kind == ATOM_VAR) {
            *raised = true;
            return prelude_instantiation_error(arena, "atom_concat", 3);
        }
        text[k] = prelude_atomic_text(arena, parts[k], &length[k]);
        if (!text[k]) {
            *raised = true;
            return prelude_type_error(arena, "atomic", parts[k], true,
                                      "atom_concat", 3);
        }
    }
    char *joined = arena_alloc(arena, length[0] + length[1] + 1u);
    if (!joined)
        return NULL;
    memcpy(joined, text[0], length[0]);
    memcpy(joined + length[0], text[1], length[1]);
    joined[length[0] + length[1]] = '\0';
    return prelude_symbol_of(arena, joined, length[0] + length[1]);
}

/* The length of the UTF-8 sequence starting with `lead`. */
static size_t prelude_utf8_width(unsigned char lead) {
    if (lead < 0x80u) return 1u;
    if ((lead & 0xe0u) == 0xc0u) return 2u;
    if ((lead & 0xf0u) == 0xe0u) return 3u;
    if ((lead & 0xf8u) == 0xf0u) return 4u;
    return 1u;
}

/* atom_chars(+A, -Chars). */
static Atom *prelude_atom_chars(Arena *arena, Atom *a, bool *raised) {
    if (a->kind == ATOM_VAR) {
        *raised = true;
        return prelude_instantiation_error(arena, "atom_chars", 2);
    }
    size_t length = 0u;
    const char *text = prelude_atomic_text(arena, a, &length);
    if (!text) {
        *raised = true;
        return prelude_type_error(arena, "atom", a, false, "atom_chars", 2);
    }
    Atom **chars = length
        ? arena_alloc(arena, sizeof(*chars) * length) : NULL;
    if (length && !chars)
        return NULL;
    CettaExprLen count = 0u;
    for (size_t at = 0u; at < length;) {
        size_t width = prelude_utf8_width((unsigned char)text[at]);
        if (at + width > length)
            width = length - at;
        chars[count] = prelude_symbol_of(arena, text + at, width);
        if (!chars[count])
            return NULL;
        count++;
        at += width;
    }
    return atom_expr(arena, chars, count);
}

/* ── term_hash/2 ────────────────────────────────────────────────────────
 * SWI's term_hash/2 (pl-termhash.c): MurmurHashAligned2 with SWI's seed
 * over each primitive's representation, a compound's functor folded first
 * and each completed argument's hash after it.  metta.pl passes an
 * expression as a Prolog list, '[|]'(Head, Tail) ending in []. */

#define PRELUDE_MURMUR_SEED 0x1a3be34au

static uint32_t prelude_murmur(const void *key, size_t len, uint32_t seed) {
    const uint32_t m = 0x5bd1e995u;
    const unsigned char *data = key;
    uint32_t h = seed ^ (uint32_t)len;
    while (len >= 4u) {
        uint32_t k = (uint32_t)data[0] | (uint32_t)data[1] << 8 |
                     (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
        k *= m;
        k ^= k >> 24;
        k *= m;
        h *= m;
        h ^= k;
        data += 4;
        len -= 4u;
    }
    switch (len) {
    case 3: h ^= (uint32_t)data[2] << 16; /* fall through */
    case 2: h ^= (uint32_t)data[1] << 8;  /* fall through */
    case 1: h ^= data[0]; h *= m;
    default: break;
    }
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h;
}

/* The codepoints of UTF-8 text; false on a malformed sequence. */
static bool prelude_codepoints(const char *text, size_t length,
                               uint32_t **out, size_t *count) {
    uint32_t *points = malloc(sizeof(*points) * (length ? length : 1u));
    if (!points)
        return false;
    size_t n = 0u;
    for (size_t at = 0u; at < length;) {
        unsigned char lead = (unsigned char)text[at];
        size_t width = prelude_utf8_width(lead);
        if (at + width > length) {
            free(points);
            return false;
        }
        uint32_t point = width == 1u ? lead
            : width == 2u ? lead & 0x1fu
            : width == 3u ? lead & 0x0fu : lead & 0x07u;
        for (size_t k = 1u; k < width; k++)
            point = point << 6 | ((unsigned char)text[at + k] & 0x3fu);
        points[n++] = point;
        at += width;
    }
    *out = points;
    *count = n;
    return true;
}

/* SWI keeps text whose characters all fit a byte as ISO Latin-1 and other
 * text wide.  `wide_utf8` gives wide text as UTF-8 (strings) rather than as
 * 32-bit characters (atoms). */
static bool prelude_text_hash(const char *text, size_t length, bool wide_utf8,
                              uint32_t seed, uint32_t *hash) {
    uint32_t *points = NULL;
    size_t count = 0u;
    if (!prelude_codepoints(text, length, &points, &count))
        return false;
    bool narrow = true;
    for (size_t k = 0u; k < count && narrow; k++)
        narrow = points[k] <= 0xffu;
    if (narrow) {
        unsigned char *bytes = malloc(count ? count : 1u);
        if (!bytes) {
            free(points);
            return false;
        }
        for (size_t k = 0u; k < count; k++)
            bytes[k] = (unsigned char)points[k];
        *hash = prelude_murmur(bytes, count, seed);
        free(bytes);
    } else if (wide_utf8) {
        *hash = prelude_murmur(text, length, seed);
    } else {
        unsigned char *bytes = malloc(count * 4u);
        if (!bytes) {
            free(points);
            return false;
        }
        for (size_t k = 0u; k < count; k++) {
            bytes[4u * k] = (unsigned char)points[k];
            bytes[4u * k + 1u] = (unsigned char)(points[k] >> 8);
            bytes[4u * k + 2u] = (unsigned char)(points[k] >> 16);
            bytes[4u * k + 3u] = (unsigned char)(points[k] >> 24);
        }
        *hash = prelude_murmur(bytes, count * 4u, seed);
        free(bytes);
    }
    free(points);
    return true;
}

/* An atom's own hash, which SWI folds as four little-endian bytes. */
static bool prelude_fold_atom(const char *text, size_t length, uint32_t *h) {
    uint32_t atom_hash = 0u;
    if (!prelude_text_hash(text, length, false, PRELUDE_MURMUR_SEED,
                           &atom_hash))
        return false;
    unsigned char bytes[4] = {
        (unsigned char)atom_hash, (unsigned char)(atom_hash >> 8),
        (unsigned char)(atom_hash >> 16), (unsigned char)(atom_hash >> 24),
    };
    *h = prelude_murmur(bytes, 4u, *h);
    return true;
}

static void prelude_fold_word(uint64_t word, uint32_t *h) {
    unsigned char bytes[8];
    for (int k = 0; k < 8; k++)
        bytes[k] = (unsigned char)(word >> (8 * k));
    *h = prelude_murmur(bytes, 8u, *h);
}

static void prelude_fold_hash(uint32_t child, uint32_t *h) {
    unsigned char bytes[4] = {
        (unsigned char)child, (unsigned char)(child >> 8),
        (unsigned char)(child >> 16), (unsigned char)(child >> 24),
    };
    *h = prelude_murmur(bytes, 4u, *h);
}

#if CETTA_BUILD_WITH_GMP
#define PRELUDE_MAX_TAGGED ((INT64_C(1) << 56) - 1)

/* A big integer as SWI keeps it on its stacks and hashes it: the limb
 * count, shifted left over the sign, then the limbs, as one run of
 * words. */
static bool prelude_fold_mpz(const mpz_t value, uint32_t *h) {
    size_t limbs = mpz_size(value);
    int64_t size = mpz_sgn(value) < 0 ? -(int64_t)limbs : (int64_t)limbs;
    size_t words = 1u + limbs;
    unsigned char *bytes = malloc(words * 8u);
    if (!bytes)
        return false;
    uint64_t header = ((uint64_t)size << 1) & ~UINT64_C(1);
    for (int k = 0; k < 8; k++)
        bytes[k] = (unsigned char)(header >> (8 * k));
    for (size_t l = 0u; l < limbs; l++) {
        uint64_t limb = (uint64_t)mpz_getlimbn(value, (mp_size_t)l);
        for (int k = 0; k < 8; k++)
            bytes[8u * (l + 1u) + (size_t)k] = (unsigned char)(limb >> (8 * k));
    }
    *h = prelude_murmur(bytes, words * 8u, *h);
    free(bytes);
    return true;
}
#endif

typedef enum { PRELUDE_HASHED, PRELUDE_NONGROUND, PRELUDE_UNHASHABLE }
    PreludeHashStatus;

static PreludeHashStatus prelude_list_hash(Atom *list, uint32_t *out);

/* Fold the primitive `atom` into *h, or the hash of the compound it is. */
static PreludeHashStatus prelude_fold_term(Atom *atom, uint32_t *h) {
    switch (atom->kind) {
    case ATOM_VAR:
        return PRELUDE_NONGROUND;
    case ATOM_SYMBOL: {
        size_t length = symbol_len(g_symbols, atom->sym_id);
        return prelude_fold_atom(symbol_bytes(g_symbols, atom->sym_id),
                                 length, h)
            ? PRELUDE_HASHED : PRELUDE_UNHASHABLE;
    }
    case ATOM_EXPR: {
        if (atom->expr.len == 0u)
            return prelude_fold_atom("[]", 2u, h)
                ? PRELUDE_HASHED : PRELUDE_UNHASHABLE;
        uint32_t child = 0u;
        PreludeHashStatus status = prelude_list_hash(atom, &child);
        if (status == PRELUDE_HASHED)
            prelude_fold_hash(child, h);
        return status;
    }
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
#if CETTA_BUILD_WITH_GMP
            /* SWI tags integers of 57 bits; a wider one is a big integer
             * on its stacks, and hashes as one. */
            if (atom->ground.ival > PRELUDE_MAX_TAGGED ||
                atom->ground.ival < -PRELUDE_MAX_TAGGED - 1) {
                mpz_t value;
                mpz_init_set_si(value, (long)atom->ground.ival);
                bool ok = prelude_fold_mpz(value, h);
                mpz_clear(value);
                return ok ? PRELUDE_HASHED : PRELUDE_UNHASHABLE;
            }
#endif
            prelude_fold_word((uint64_t)atom->ground.ival, h);
            return PRELUDE_HASHED;
        case GV_FLOAT: {
            uint64_t word = 0u;
            memcpy(&word, &atom->ground.fval, sizeof(word));
            prelude_fold_word(word, h);
            return PRELUDE_HASHED;
        }
        case GV_BOOL:
            return prelude_fold_atom(atom->ground.bval ? "true" : "false",
                                     atom->ground.bval ? 4u : 5u, h)
                ? PRELUDE_HASHED : PRELUDE_UNHASHABLE;
        case GV_STRING: {
            uint32_t value = 0u;
            if (!prelude_text_hash(atom->ground.sval, atom->ground.slen,
                                   true, *h, &value))
                return PRELUDE_UNHASHABLE;
            *h = value;
            return PRELUDE_HASHED;
        }
        case GV_SPACE: {
            CettaGeneratedName name = {0};
            if (!space_generated_name ||
                !space_generated_name(atom->ground.ptr, &name))
                return PRELUDE_UNHASHABLE;
            char buf[CETTA_GENERATED_NAME_SPELLING_MAX];
            size_t written = cetta_generated_name_spell(
                CETTA_NAME_KIND_SPACE, name, buf, sizeof(buf));
            if (written == 0u)
                return PRELUDE_UNHASHABLE;
            return prelude_fold_atom(buf, written, h)
                ? PRELUDE_HASHED : PRELUDE_UNHASHABLE;
        }
#if CETTA_BUILD_WITH_GMP
        case GV_BIGINT: {
            mpz_t value;
            mpz_init(value);
            bool ok = atom_bigint_get_mpz(atom, value);
            if (ok)
                ok = prelude_fold_mpz(value, h);
            mpz_clear(value);
            return ok ? PRELUDE_HASHED : PRELUDE_UNHASHABLE;
        }
#endif
        default:
            return PRELUDE_UNHASHABLE;
        }
    default:
        return PRELUDE_UNHASHABLE;
    }
}

/* The hash of the list of `list`'s elements: each cell '[|]'(E, Rest),
 * computed from the last cell back. */
static PreludeHashStatus prelude_list_hash(Atom *list, uint32_t *out) {
    CettaExprLen length = list->expr.len;
    uint32_t rest = 0u;
    for (CettaExprLen index = length; index > 0u; index--) {
        uint32_t h = PRELUDE_MURMUR_SEED;
        if (!prelude_fold_atom("[|]", 3u, &h))
            return PRELUDE_UNHASHABLE;
        PreludeHashStatus status =
            prelude_fold_term(list->expr.elems[index - 1u], &h);
        if (status != PRELUDE_HASHED)
            return status;
        if (index == length) {
            if (!prelude_fold_atom("[]", 2u, &h))
                return PRELUDE_UNHASHABLE;
        } else {
            prelude_fold_hash(rest, &h);
        }
        rest = h;
    }
    *out = rest;
    return PRELUDE_HASHED;
}

static Atom *prelude_term_hash(Arena *arena, Atom *term) {
    uint32_t hash = 0u;
    PreludeHashStatus status;
    if (term->kind == ATOM_EXPR && term->expr.len > 0u) {
        status = prelude_list_hash(term, &hash);
    } else {
        hash = PRELUDE_MURMUR_SEED;
        status = prelude_fold_term(term, &hash);
    }
    if (status == PRELUDE_NONGROUND)
        return atom_var_with_id(arena, "_", fresh_var_id());
    if (status != PRELUDE_HASHED)
        return NULL;
    return atom_int(arena, (int64_t)hash);
}

/* Read an integer operand of term_hash/4, preserving SWI's error taxonomy
 * (instantiation_error, type_error(integer, _), representation_error(_)). */
static bool prelude_hash4_read_int64(Arena *arena, Atom *atom, bool as_int32,
                                     int64_t *value_out, Atom **error_out) {
    if (atom->kind == ATOM_VAR) {
        *error_out = prelude_instantiation_error(arena, "term_hash", 4);
        return false;
    }
    if (atom->kind != ATOM_GROUNDED) {
        *error_out = prelude_type_error(arena, "integer", atom, true,
                                        "term_hash", 4);
        return false;
    }
    if (atom->ground.gkind == GV_INT) {
        int64_t v = atom->ground.ival;
        if (as_int32 && (v < INT_MIN || v > INT_MAX)) {
            *error_out = prelude_representation_error(arena, "int", true,
                                                      "term_hash", 4);
            return false;
        }
        *value_out = v;
        return true;
    }
    if (atom->ground.gkind == GV_BIGINT) {
#if CETTA_BUILD_WITH_GMP
        mpz_t value;
        mpz_init(value);
        bool ok = atom_bigint_get_mpz(atom, value);
        if (ok) {
            if (as_int32) {
                if (mpz_fits_sint_p(value)) {
                    *value_out = (int64_t)mpz_get_si(value);
                    mpz_clear(value);
                    return true;
                }
                mpz_clear(value);
                *error_out = prelude_representation_error(arena, "int", true,
                                                          "term_hash", 4);
                return false;
            }
            if (mpz_fits_slong_p(value)) {
                *value_out = (int64_t)mpz_get_si(value);
                mpz_clear(value);
                return true;
            }
            mpz_clear(value);
            *error_out = prelude_representation_error(arena, "int64_t", true,
                                                      "term_hash", 4);
            return false;
        }
        mpz_clear(value);
#endif
        const char *text = atom_bigint_cstr(atom);
        if (text) {
            errno = 0;
            char *end = NULL;
            long long parsed = strtoll(text, &end, 10);
            if (errno == 0 && end && *end == '\0') {
                if (as_int32 && (parsed < INT_MIN || parsed > INT_MAX)) {
                    *error_out = prelude_representation_error(arena, "int", true,
                                                              "term_hash", 4);
                    return false;
                }
                *value_out = (int64_t)parsed;
                return true;
            }
        }
        *error_out = prelude_representation_error(
            arena, as_int32 ? "int" : "int64_t", true, "term_hash", 4);
        return false;
    }
    *error_out = prelude_type_error(arena, "integer", atom, true,
                                    "term_hash", 4);
    return false;
}

typedef struct {
    Atom *term;
    CettaExprIndex index;
    int64_t depth;
    bool list_slice;
} PreludeHash4Frame;

static bool prelude_hash4_push(PreludeHash4Frame **stack, size_t *len,
                               size_t *cap, PreludeHash4Frame *inline_buf,
                               PreludeHash4Frame frame) {
    if (*len == *cap) {
        size_t next = *cap * 2u;
        if (next <= *cap || next > SIZE_MAX / sizeof(PreludeHash4Frame))
            return false;
        PreludeHash4Frame *grown = (*stack == inline_buf)
            ? malloc(next * sizeof(*grown))
            : realloc(*stack, next * sizeof(*grown));
        if (!grown)
            return false;
        if (*stack == inline_buf)
            memcpy(grown, inline_buf, *len * sizeof(*grown));
        *stack = grown;
        *cap = next;
    }
    (*stack)[(*len)++] = frame;
    return true;
}

/* SWI's term_hash/4 (pl-prims.c): single MurmurHashAligned2 accumulator
 * initialized to MURMUR_SEED.  Compounds fold (uint64_t)atom_hash + arity
 * (8 little-endian bytes) before recursing up to `depth` levels (-1 = full).
 * Wide UTF-8 strings hash safely as UTF-8 bytes rather than dereferencing
 * a narrow-only buffer. */
static Atom *prelude_term_hash_bounded(Arena *arena, Atom *term,
                                       Atom *depth_atom, Atom *range_atom,
                                       bool *raised) {
    int64_t depth = 0;
    Atom *err = NULL;
    if (!prelude_hash4_read_int64(arena, depth_atom, false, &depth, &err)) {
        *raised = true;
        return err;
    }
    if (depth < -1) {
        *raised = true;
        return prelude_domain_error(arena, "not_less_than_zero", depth_atom,
                                    true, "term_hash", 4);
    }
    int64_t range = 0;
    if (!prelude_hash4_read_int64(arena, range_atom, true, &range, &err)) {
        *raised = true;
        return err;
    }
    if (range < 1) {
        *raised = true;
        return prelude_domain_error(arena, "not_less_than_one", range_atom,
                                    true, "term_hash", 4);
    }

    uint32_t h = PRELUDE_MURMUR_SEED;
    if (depth == 0)
        return atom_int(arena, (int64_t)(h % (uint32_t)range));

    uint32_t cons_atom_hash = prelude_murmur("[|]", 3u, PRELUDE_MURMUR_SEED);
    uint64_t cons_functor_key = (uint64_t)cons_atom_hash + UINT64_C(2);

    PreludeHash4Frame inline_stack[64];
    PreludeHash4Frame *stack = inline_stack;
    size_t stack_len = 0u;
    size_t stack_cap = sizeof(inline_stack) / sizeof(inline_stack[0]);
    stack[stack_len++] = (PreludeHash4Frame){
        .term = term, .index = 0u, .depth = depth, .list_slice = false,
    };

    PreludeHashStatus status = PRELUDE_HASHED;
    while (status == PRELUDE_HASHED && stack_len > 0u) {
        PreludeHash4Frame cur = stack[--stack_len];
        if (cur.list_slice) {
            if (cur.index >= cur.term->expr.len) {
                if (!prelude_fold_atom("[]", 2u, &h))
                    status = PRELUDE_UNHASHABLE;
                continue;
            }
            prelude_fold_word(cons_functor_key, &h);
            int64_t next_depth = cur.depth > 0 ? cur.depth - 1 : -1;
            if (next_depth != 0) {
                if (!prelude_hash4_push(
                        &stack, &stack_len, &stack_cap, inline_stack,
                        (PreludeHash4Frame){.term = cur.term,
                                            .index = cur.index + 1u,
                                            .depth = next_depth,
                                            .list_slice = true}) ||
                    !prelude_hash4_push(
                        &stack, &stack_len, &stack_cap, inline_stack,
                        (PreludeHash4Frame){
                            .term = cur.term->expr.elems[cur.index],
                            .index = 0u,
                            .depth = next_depth,
                            .list_slice = false})) {
                    status = PRELUDE_UNHASHABLE;
                }
            }
            continue;
        }

        Atom *atom = cur.term;
        if (atom->kind != ATOM_EXPR) {
            status = prelude_fold_term(atom, &h);
            continue;
        }

        Atom *compound_body = NULL;
        if (atom_petta_prolog_compound_body(atom, &compound_body) &&
            compound_body && compound_body->kind == ATOM_EXPR &&
            compound_body->expr.len >= 2u &&
            compound_body->expr.elems[0]->kind == ATOM_SYMBOL) {
            SymbolId fname = compound_body->expr.elems[0]->sym_id;
            uint32_t name_hash = 0u;
            if (!prelude_text_hash(symbol_bytes(g_symbols, fname),
                                   symbol_len(g_symbols, fname), false,
                                   PRELUDE_MURMUR_SEED, &name_hash)) {
                status = PRELUDE_UNHASHABLE;
                continue;
            }
            uint64_t arity = (uint64_t)(compound_body->expr.len - 1u);
            prelude_fold_word((uint64_t)name_hash + arity, &h);
            int64_t next_depth = cur.depth > 0 ? cur.depth - 1 : -1;
            if (next_depth != 0) {
                for (CettaExprIndex k = compound_body->expr.len; k > 1u; k--) {
                    if (!prelude_hash4_push(
                            &stack, &stack_len, &stack_cap, inline_stack,
                            (PreludeHash4Frame){
                                .term = compound_body->expr.elems[k - 1u],
                                .index = 0u,
                                .depth = next_depth,
                                .list_slice = false})) {
                        status = PRELUDE_UNHASHABLE;
                        break;
                    }
                }
            }
            continue;
        }

        if (atom->expr.len == 3u &&
            atom_is_internal_tag(atom->expr.elems[0],
                                 CETTA_INTERNAL_TAG_PETTA_OPEN_CONS)) {
            prelude_fold_word(cons_functor_key, &h);
            int64_t next_depth = cur.depth > 0 ? cur.depth - 1 : -1;
            if (next_depth != 0) {
                if (!prelude_hash4_push(
                        &stack, &stack_len, &stack_cap, inline_stack,
                        (PreludeHash4Frame){.term = atom->expr.elems[2],
                                            .index = 0u,
                                            .depth = next_depth,
                                            .list_slice = false}) ||
                    !prelude_hash4_push(
                        &stack, &stack_len, &stack_cap, inline_stack,
                        (PreludeHash4Frame){.term = atom->expr.elems[1],
                                            .index = 0u,
                                            .depth = next_depth,
                                            .list_slice = false})) {
                    status = PRELUDE_UNHASHABLE;
                }
            }
            continue;
        }

        CettaExprIndex base = atom_is_list(atom) ? 1u : 0u;
        if (atom->expr.len <= base) {
            if (!prelude_fold_atom("[]", 2u, &h))
                status = PRELUDE_UNHASHABLE;
            continue;
        }
        prelude_fold_word(cons_functor_key, &h);
        int64_t next_depth = cur.depth > 0 ? cur.depth - 1 : -1;
        if (next_depth != 0) {
            if (!prelude_hash4_push(
                    &stack, &stack_len, &stack_cap, inline_stack,
                    (PreludeHash4Frame){.term = atom,
                                        .index = base + 1u,
                                        .depth = next_depth,
                                        .list_slice = true}) ||
                !prelude_hash4_push(
                    &stack, &stack_len, &stack_cap, inline_stack,
                    (PreludeHash4Frame){.term = atom->expr.elems[base],
                                        .index = 0u,
                                        .depth = next_depth,
                                        .list_slice = false})) {
                status = PRELUDE_UNHASHABLE;
            }
        }
    }

    if (stack != inline_stack)
        free(stack);
    if (status == PRELUDE_NONGROUND)
        return atom_var_with_id(arena, "_", fresh_var_id());
    if (status != PRELUDE_HASHED)
        return NULL;
    return atom_int(arena, (int64_t)(h % (uint32_t)range));
}

/* ── =?/2 ───────────────────────────────────────────────────────────────
 * \+ \+ A = B: whether A and B unify, with SWI's unification, which has
 * no occurs check, so a variable may meet a term containing it (a rational
 * tree).  Each pair of compound terms is unified once: a pair met again
 * through a cycle is already being made equal. */

typedef struct {
    VarId var;
    Atom *value;
} PreludeBinding;

typedef struct {
    Atom *left;
    Atom *right;
} PreludePair;

typedef struct {
    PreludeBinding *bindings;
    size_t binding_len, binding_cap;
    PreludePair *stack;
    size_t stack_len, stack_cap;
    PreludePair *seen;
    size_t seen_len, seen_cap;
} PreludeUnifier;

static bool prelude_grow(void **items, size_t *cap, size_t len, size_t size) {
    if (len < *cap)
        return true;
    size_t next = *cap ? *cap * 2u : 16u;
    void *grown = cetta_realloc(*items, next * size);
    if (!grown)
        return false;
    *items = grown;
    *cap = next;
    return true;
}

static Atom *prelude_deref(PreludeUnifier *u, Atom *atom) {
    while (atom && atom->kind == ATOM_VAR) {
        Atom *value = NULL;
        for (size_t k = u->binding_len; k > 0u && !value; k--)
            if (var_id_eq(u->bindings[k - 1u].var, atom->var_id))
                value = u->bindings[k - 1u].value;
        if (!value)
            return atom;
        atom = value;
    }
    return atom;
}

static bool prelude_push_pair(PreludeUnifier *u, Atom *left, Atom *right) {
    if (!prelude_grow((void **)&u->stack, &u->stack_cap, u->stack_len,
                      sizeof(*u->stack)))
        return false;
    u->stack[u->stack_len++] = (PreludePair){left, right};
    return true;
}

/* 1 when A and B unify, 0 when they do not, -1 when memory runs out. */
static int prelude_unifiable(Atom *a, Atom *b) {
    PreludeUnifier u = {0};
    int verdict = prelude_push_pair(&u, a, b) ? 1 : -1;
    while (verdict == 1 && u.stack_len > 0u) {
        PreludePair pair = u.stack[--u.stack_len];
        Atom *left = prelude_deref(&u, pair.left);
        Atom *right = prelude_deref(&u, pair.right);
        if (left == right)
            continue;
        if (left->kind == ATOM_VAR || right->kind == ATOM_VAR) {
            if (left->kind == ATOM_VAR && right->kind == ATOM_VAR &&
                var_id_eq(left->var_id, right->var_id))
                continue;
            Atom *var = left->kind == ATOM_VAR ? left : right;
            Atom *value = var == left ? right : left;
            if (!prelude_grow((void **)&u.bindings, &u.binding_cap,
                              u.binding_len, sizeof(*u.bindings))) {
                verdict = -1;
                break;
            }
            u.bindings[u.binding_len++] = (PreludeBinding){var->var_id, value};
            continue;
        }
        if (left->kind == ATOM_EXPR && right->kind == ATOM_EXPR) {
            if (left->expr.len != right->expr.len) {
                verdict = 0;
                break;
            }
            bool met = false;
            for (size_t k = 0u; k < u.seen_len && !met; k++)
                met = u.seen[k].left == left && u.seen[k].right == right;
            if (met)
                continue;
            if (!prelude_grow((void **)&u.seen, &u.seen_cap, u.seen_len,
                              sizeof(*u.seen))) {
                verdict = -1;
                break;
            }
            u.seen[u.seen_len++] = (PreludePair){left, right};
            for (CettaExprIndex k = left->expr.len; k > 0u; k--) {
                if (!prelude_push_pair(&u, left->expr.elems[k - 1u],
                                       right->expr.elems[k - 1u])) {
                    verdict = -1;
                    break;
                }
            }
            continue;
        }
        if (!atom_eq(left, right))
            verdict = 0;
    }
    free(u.bindings);
    free(u.stack);
    free(u.seen);
    return verdict;
}

/* ── Dispatch ───────────────────────────────────────────────────────── */

uint16_t petta_prelude_native_arities(SymbolId head) {
    if (head == SYMBOL_ID_NONE)
        return 0u;
    if (head == g_builtin_syms.petta_unifiable ||
        head == g_builtin_syms.petta_atom_concat)
        return 1u << 2;
    if (head == g_builtin_syms.petta_atom_chars ||
        head == g_builtin_syms.petta_copy_term)
        return 1u << 1;
    if (head == g_builtin_syms.petta_term_hash)
        return (1u << 1) | (1u << 3);
    return 0u;
}

bool petta_prelude_function(SymbolId head, uint32_t nargs) {
    return nargs < 16u &&
           (petta_prelude_native_arities(head) & (1u << nargs)) != 0u;
}

Atom *petta_prelude_apply(Arena *arena, SymbolId head, Atom **args,
                          uint32_t nargs, bool *raised) {
    if (raised)
        *raised = false;
    if (!arena || !raised || !petta_prelude_function(head, nargs))
        return NULL;
    for (uint32_t k = 0u; k < nargs; k++)
        if (!args[k])
            return NULL;
    if (head == g_builtin_syms.petta_unifiable) {
        int verdict = prelude_unifiable(args[0], args[1]);
        return verdict < 0 ? NULL
                           : atom_symbol(arena, verdict ? "true" : "false");
    }
    if (head == g_builtin_syms.petta_atom_concat)
        return prelude_atom_concat(arena, args[0], args[1], raised);
    if (head == g_builtin_syms.petta_atom_chars)
        return prelude_atom_chars(arena, args[0], raised);
    if (head == g_builtin_syms.petta_copy_term) {
        Atom *none = atom_expr(arena, NULL, 0u);
        return none ? rename_vars_except(arena, args[0], none) : NULL;
    }
    if (nargs == 3u)
        return prelude_term_hash_bounded(arena, args[0], args[1], args[2],
                                         raised);
    return prelude_term_hash(arena, args[0]);
}

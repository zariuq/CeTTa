#include "atom_wire.h"
#include "tensor_dense_v1.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CettaDenseTensorV1Status cetta_dense_tensor_v1_encode(const CettaDenseTensorV1 *, unsigned char **, size_t *)
    __attribute__((weak));
CettaDenseTensorV1Status cetta_dense_tensor_v1_encoded_size(const CettaDenseTensorV1 *, size_t *)
    __attribute__((weak));
CettaDenseTensorV1Status cetta_dense_tensor_v1_decode(const unsigned char *, size_t, CettaDenseTensorV1 **)
    __attribute__((weak));
void cetta_dense_tensor_v1_release(void *) __attribute__((weak));

_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "CAV1 requires binary64");

typedef struct {
    unsigned char *bytes;
    size_t size, capacity, nodes, vars;
    VarId ids[CETTA_ATOM_WIRE_VARS];
} Writer;

static CettaAtomWireStatus append(Writer *w, const void *bytes, size_t n) {
    if (n > CETTA_ATOM_WIRE_BYTES - w->size) return ATOM_WIRE_LIMIT;
    if (w->size + n > w->capacity) {
        size_t cap = w->capacity ? w->capacity * 2 : 256;
        if (cap < w->size + n) cap = w->size + n;
        if (cap > CETTA_ATOM_WIRE_BYTES) cap = CETTA_ATOM_WIRE_BYTES;
        void *p = realloc(w->bytes, cap);
        if (!p) return ATOM_WIRE_NOMEM;
        w->bytes = p; w->capacity = cap;
    }
    if (n) memcpy(w->bytes + w->size, bytes, n);
    w->size += n;
    return ATOM_WIRE_OK;
}
static CettaAtomWireStatus number(Writer *w, uint64_t value, size_t n) {
    unsigned char bytes[8];
    for (size_t i = 0; i < n; ++i) bytes[i] = (unsigned char)(value >> (8 * i));
    return append(w, bytes, n);
}
static CettaAtomWireStatus encode(Writer *w, const Atom *a, unsigned depth) {
    if (!a) return ATOM_WIRE_INVALID;
    if (depth > CETTA_ATOM_WIRE_DEPTH || ++w->nodes > CETTA_ATOM_WIRE_NODES)
        return ATOM_WIRE_LIMIT;
    unsigned char tag;
    const char *text = NULL;
    size_t len = 0;
    uint64_t bits = 0;
    if (a->kind == ATOM_SYMBOL) {
        tag = 'S'; text = atom_symbol_bytes(a); len = atom_symbol_length(a);
    } else if (a->kind == ATOM_VAR) {
        tag = 'V';
        size_t index = 0;
        while (index < w->vars && !var_id_eq(w->ids[index], a->var_id)) ++index;
        if (index == w->vars) {
            if (w->vars == CETTA_ATOM_WIRE_VARS) return ATOM_WIRE_LIMIT;
            w->ids[w->vars++] = a->var_id;
        }
        bits = index;
    } else if (a->kind == ATOM_EXPR) {
        tag = 'E'; bits = a->expr.len;
        if (bits > CETTA_ATOM_WIRE_NODES) return ATOM_WIRE_LIMIT;
    } else if (a->kind == ATOM_GROUNDED) {
        switch (a->ground.gkind) {
        case GV_INT:
            /* An owning identifier is a resource reference, not its printed
             * integer. Encoding it would silently erase the lifetime and
             * authority carried by the value. Plain identifier data remains
             * ordinary portable syntax. */
            if (a->structural_facts & ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID)
                return ATOM_WIRE_UNSUPPORTED;
            tag = 'I'; memcpy(&bits, &a->ground.ival, 8); break;
        case GV_FLOAT: tag = 'F'; memcpy(&bits, &a->ground.fval, 8); break;
        case GV_BOOL: tag = a->ground.bval ? 'Y' : 'N'; break;
        case GV_STRING: tag = 'T'; text = a->ground.sval; len = atom_string_len(a); break;
        case GV_BIGINT: tag = 'J'; text = atom_bigint_cstr(a); if (text) len = strlen(text); break;
        case GV_RATIONAL: tag = 'Q'; text = atom_rational_cstr(a); if (text) len = strlen(text); break;
        case GV_TENSOR: {
            if (!cetta_dense_tensor_v1_encode || !cetta_dense_tensor_v1_encoded_size) return ATOM_WIRE_UNSUPPORTED;
            unsigned char *encoded; size_t size;
            CettaDenseTensorV1Status result = cetta_dense_tensor_v1_encoded_size(a->ground.ptr, &size);
            if (result != CETTA_DENSE_TENSOR_V1_OK) return ATOM_WIRE_INVALID;
            size_t room = CETTA_ATOM_WIRE_BYTES - w->size;
            if (room < 5u || size > room - 5u) return ATOM_WIRE_LIMIT;
            result = cetta_dense_tensor_v1_encode(a->ground.ptr, &encoded, &size);
            if (result != CETTA_DENSE_TENSOR_V1_OK)
                return result == CETTA_DENSE_TENSOR_V1_NOMEM ? ATOM_WIRE_NOMEM : ATOM_WIRE_INVALID;
            tag = 'A';
            CettaAtomWireStatus status = size > CETTA_ATOM_WIRE_BYTES ? ATOM_WIRE_LIMIT :
                append(w, &tag, 1u);
            if (status == ATOM_WIRE_OK) status = number(w, size, 4u);
            if (status == ATOM_WIRE_OK) status = append(w, encoded, size);
            free(encoded);
            return status;
        }
        default: return ATOM_WIRE_UNSUPPORTED;
        }
    } else return ATOM_WIRE_UNSUPPORTED;
    if ((tag == 'S' || tag == 'T' || tag == 'J' || tag == 'Q') && !text)
        return ATOM_WIRE_INVALID;
    CettaAtomWireStatus status = append(w, &tag, 1);
    if (status != ATOM_WIRE_OK) return status;
    if (text) {
        if (len > CETTA_ATOM_WIRE_BYTES) return ATOM_WIRE_LIMIT;
        status = number(w, len, 4);
        return status == ATOM_WIRE_OK ? append(w, text, len) : status;
    }
    if (tag == 'I' || tag == 'F') return number(w, bits, 8);
    if (tag == 'V') return number(w, bits, 4);
    if (tag == 'E') {
        status = number(w, bits, 4);
        for (size_t i = 0; status == ATOM_WIRE_OK && i < bits; ++i)
            status = encode(w, a->expr.elems[i], depth + 1);
    }
    return status;
}

CettaAtomWireStatus cetta_atom_wire_encode(const Atom *a, unsigned char **bytes, size_t *n) {
    if (!bytes || !n) return ATOM_WIRE_INVALID;
    *bytes = NULL; *n = 0;
    Writer *w = calloc(1, sizeof(*w));
    if (!w) return ATOM_WIRE_NOMEM;
    CettaAtomWireStatus status = append(w, "CAV1", 4);
    if (status == ATOM_WIRE_OK) status = encode(w, a, 0);
    if (status == ATOM_WIRE_OK) { *bytes = w->bytes; *n = w->size; }
    else free(w->bytes);
    free(w);
    return status;
}

typedef struct {
    const unsigned char *bytes;
    size_t size, pos, nodes, vars;
    Atom *variables[CETTA_ATOM_WIRE_VARS];
} Reader;
static bool read_number(Reader *r, size_t n, uint64_t *out) {
    if (n > r->size - r->pos) return false;
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= (uint64_t)r->bytes[r->pos++] << (8 * i);
    *out = v; return true;
}
static bool digits(const char *s) {
    if (*s == '-') ++s;
    if (!*s) return false;
    for (; *s; ++s) if (*s < '0' || *s > '9') return false;
    return true;
}
static CettaAtomWireStatus decode(Reader *r, Arena *arena, unsigned depth, Atom **out) {
    if (depth > CETTA_ATOM_WIRE_DEPTH || ++r->nodes > CETTA_ATOM_WIRE_NODES)
        return ATOM_WIRE_LIMIT;
    uint64_t tag, n;
    if (!read_number(r, 1, &tag)) return ATOM_WIRE_CORRUPT;
    if (tag == 'I' || tag == 'F') {
        if (!read_number(r, 8, &n)) return ATOM_WIRE_CORRUPT;
        if (tag == 'I') { int64_t v; memcpy(&v, &n, 8); *out = atom_int(arena, v); }
        else { double v; memcpy(&v, &n, 8); *out = atom_float(arena, v); }
    } else if (tag == 'Y' || tag == 'N') *out = atom_bool(arena, tag == 'Y');
    else if (tag == 'V') {
        if (!read_number(r, 4, &n) || n > r->vars || n >= CETTA_ATOM_WIRE_VARS)
            return ATOM_WIRE_CORRUPT;
        if (n == r->vars) {
            VarId id;
            if (!fresh_var_id_try(&id)) return ATOM_WIRE_LIMIT;
            char name[24]; snprintf(name, sizeof(name), "_wire%u", (unsigned)n);
            r->variables[r->vars++] = atom_var_with_name_key(arena,
                atom_string(arena, name), id);
        }
        *out = r->variables[n];
    } else if (tag == 'E') {
        if (!read_number(r, 4, &n) || n > CETTA_ATOM_WIRE_NODES - r->nodes ||
            n > r->size - r->pos) return ATOM_WIRE_CORRUPT;
        Atom **items = arena_alloc(arena, n * sizeof(*items));
        for (size_t i = 0; i < n; ++i) {
            CettaAtomWireStatus status = decode(r, arena, depth + 1, &items[i]);
            if (status != ATOM_WIRE_OK) return status;
        }
        *out = atom_expr(arena, items, n);
    } else if (tag == 'A') {
        if (!cetta_dense_tensor_v1_decode || !cetta_dense_tensor_v1_release) return ATOM_WIRE_UNSUPPORTED;
        if (!read_number(r, 4u, &n) || n > r->size - r->pos) return ATOM_WIRE_CORRUPT;
        CettaDenseTensorV1 *tensor = NULL;
        CettaDenseTensorV1Status status = cetta_dense_tensor_v1_decode(r->bytes + r->pos, (size_t)n, &tensor);
        if (status != CETTA_DENSE_TENSOR_V1_OK)
            return status == CETTA_DENSE_TENSOR_V1_NOMEM ? ATOM_WIRE_NOMEM : ATOM_WIRE_CORRUPT;
        r->pos += (size_t)n;
        *out = atom_tensor(arena, tensor);
        cetta_dense_tensor_v1_release(tensor);
    } else if (tag == 'S' || tag == 'T' || tag == 'J' || tag == 'Q') {
        if (!read_number(r, 4, &n) || n > r->size - r->pos ||
            ((tag == 'J' || tag == 'Q') && memchr(r->bytes + r->pos, 0, n)))
            return ATOM_WIRE_CORRUPT;
        char *s = arena_alloc(arena, n + 1);
        memcpy(s, r->bytes + r->pos, n); s[n] = 0; r->pos += n;
        if (tag == 'S') *out = atom_symbol_data_n(arena, s, (uint32_t)n);
        else if (tag == 'T') *out = atom_string_n(arena, s, n);
        else {
#if CETTA_BUILD_WITH_GMP
            if (tag == 'J') {
                if (!digits(s)) return ATOM_WIRE_CORRUPT;
                *out = atom_bigint(arena, s);
            } else {
                char *sep = strchr(s, 'r');
                if (!sep) sep = strchr(s, '/');
                if (!sep) return ATOM_WIRE_CORRUPT;
                char saved = *sep; *sep = 0;
                bool valid = digits(s) && digits(sep + 1) && sep[1] != '-' &&
                    strspn(sep + 1, "0") != strlen(sep + 1);
                *sep = saved;
                if (!valid) return ATOM_WIRE_CORRUPT;
                *out = atom_rational(arena, s);
            }
#else
            (void)digits;
            return ATOM_WIRE_UNSUPPORTED;
#endif
        }
    } else return ATOM_WIRE_CORRUPT;
    return *out ? ATOM_WIRE_OK : ATOM_WIRE_NOMEM;
}

CettaAtomWireStatus cetta_atom_wire_decode(Arena *arena, const unsigned char *bytes,
        size_t n, Atom **out) {
    if (!arena || !bytes || !out) return ATOM_WIRE_INVALID;
    *out = NULL;
    if (n > CETTA_ATOM_WIRE_BYTES) return ATOM_WIRE_LIMIT;
    if (n < 4 || memcmp(bytes, "CAV1", 4)) return ATOM_WIRE_CORRUPT;
    Reader *r = calloc(1, sizeof(*r));
    if (!r) return ATOM_WIRE_NOMEM;
    r->bytes = bytes; r->size = n; r->pos = 4;
    Atom *value = NULL;
    CettaAtomWireStatus status = decode(r, arena, 0, &value);
    if (status == ATOM_WIRE_OK && r->pos != n) status = ATOM_WIRE_CORRUPT;
    if (status == ATOM_WIRE_OK) *out = value;
    free(r);
    return status;
}
const char *cetta_atom_wire_status_name(CettaAtomWireStatus status) {
    static const char *names[] = {"ok", "invalid", "limit", "corrupt", "unsupported", "memory"};
    return (unsigned)status < sizeof(names) / sizeof(*names) ? names[status] : "invalid";
}

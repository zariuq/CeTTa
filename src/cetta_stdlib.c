#define _GNU_SOURCE
#include "cetta_stdlib.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Binary blob format ────────────────────────────────────────────────
   Header:
     4 bytes  magic "CSTD"
     2 bytes  version (0x0001)
     2 bytes  num_strings
     4 bytes  string_table_byte_length
     ...NUL-terminated strings...
     2 bytes  num_atoms (top-level)
     ...depth-first serialized atoms...

   Atom tags:
     0x01 SYMBOL  + uint16 string offset
     0x02 VAR     + uint16 string offset
     0x03 EXPR    + uint16 child count + N children
     0x04 INT     + int64 LE
     0x05 FLOAT   + double LE
     0x06 BOOL    + uint8
     0x07 STRING  + uint16 string offset
   ──────────────────────────────────────────────────────────────────── */

#define BLOB_TAG_SYMBOL 0x01
#define BLOB_TAG_VAR    0x02
#define BLOB_TAG_EXPR   0x03
#define BLOB_TAG_INT    0x04
#define BLOB_TAG_FLOAT  0x05
#define BLOB_TAG_BOOL   0x06
#define BLOB_TAG_STRING 0x07

/* ══════════════════════════════════════════════════════════════════════
   SERIALIZER  (--compile-stdlib)
   ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char **names;
    uint16_t *offsets;
    uint32_t len, cap;
    uint32_t total_bytes;
} StringTable;

static void strtab_init(StringTable *st) {
    st->names = NULL; st->offsets = NULL;
    st->len = 0; st->cap = 0; st->total_bytes = 0;
}

static uint16_t strtab_add(StringTable *st, const char *name) {
    for (uint32_t i = 0; i < st->len; i++)
        if (strcmp(st->names[i], name) == 0) return st->offsets[i];
    if (st->len >= st->cap) {
        st->cap = st->cap ? st->cap * 2 : 32;
        st->names = cetta_realloc(st->names, sizeof(const char *) * st->cap);
        st->offsets = cetta_realloc(st->offsets, sizeof(uint16_t) * st->cap);
    }
    uint16_t off = (uint16_t)st->total_bytes;
    st->offsets[st->len] = off;
    st->names[st->len] = name;
    st->len++;
    st->total_bytes += (uint32_t)(strlen(name) + 1);
    return off;
}

static void strtab_free(StringTable *st) {
    free(st->names); free(st->offsets);
    st->names = NULL; st->offsets = NULL;
    st->len = st->cap = 0;
}

/* Collect all strings from atom trees */
static void collect_strings(Atom *atom, StringTable *st) {
    switch (atom->kind) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        strtab_add(st, atom_name_cstr(atom));
        break;
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_STRING)
            strtab_add(st, atom->ground.sval);
        break;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++)
            collect_strings(atom->expr.elems[i], st);
        break;
    }
}

/* Write binary data to a dynamic buffer */
typedef struct {
    unsigned char *data;
    uint32_t len, cap;
} ByteBuf;

static void buf_init(ByteBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

static void buf_write(ByteBuf *b, const void *src, uint32_t n) {
    while (b->len + n > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->data = cetta_realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
}

static void buf_u8(ByteBuf *b, uint8_t v) { buf_write(b, &v, 1); }
static void buf_u16(ByteBuf *b, uint16_t v) { buf_write(b, &v, 2); }
static void buf_u32(ByteBuf *b, uint32_t v) { buf_write(b, &v, 4); }
static void buf_i64(ByteBuf *b, int64_t v) { buf_write(b, &v, 8); }
static void buf_f64(ByteBuf *b, double v) { buf_write(b, &v, 8); }

static void serialize_atom(Atom *atom, StringTable *st, ByteBuf *b) {
    switch (atom->kind) {
    case ATOM_SYMBOL:
        buf_u8(b, BLOB_TAG_SYMBOL);
        buf_u16(b, strtab_add(st, atom_name_cstr(atom)));
        break;
    case ATOM_VAR:
        buf_u8(b, BLOB_TAG_VAR);
        buf_u16(b, strtab_add(st, atom_name_cstr(atom)));
        break;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:    buf_u8(b, BLOB_TAG_INT);    buf_i64(b, atom->ground.ival); break;
        case GV_FLOAT:  buf_u8(b, BLOB_TAG_FLOAT);  buf_f64(b, atom->ground.fval); break;
        case GV_BOOL:   buf_u8(b, BLOB_TAG_BOOL);   buf_u8(b, atom->ground.bval ? 1 : 0); break;
        case GV_STRING: buf_u8(b, BLOB_TAG_STRING);  buf_u16(b, strtab_add(st, atom->ground.sval)); break;
        case GV_FOREIGN:
        default:
            fprintf(stderr, "stdlib_compile: cannot serialize grounded kind %d\n", atom->ground.gkind);
            break;
        }
        break;
    case ATOM_EXPR:
        buf_u8(b, BLOB_TAG_EXPR);
        buf_u16(b, (uint16_t)atom->expr.len);
        for (uint32_t i = 0; i < atom->expr.len; i++)
            serialize_atom(atom->expr.elems[i], st, b);
        break;
    }
}

int stdlib_compile(const char *metta_path, Arena *a, FILE *out) {
    Atom **atoms = NULL;
    int n = parse_metta_file(metta_path, a, &atoms);
    if (n < 0) {
        fprintf(stderr, "stdlib_compile: failed to parse %s\n", metta_path);
        return -1;
    }

    /* Build string table */
    StringTable st;
    strtab_init(&st);
    for (int i = 0; i < n; i++)
        collect_strings(atoms[i], &st);

    /* Serialize atoms */
    ByteBuf ab;
    buf_init(&ab);
    for (int i = 0; i < n; i++)
        serialize_atom(atoms[i], &st, &ab);

    /* Build complete blob: header + string table + atoms */
    ByteBuf blob;
    buf_init(&blob);

    /* Header */
    buf_write(&blob, "CSTD", 4);
    buf_u16(&blob, 0x0001);
    buf_u16(&blob, (uint16_t)st.len);
    buf_u32(&blob, st.total_bytes);

    /* String table */
    for (uint32_t i = 0; i < st.len; i++) {
        size_t slen = strlen(st.names[i]) + 1;
        buf_write(&blob, st.names[i], (uint32_t)slen);
    }

    /* Atom count + atom data */
    buf_u16(&blob, (uint16_t)n);
    buf_write(&blob, ab.data, ab.len);

    /* Output as C header */
    fprintf(out, "/* AUTO-GENERATED by cetta --compile-stdlib — do not edit */\n");
    fprintf(out, "static const unsigned char STDLIB_BLOB[] = {\n");
    for (uint32_t i = 0; i < blob.len; i++) {
        if (i % 16 == 0) fprintf(out, "    ");
        fprintf(out, "0x%02x", blob.data[i]);
        if (i + 1 < blob.len) fprintf(out, ",");
        if (i % 16 == 15 || i + 1 == blob.len) fprintf(out, "\n");
    }
    fprintf(out, "};\n");
    fprintf(out, "static const unsigned int STDLIB_BLOB_LEN = %u;\n", blob.len);

    free(ab.data);
    free(blob.data);
    free(atoms);
    strtab_free(&st);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
   DESERIALIZER  (runtime stdlib_load)
   ══════════════════════════════════════════════════════════════════════ */

#ifdef CETTA_NO_STDLIB

void stdlib_load(Space *s, Arena *a) {
    (void)s; (void)a;
}

#else /* !CETTA_NO_STDLIB */

#include "stdlib_blob.h"

typedef struct {
    const unsigned char *data;
    uint32_t len;
    uint32_t pos;
    const char *strtab;      /* base of NUL-terminated string table */
    uint32_t strtab_bytes;
} BlobReader;

typedef struct {
    SymbolId *spellings;
    VarId *ids;
    uint32_t len;
    uint32_t cap;
} BlobVarScope;

static void blob_var_scope_init(BlobVarScope *scope) {
    if (!scope)
        return;
    scope->spellings = NULL;
    scope->ids = NULL;
    scope->len = 0;
    scope->cap = 0;
}

static void blob_var_scope_free(BlobVarScope *scope) {
    if (!scope)
        return;
    free(scope->spellings);
    free(scope->ids);
    scope->spellings = NULL;
    scope->ids = NULL;
    scope->len = 0;
    scope->cap = 0;
}

static VarId blob_var_scope_id(BlobVarScope *scope, SymbolId spelling) {
    if (!scope)
        return fresh_var_id();
    for (uint32_t i = 0; i < scope->len; i++) {
        if (scope->spellings[i] == spelling)
            return scope->ids[i];
    }
    if (scope->len == scope->cap) {
        uint32_t next_cap = scope->cap ? scope->cap * 2 : 16;
        scope->spellings =
            cetta_realloc(scope->spellings, sizeof(SymbolId) * next_cap);
        scope->ids = cetta_realloc(scope->ids, sizeof(VarId) * next_cap);
        scope->cap = next_cap;
    }
    VarId id = fresh_var_id();
    scope->spellings[scope->len] = spelling;
    scope->ids[scope->len] = id;
    scope->len++;
    return id;
}

static uint8_t  rd_u8(BlobReader *r)  { return (r->pos < r->len) ? r->data[r->pos++] : 0; }
static uint16_t rd_u16(BlobReader *r) {
    uint16_t v = 0;
    if (r->pos + 2 <= r->len) { memcpy(&v, r->data + r->pos, 2); r->pos += 2; }
    return v;
}
static uint32_t rd_u32(BlobReader *r) {
    uint32_t v = 0;
    if (r->pos + 4 <= r->len) { memcpy(&v, r->data + r->pos, 4); r->pos += 4; }
    return v;
}
static int64_t rd_i64(BlobReader *r) {
    int64_t v = 0;
    if (r->pos + 8 <= r->len) { memcpy(&v, r->data + r->pos, 8); r->pos += 8; }
    return v;
}
static double rd_f64(BlobReader *r) {
    double v = 0;
    if (r->pos + 8 <= r->len) { memcpy(&v, r->data + r->pos, 8); r->pos += 8; }
    return v;
}

static const char *rd_str(BlobReader *r, uint16_t offset) {
    if (offset < r->strtab_bytes)
        return r->strtab + offset;
    return "";
}

static AtomId deserialize_atom_id_scoped(BlobReader *r, TermUniverse *universe,
                                         BlobVarScope *scope) {
    if (!r || !universe)
        return CETTA_ATOM_ID_NONE;
    uint8_t tag = rd_u8(r);
    switch (tag) {
    case BLOB_TAG_SYMBOL: {
        SymbolId spelling = symbol_intern_cstr(g_symbols, rd_str(r, rd_u16(r)));
        return tu_intern_symbol(universe, spelling);
    }
    case BLOB_TAG_VAR: {
        SymbolId spelling = symbol_intern_cstr(g_symbols, rd_str(r, rd_u16(r)));
        VarId var_id = blob_var_scope_id(scope, spelling);
        return tu_intern_var(universe, spelling, var_id);
    }
    case BLOB_TAG_INT:
        return tu_intern_int(universe, rd_i64(r));
    case BLOB_TAG_FLOAT:
        return tu_intern_float(universe, rd_f64(r));
    case BLOB_TAG_BOOL:
        return tu_intern_bool(universe, rd_u8(r) != 0);
    case BLOB_TAG_STRING:
        return tu_intern_string(universe, rd_str(r, rd_u16(r)));
    case BLOB_TAG_EXPR: {
        uint16_t count = rd_u16(r);
        AtomId *child_ids = cetta_malloc(sizeof(AtomId) * count);
        AtomId expr_id = CETTA_ATOM_ID_NONE;
        for (uint16_t i = 0; i < count; i++) {
            child_ids[i] = deserialize_atom_id_scoped(r, universe, scope);
            if (child_ids[i] == CETTA_ATOM_ID_NONE)
                goto cleanup;
        }
        expr_id = tu_expr_from_ids(universe, child_ids, count);
cleanup:
        free(child_ids);
        return expr_id;
    }
    default:
        fprintf(stderr, "stdlib_load: unknown blob tag 0x%02x at offset %u\n", tag, r->pos - 1);
        return CETTA_ATOM_ID_NONE;
    }
}

static Atom *deserialize_atom_scoped(BlobReader *r, Arena *a,
                                     BlobVarScope *scope) {
    uint8_t tag = rd_u8(r);
    switch (tag) {
    case BLOB_TAG_SYMBOL: return atom_symbol(a, rd_str(r, rd_u16(r)));
    case BLOB_TAG_VAR: {
        SymbolId spelling = symbol_intern_cstr(g_symbols, rd_str(r, rd_u16(r)));
        VarId id = blob_var_scope_id(scope, spelling);
        return atom_var_with_spelling(a, spelling, id);
    }
    case BLOB_TAG_INT:    return atom_int(a, rd_i64(r));
    case BLOB_TAG_FLOAT:  return atom_float(a, rd_f64(r));
    case BLOB_TAG_BOOL:   return atom_bool(a, rd_u8(r) != 0);
    case BLOB_TAG_STRING: return atom_string(a, rd_str(r, rd_u16(r)));
    case BLOB_TAG_EXPR: {
        uint16_t count = rd_u16(r);
        Atom **elems = arena_alloc(a, sizeof(Atom *) * count);
        for (uint16_t i = 0; i < count; i++)
            elems[i] = deserialize_atom_scoped(r, a, scope);
        return atom_expr(a, elems, count);
    }
    default:
        fprintf(stderr, "stdlib_load: unknown blob tag 0x%02x at offset %u\n", tag, r->pos - 1);
        return atom_symbol(a, "Error");
    }
}

static bool stdlib_load_blob_internal(Space *s, Arena *a,
                                      const unsigned char *blob,
                                      uint32_t blob_len) {
    if (!blob || blob_len < 12)
        return false;

    BlobReader r = {
        .data = blob,
        .len = blob_len,
        .pos = 0,
        .strtab = NULL,
        .strtab_bytes = 0,
    };

    /* Verify magic */
    if (r.data[0] != 'C' || r.data[1] != 'S' || r.data[2] != 'T' || r.data[3] != 'D') {
        fprintf(stderr, "stdlib_load: bad magic\n");
        return false;
    }
    r.pos = 4;

    /* Version */
    uint16_t version = rd_u16(&r);
    (void)version;

    /* String table */
    uint16_t num_strings = rd_u16(&r);
    (void)num_strings;
    uint32_t strtab_bytes = rd_u32(&r);
    r.strtab = (const char *)(r.data + r.pos);
    r.strtab_bytes = strtab_bytes;
    r.pos += strtab_bytes;

    /* Atoms */
    uint16_t num_atoms = rd_u16(&r);
    if (s && s->native.universe) {
        BlobReader direct = r;
        bool direct_ok = true;
        AtomId *atom_ids = cetta_malloc(sizeof(AtomId) * num_atoms);
        for (uint16_t i = 0; i < num_atoms; i++) {
            BlobVarScope scope;
            blob_var_scope_init(&scope);
            atom_ids[i] = deserialize_atom_id_scoped(&direct, s->native.universe,
                                                     &scope);
            blob_var_scope_free(&scope);
            if (atom_ids[i] == CETTA_ATOM_ID_NONE) {
                direct_ok = false;
                break;
            }
        }
        if (direct_ok) {
            for (uint16_t i = 0; i < num_atoms; i++)
                space_add_atom_id(s, atom_ids[i]);
            free(atom_ids);
            return true;
        }
        free(atom_ids);
    }
    for (uint16_t i = 0; i < num_atoms; i++) {
        BlobVarScope scope;
        blob_var_scope_init(&scope);
        Atom *atom = deserialize_atom_scoped(&r, a, &scope);
        blob_var_scope_free(&scope);
        if (!space_admit_atom(s, a, atom))
            space_add(s, atom);
    }
    return true;
}

void stdlib_load(Space *s, Arena *a) {
    (void)stdlib_load_blob_internal(s, a, STDLIB_BLOB, STDLIB_BLOB_LEN);
}

#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
bool stdlib_load_blob_bytes(Space *s, Arena *a, const unsigned char *data,
                            uint32_t len) {
    return stdlib_load_blob_internal(s, a, data, len);
}
#endif

#endif /* !CETTA_NO_STDLIB */

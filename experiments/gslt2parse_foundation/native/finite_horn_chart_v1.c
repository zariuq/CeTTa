#define _POSIX_C_SOURCE 200809L
/* ===========================================================================
 * finite_horn_chart_v1.c -- a generic tabled finite-Horn chart engine.
 *
 * The presentation is the ONLY authority.  This file contains no grammar
 * combinator, no lexer, no token table, no start-category field, and no guest
 * language.  It reads (rule <id> (head <H>) (body <G>...)) forms and evaluates
 * them by unification and tabled fixpoint saturation.
 *
 * The answer table IS the chart.  Each answer node carries the set of
 * derivations that produced it (its "packings"), which is the SPPF: duplicate
 * derivations of the same value share one node, distinct values stay distinct.
 *
 * The strict finite-Horn package reader validates every source before this
 * execution representation sees it.  This file is an untrusted producer and
 * certificate emitter, never an authority for a guest language.
 * ======================================================================== */

#include "finite_horn_gslt_v1.h"
#include "native_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>

static void fail_resource(const char *what) {
    fprintf(stderr, "resource limit: %s\n", what);
    exit(3);
}

/* ------------------------------------------------------------------ terms
 * Immutable terms are content-addressed inside two append-only arenas:
 *   - nodes[] stores fixed-size headers and structural hashes;
 *   - child_ids[] stores all child TermIds contiguously.
 *
 * newnode() consumes the caller's temporary child vector and hash-conses the
 * result.  Equal immutable terms therefore have one stable TermId.  This is
 * the key representation invariant used by goal/answer indexing and by the
 * input-suffix position map; no serialized term strings are used as keys.
 */
enum { T_SYM, T_INT, T_VAR, T_APP, T_STR, T_BIGINT };
enum { NF_HAS_VAR = 1u };

typedef struct {
    uint64_t hash;
    uint32_t kid_off;
    int32_t sym;       /* symbol id, integer value, or variable id */
    int32_t hnext;     /* hash-cons collision chain */
    uint16_t nkids;
    uint8_t tag;
    uint8_t flags;
} Node;

static Node *nodes = NULL;
static int nnodes = 0, capnodes = 0;
static int *child_ids = NULL;
static uint32_t nchild_ids = 0, capchild_ids = 0;
static int *node_buckets = NULL;
static uint32_t node_bucket_cap = 0;

#define KID(t, i) (child_ids[nodes[(t)].kid_off + (uint32_t)(i)])

static uint64_t mix64(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static uint64_t term_hash_parts(int tag, int sym, int nkids, const int *kids) {
    uint64_t h = mix64((uint64_t)(uint32_t)tag ^
                       ((uint64_t)(uint32_t)sym << 17) ^
                       ((uint64_t)(uint32_t)nkids << 49));
    for (int i = 0; i < nkids; i++) {
        h ^= mix64(nodes[kids[i]].hash + UINT64_C(0x9e3779b97f4a7c15) +
                   (uint64_t)(uint32_t)i);
        h = (h << 13) | (h >> 51);
    }
    return mix64(h);
}

static void node_index_rehash(uint32_t want) {
    uint32_t cap = 4096;
    while (cap < want) cap <<= 1;
    int *b = malloc((size_t)cap * sizeof(int));
    if (!b) { fprintf(stderr, "oom term index\n"); exit(2); }
    for (uint32_t i = 0; i < cap; i++) b[i] = -1;
    for (int i = 0; i < nnodes; i++) {
        uint32_t slot = (uint32_t)nodes[i].hash & (cap - 1u);
        nodes[i].hnext = b[slot];
        b[slot] = i;
    }
    free(node_buckets);
    node_buckets = b;
    node_bucket_cap = cap;
}

static int node_matches(int id, int tag, int sym, int nkids, const int *kids,
                        uint64_t hash) {
    const Node *n = &nodes[id];
    if (n->hash != hash || n->tag != tag || n->sym != sym || n->nkids != nkids)
        return 0;
    for (int i = 0; i < nkids; i++) if (KID(id, i) != kids[i]) return 0;
    return 1;
}

/* Takes ownership of kids, including on a hash-cons hit. */
static int newnode(int tag, int sym, int nkids, int *kids) {
    if (nkids < 0 || nkids > UINT16_MAX) { free(kids); fail_resource("term arity exceeds 65535"); }
    for (int i = 0; i < nkids; i++)
        if (kids[i] < 0 || kids[i] >= nnodes) { free(kids); fail_resource("invalid child TermId"); }
    if (!node_bucket_cap) node_index_rehash(4096);
    uint64_t hash = term_hash_parts(tag, sym, nkids, kids);
    uint32_t slot = (uint32_t)hash & (node_bucket_cap - 1u);
    for (int id = node_buckets[slot]; id >= 0; id = nodes[id].hnext) {
        if (node_matches(id, tag, sym, nkids, kids, hash)) {
            free(kids);
            return id;
        }
    }
    if ((uint32_t)(nnodes + 1) * 4u >= node_bucket_cap * 3u) {
        node_index_rehash(node_bucket_cap << 1);
        slot = (uint32_t)hash & (node_bucket_cap - 1u);
    }
    if (nnodes == capnodes) {
        capnodes = capnodes ? capnodes * 2 : 4096;
        nodes = realloc(nodes, (size_t)capnodes * sizeof(Node));
        if (!nodes) { fprintf(stderr, "oom nodes\n"); exit(2); }
    }
    uint32_t off = nchild_ids;
    if (nkids > 0) {
        uint32_t need = nchild_ids + (uint32_t)nkids;
        if (need > capchild_ids) {
            uint32_t cap = capchild_ids ? capchild_ids : 8192;
            while (cap < need) cap <<= 1;
            child_ids = realloc(child_ids, (size_t)cap * sizeof(int));
            if (!child_ids) { fprintf(stderr, "oom term children\n"); exit(2); }
            capchild_ids = cap;
        }
        memcpy(child_ids + nchild_ids, kids, (size_t)nkids * sizeof(int));
        nchild_ids = need;
    }
    uint8_t flags = (tag == T_VAR) ? NF_HAS_VAR : 0;
    for (int i = 0; i < nkids; i++) flags |= nodes[kids[i]].flags & NF_HAS_VAR;
    free(kids);
    nodes[nnodes] = (Node){
        .hash = hash, .kid_off = off, .sym = sym,
        .hnext = node_buckets[slot], .nkids = (uint16_t)nkids,
        .tag = (uint8_t)tag, .flags = flags
    };
    node_buckets[slot] = nnodes;
    return nnodes++;
}

/* -------------------------------------------------------------- symbols
 * Symbols are independently hash-consed; presentation loading is no longer a
 * quadratic linear scan through every symbol already seen.
 */
typedef struct {
    char *name;
    uint32_t len;
    uint32_t hash;
    int next;
} Symbol;
static Symbol *symbols = NULL;
static int nsyms = 0, capsyms = 0;
static int *sym_buckets = NULL;
static uint32_t sym_bucket_cap = 0;

static uint32_t hash_bytes(const char *s, int len) {
    uint32_t h = UINT32_C(2166136261);
    for (int i = 0; i < len; i++) { h ^= (unsigned char)s[i]; h *= UINT32_C(16777619); }
    return h ? h : 1u;
}
static void sym_index_rehash(uint32_t want) {
    uint32_t cap = 1024;
    while (cap < want) cap <<= 1;
    int *b = malloc((size_t)cap * sizeof(int));
    if (!b) { fprintf(stderr, "oom symbol index\n"); exit(2); }
    for (uint32_t i = 0; i < cap; i++) b[i] = -1;
    for (int i = 0; i < nsyms; i++) {
        uint32_t slot = symbols[i].hash & (cap - 1u);
        symbols[i].next = b[slot]; b[slot] = i;
    }
    free(sym_buckets); sym_buckets = b; sym_bucket_cap = cap;
}
static int intern(const char *s, int len) {
    if (!sym_bucket_cap) sym_index_rehash(1024);
    uint32_t h = hash_bytes(s, len), slot = h & (sym_bucket_cap - 1u);
    for (int i = sym_buckets[slot]; i >= 0; i = symbols[i].next)
        if (symbols[i].hash == h && symbols[i].len == (uint32_t)len &&
            !memcmp(symbols[i].name, s, (size_t)len)) return i;
    if ((uint32_t)(nsyms + 1) * 4u >= sym_bucket_cap * 3u) {
        sym_index_rehash(sym_bucket_cap << 1);
        slot = h & (sym_bucket_cap - 1u);
    }
    if (nsyms == capsyms) {
        capsyms = capsyms ? capsyms * 2 : 1024;
        symbols = realloc(symbols, (size_t)capsyms * sizeof(Symbol));
        if (!symbols) { fprintf(stderr, "oom symbols\n"); exit(2); }
    }
    char *name = malloc((size_t)len + 1u);
    if (!name) { fprintf(stderr, "oom symbol text\n"); exit(2); }
    memcpy(name, s, (size_t)len); name[len] = 0;
    symbols[nsyms] = (Symbol){ name, (uint32_t)len, h, sym_buckets[slot] };
    sym_buckets[slot] = nsyms;
    return nsyms++;
}
static const char *symname(int i) { return symbols[i].name; }
static const char *symbytes(int i) { return symbols[i].name; }
static size_t symlen(int i) { return symbols[i].len; }

/* ------------------------------------------------------- s-expression read */
typedef struct { const char *p, *end; int failed; } Reader;

static int utf8_decode_one(const unsigned char *bytes, size_t len, size_t pos,
                           uint32_t *codepoint, size_t *width);

static int unicode_space(uint32_t codepoint) {
    return (codepoint >= 0x09u && codepoint <= 0x0du) ||
           codepoint == 0x20u || codepoint == 0x85u ||
           codepoint == 0xa0u || codepoint == 0x1680u ||
           (codepoint >= 0x2000u && codepoint <= 0x200au) ||
           codepoint == 0x2028u || codepoint == 0x2029u ||
           codepoint == 0x202fu || codepoint == 0x205fu ||
           codepoint == 0x3000u;
}

static void reader_fail(Reader *r) {
    r->failed = 1;
}

static void skipws(Reader *r) {
    for (;;) {
        if (r->p < r->end && *r->p == ';') {
            while (r->p < r->end && *r->p != '\n') r->p++;
            continue;
        }
        if (r->p < r->end) {
            uint32_t codepoint = 0;
            size_t width = 0;
            size_t remaining = (size_t)(r->end - r->p);
            if (!utf8_decode_one((const unsigned char *)r->p, remaining, 0,
                                 &codepoint, &width)) {
                reader_fail(r);
                r->p = r->end;
                return;
            }
            if (unicode_space(codepoint)) {
                r->p += width;
                continue;
            }
        }
        break;
    }
}

/* variable naming: ?x in source becomes a fresh T_VAR per rule */
static int rd_term(Reader *r, int *varmap, int *nvars);

static int token_is_integer(const char *text, int len) {
    int pos = (len > 0 && text[0] == '-') ? 1 : 0;
    if (pos == len) return 0;
    for (; pos < len; pos++)
        if (text[pos] < '0' || text[pos] > '9') return 0;
    return 1;
}

static int rd_integer(const char *text, int len) {
    int negative = text[0] == '-';
    int pos = negative ? 1 : 0;
    while (pos < len && text[pos] == '0') pos++;
    if (pos == len) return newnode(T_INT, 0, 0, NULL);

    int digits = len - pos;
    int canonical_len = digits + negative;
    char *canonical = malloc((size_t)canonical_len + 1u);
    if (!canonical) { fprintf(stderr, "oom canonical integer\n"); exit(2); }
    int out = 0;
    if (negative) canonical[out++] = '-';
    memcpy(canonical + out, text + pos, (size_t)digits);
    canonical[canonical_len] = 0;

    char *end = NULL;
    errno = 0;
    long long value = strtoll(canonical, &end, 10);
    int result;
    if (errno == 0 && end && *end == 0 &&
        value >= INT32_MIN && value <= INT32_MAX) {
        result = newnode(T_INT, (int32_t)value, 0, NULL);
    } else {
        result = newnode(T_BIGINT,
                         intern(canonical, canonical_len), 0, NULL);
    }
    free(canonical);
    return result;
}

static int rd_atomtok(Reader *r, int *varmap, int *nvars) {
    const char *s = r->p;
    while (r->p < r->end) {
        unsigned char byte = (unsigned char)*r->p;
        if (byte == '(' || byte == ')' || byte == ';' || byte == '"') break;
        uint32_t codepoint = 0;
        size_t width = 0;
        size_t remaining = (size_t)(r->end - r->p);
        if (!utf8_decode_one((const unsigned char *)r->p, remaining, 0,
                             &codepoint, &width)) {
            reader_fail(r);
            return -1;
        }
        if (unicode_space(codepoint)) break;
        r->p += width;
    }
    ptrdiff_t raw_len = r->p - s;
    if (raw_len > INT_MAX) fail_resource("reader token exceeds INT_MAX bytes");
    int len = (int)raw_len;
    if (len == 0) return -1;
    if (token_is_integer(s, len)) return rd_integer(s, len);
    if (s[0] == '?' && len > 1) {            /* source variable */
        int id = intern(s, len);
        for (int i = 0; i < *nvars; i++) if (varmap[i] == id) return newnode(T_VAR, i, 0, NULL);
        varmap[*nvars] = id;
        return newnode(T_VAR, (*nvars)++, 0, NULL);
    }
    return newnode(T_SYM, intern(s, len), 0, NULL);
}

static int hex_digit(unsigned char byte) {
    if (byte >= '0' && byte <= '9') return (int)(byte - '0');
    if (byte >= 'a' && byte <= 'f') return 10 + (int)(byte - 'a');
    if (byte >= 'A' && byte <= 'F') return 10 + (int)(byte - 'A');
    return -1;
}

static int rd_hex4(Reader *r, uint32_t *value) {
    if ((size_t)(r->end - r->p) < 4u) {
        reader_fail(r);
        return 0;
    }
    uint32_t parsed = 0;
    for (int i = 0; i < 4; i++) {
        int digit = hex_digit((unsigned char)r->p[i]);
        if (digit < 0) {
            reader_fail(r);
            return 0;
        }
        parsed = (parsed << 4u) | (uint32_t)digit;
    }
    r->p += 4;
    *value = parsed;
    return 1;
}

static void string_bytes_append(char **bytes, size_t *len, size_t *cap,
                                const char *more, size_t more_len) {
    if (more_len > SIZE_MAX - *len) fail_resource("string term too large");
    size_t need = *len + more_len;
    if (need > *cap) {
        size_t next_cap = *cap ? *cap : 64u;
        while (next_cap < need) {
            if (next_cap > SIZE_MAX / 2u) next_cap = need;
            else next_cap *= 2u;
        }
        *bytes = realloc(*bytes, next_cap);
        if (!*bytes) { fprintf(stderr, "oom string term\n"); exit(2); }
        *cap = next_cap;
    }
    if (more_len) memcpy(*bytes + *len, more, more_len);
    *len = need;
}

static void string_codepoint_append(char **bytes, size_t *len, size_t *cap,
                                    uint32_t codepoint) {
    char encoded[4];
    size_t width;
    if (codepoint <= 0x7fu) {
        encoded[0] = (char)codepoint; width = 1u;
    } else if (codepoint <= 0x7ffu) {
        encoded[0] = (char)(0xc0u | (codepoint >> 6u));
        encoded[1] = (char)(0x80u | (codepoint & 0x3fu)); width = 2u;
    } else if (codepoint <= 0xffffu) {
        encoded[0] = (char)(0xe0u | (codepoint >> 12u));
        encoded[1] = (char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[2] = (char)(0x80u | (codepoint & 0x3fu)); width = 3u;
    } else {
        encoded[0] = (char)(0xf0u | (codepoint >> 18u));
        encoded[1] = (char)(0x80u | ((codepoint >> 12u) & 0x3fu));
        encoded[2] = (char)(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[3] = (char)(0x80u | (codepoint & 0x3fu)); width = 4u;
    }
    string_bytes_append(bytes, len, cap, encoded, width);
}

static int rd_string(Reader *r) {
    char *decoded = NULL;
    size_t len = 0, cap = 0;
    r->p++;
    while (r->p < r->end) {
        unsigned char byte = (unsigned char)*r->p;
        if (byte == '"') {
            r->p++;
            if (len > INT_MAX) fail_resource("string term exceeds INT_MAX bytes");
            int id = intern(decoded ? decoded : "", (int)len);
            free(decoded);
            return newnode(T_STR, id, 0, NULL);
        }
        if (byte == '\\') {
            r->p++;
            if (r->p >= r->end) break;
            unsigned char escape = (unsigned char)*r->p++;
            uint32_t codepoint;
            switch (escape) {
            case '"': codepoint = '"'; break;
            case '\\': codepoint = '\\'; break;
            case '/': codepoint = '/'; break;
            case 'b': codepoint = 0x08u; break;
            case 'f': codepoint = 0x0cu; break;
            case 'n': codepoint = 0x0au; break;
            case 'r': codepoint = 0x0du; break;
            case 't': codepoint = 0x09u; break;
            case 'u': {
                if (!rd_hex4(r, &codepoint)) { free(decoded); return -1; }
                if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
                    if ((size_t)(r->end - r->p) < 6u || r->p[0] != '\\' ||
                        r->p[1] != 'u') {
                        reader_fail(r); free(decoded); return -1;
                    }
                    r->p += 2;
                    uint32_t low;
                    if (!rd_hex4(r, &low)) { free(decoded); return -1; }
                    if (low < 0xdc00u || low > 0xdfffu) {
                        reader_fail(r); free(decoded); return -1;
                    }
                    codepoint = 0x10000u +
                                ((codepoint - 0xd800u) << 10u) +
                                (low - 0xdc00u);
                } else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
                    reader_fail(r); free(decoded); return -1;
                }
                break;
            }
            default:
                reader_fail(r); free(decoded); return -1;
            }
            string_codepoint_append(&decoded, &len, &cap, codepoint);
            continue;
        }
        uint32_t codepoint = 0;
        size_t width = 0;
        size_t remaining = (size_t)(r->end - r->p);
        if (!utf8_decode_one((const unsigned char *)r->p, remaining, 0,
                             &codepoint, &width) || codepoint < 0x20u) {
            reader_fail(r); free(decoded); return -1;
        }
        string_bytes_append(&decoded, &len, &cap, r->p, width);
        r->p += width;
    }
    reader_fail(r);
    free(decoded);
    return -1;
}

static int rd_term(Reader *r, int *varmap, int *nvars) {
    skipws(r);
    if (r->failed || r->p >= r->end) return -1;
    if (*r->p == '(') {
        r->p++;
        int cap = 8, n = 0, closed = 0;
        int *kids = malloc((size_t)cap * sizeof(int));
        if (!kids) { fprintf(stderr, "oom reader children\n"); exit(2); }
        for (;;) {
            skipws(r);
            if (r->failed || r->p >= r->end) break;
            if (*r->p == ')') { r->p++; closed = 1; break; }
            int t = rd_term(r, varmap, nvars);
            if (t < 0) break;
            if (n == cap) {
                if (cap > INT_MAX / 2) fail_resource("reader list too large");
                cap *= 2;
                kids = realloc(kids, (size_t)cap * sizeof(int));
                if (!kids) { fprintf(stderr, "oom reader children\n"); exit(2); }
            }
            kids[n++] = t;
        }
        if (!closed) {
            reader_fail(r);
            free(kids);
            return -1;
        }
        if (n == 0) {
            free(kids);
            return newnode(T_SYM, intern("()", 2), 0, NULL);
        }
        /* (f a b) -> APP with sym = head if head is a symbol */
        if (nodes[kids[0]].tag == T_SYM && nodes[kids[0]].nkids == 0) {
            int h = nodes[kids[0]].sym;
            int *rest = malloc((size_t)(n > 1 ? n - 1 : 1) * sizeof(int));
            if (!rest) { fprintf(stderr, "oom reader application\n"); exit(2); }
            for (int i = 1; i < n; i++) rest[i - 1] = kids[i];
            free(kids);
            return newnode(T_APP, h, n - 1, rest);
        }
        return newnode(T_APP, intern("#list", 5), n, kids);
    }
    if (*r->p == '"') return rd_string(r);
    if (*r->p == ')') { reader_fail(r); return -1; }
    return rd_atomtok(r, varmap, nvars);
}

/* ------------------------------------------------------------ bindings */
#define MAXVAR 4096
static int *bind_;            /* binding array, indexed by global var slot */
static int bindcap = 0;
static int *trail; static int ntrail = 0, captrail = 0;

static void ensure_bind(int n) {
    if (n <= bindcap) return;
    int old = bindcap;
    bindcap = bindcap ? bindcap * 2 : 65536;
    while (bindcap < n) bindcap *= 2;
    bind_ = realloc(bind_, (size_t)bindcap * sizeof(int));
    if (!bind_) { fprintf(stderr, "oom bindings\n"); exit(2); }
    for (int i = old; i < bindcap; i++) bind_[i] = -1;
}
static void tpush(int v) {
    if (ntrail == captrail) { captrail = captrail ? captrail * 2 : 4096;
        trail = realloc(trail, (size_t)captrail * sizeof(int));
        if (!trail) { fprintf(stderr, "oom trail\n"); exit(2); } }
    trail[ntrail++] = v;
}
static void undo(int mark) { while (ntrail > mark) bind_[trail[--ntrail]] = -1; }

static int deref(int t) {
    while (t >= 0 && t < nnodes && nodes[t].tag == T_VAR) {
        int v = nodes[t].sym;
        if (v < 0 || v >= bindcap) return t;
        int b = bind_[v];
        if (b < 0) return t;
        t = b;
    }
    if (t < 0 || t >= nnodes) fail_resource("invalid TermId during dereference");
    return t;
}

/* Occurs checking is required for the finite-tree Horn semantics.  The term
 * arena itself is acyclic; only the temporary binding environment could form
 * a rational tree, so checking before binding is sufficient. */
static int occurs_in(int var, int t) {
    t = deref(t);
    if (nodes[t].tag == T_VAR) return nodes[t].sym == var;
    if (nodes[t].tag != T_APP || !(nodes[t].flags & NF_HAS_VAR)) return 0;
    for (int i = 0; i < nodes[t].nkids; i++)
        if (occurs_in(var, KID(t, i))) return 1;
    return 0;
}

static int bind_var(int var, int term) {
    ensure_bind(var + 1);
    term = deref(term);
    if (nodes[term].tag == T_VAR && nodes[term].sym == var) return 1;
    if (occurs_in(var, term)) return 0;
    bind_[var] = term;
    tpush(var);
    return 1;
}

static int unify(int a, int b) {
    a = deref(a); b = deref(b);
    if (a == b) return 1;
    if (nodes[a].tag == T_VAR) return bind_var(nodes[a].sym, b);
    if (nodes[b].tag == T_VAR) return bind_var(nodes[b].sym, a);
    if (nodes[a].tag != nodes[b].tag) return 0;
    if (nodes[a].tag != T_APP) return nodes[a].sym == nodes[b].sym;
    if (nodes[a].sym != nodes[b].sym || nodes[a].nkids != nodes[b].nkids) return 0;
    for (int i = 0; i < nodes[a].nkids; i++)
        if (!unify(KID(a, i), KID(b, i))) return 0;
    return 1;
}

/* resolve a term under the current bindings into a fresh ground-ish term */
static int resolve(int t) {
    t = deref(t);
    if (nodes[t].tag != T_APP || !(nodes[t].flags & NF_HAS_VAR)) return t;
    int n = nodes[t].nkids;
    int *kids = malloc((size_t)(n ? n : 1) * sizeof(int));
    if (!kids) { fprintf(stderr, "oom term reconstruction\n"); exit(2); }
    int changed = 0;
    for (int i = 0; i < n; i++) {
        kids[i] = resolve(KID(t, i));
        if (kids[i] != KID(t, i)) changed = 1;
    }
    if (!changed) { free(kids); return t; }
    return newnode(T_APP, nodes[t].sym, n, kids);
}

/* rename a rule's variables into a fresh global block */
static int varbase = 0;
static int rename_t(int t, int base) {
    if (nodes[t].tag == T_VAR) {
        if (nodes[t].sym < 0 || base > INT_MAX - nodes[t].sym) fail_resource("variable namespace overflow");
        return newnode(T_VAR, base + nodes[t].sym, 0, NULL);
    }
    if (nodes[t].tag != T_APP || !(nodes[t].flags & NF_HAS_VAR)) return t;
    int n = nodes[t].nkids;
    int *kids = malloc((size_t)(n ? n : 1) * sizeof(int));
    if (!kids) { fprintf(stderr, "oom variable renaming\n"); exit(2); }
    for (int i = 0; i < n; i++) kids[i] = rename_t(KID(t, i), base);
    return newnode(T_APP, nodes[t].sym, n, kids);
}

/* ------------------------------------------------------------- printing */
static void sprt_reserve(char **buf, int len, int *cap, int extra) {
    if (extra < 0 || len > INT_MAX - extra) fail_resource("rendered term too large");
    int need = len + extra;
    if (need <= *cap) return;
    int next_cap = *cap ? *cap : 256;
    while (next_cap < need) {
        if (next_cap > INT_MAX / 2) next_cap = need;
        else next_cap *= 2;
    }
    *buf = realloc(*buf, (size_t)next_cap);
    if (!*buf) { fprintf(stderr, "oom rendered term\n"); exit(2); }
    *cap = next_cap;
}

static void sprt_bytes(char **buf, int *len, int *cap,
                       const char *bytes, size_t byte_len) {
    if (byte_len > (size_t)INT_MAX - 1u)
        fail_resource("rendered atom exceeds INT_MAX bytes");
    sprt_reserve(buf, *len, cap, (int)byte_len + 1);
    if (byte_len) memcpy(*buf + *len, bytes, byte_len);
    *len += (int)byte_len;
}

static void sprt_char(char **buf, int *len, int *cap, char byte) {
    sprt_reserve(buf, *len, cap, 2);
    (*buf)[(*len)++] = byte;
}

static void sprt_string(char **buf, int *len, int *cap, int text_id) {
    const char *bytes = symbytes(text_id);
    size_t byte_len = symlen(text_id);
    size_t pos = 0;
    sprt_char(buf, len, cap, '"');
    while (pos < byte_len) {
        uint32_t codepoint = 0;
        size_t width = 0;
        if (!utf8_decode_one((const unsigned char *)bytes, byte_len, pos,
                             &codepoint, &width)) {
            fail_resource("invalid stored string UTF-8");
        }
        const char *escape = NULL;
        switch (codepoint) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case 0x08u: escape = "\\b"; break;
        case 0x0cu: escape = "\\f"; break;
        case 0x0au: escape = "\\n"; break;
        case 0x0du: escape = "\\r"; break;
        case 0x09u: escape = "\\t"; break;
        default: break;
        }
        if (escape) {
            sprt_bytes(buf, len, cap, escape, strlen(escape));
        } else if (codepoint < 0x20u) {
            char escaped[7];
            int written = snprintf(escaped, sizeof escaped, "\\u%04x", codepoint);
            sprt_bytes(buf, len, cap, escaped, (size_t)written);
        } else {
            sprt_bytes(buf, len, cap, bytes + pos, width);
        }
        pos += width;
    }
    sprt_char(buf, len, cap, '"');
}

static void sprt(char **buf, int *len, int *cap, int t) {
    char tmp[64];
    t = deref(t);
    switch (nodes[t].tag) {
    case T_SYM:
    case T_BIGINT:
        sprt_bytes(buf, len, cap, symbytes(nodes[t].sym), symlen(nodes[t].sym));
        return;
    case T_STR:
        sprt_string(buf, len, cap, nodes[t].sym);
        return;
    case T_INT:
        (void)snprintf(tmp, sizeof tmp, "%d", nodes[t].sym);
        sprt_bytes(buf, len, cap, tmp, strlen(tmp));
        return;
    case T_VAR:
        (void)snprintf(tmp, sizeof tmp, "_%d", nodes[t].sym);
        sprt_bytes(buf, len, cap, tmp, strlen(tmp));
        return;
    case T_APP: {
        sprt_char(buf, len, cap, '(');
        sprt_bytes(buf, len, cap,
                   symbytes(nodes[t].sym), symlen(nodes[t].sym));
        for (int i = 0; i < nodes[t].nkids; i++) {
            sprt_char(buf, len, cap, ' ');
            sprt(buf, len, cap, KID(t, i));
        }
        sprt_char(buf, len, cap, ')');
        return; }
    }
    fail_resource("unknown term tag during rendering");
}
static char *tostr(int t) {
    char *b = NULL; int len = 0, cap = 0;
    sprt(&b, &len, &cap, t);
    sprt_reserve(&b, len, &cap, 1);
    b[len] = 0;
    return b;
}


/* Canonical alpha-variant term.  Variables are renumbered by first
 * occurrence and the resulting immutable structure is hash-consed.  Thus a
 * chart key is a TermId, not an allocated serialization. */
static int ck_map[1 << 16];
static int ck_n;

static int canonical_rec(int t) {
    t = deref(t);
    if (nodes[t].tag == T_VAR) {
        int v = nodes[t].sym;
        for (int i = 0; i < ck_n; i++)
            if (ck_map[i] == v) return newnode(T_VAR, i, 0, NULL);
        if (ck_n >= (1 << 16)) fail_resource("more than 65536 variables in one chart key");
        int slot = ck_n;
        ck_map[ck_n++] = v;
        return newnode(T_VAR, slot, 0, NULL);
    }
    if (nodes[t].tag != T_APP || !(nodes[t].flags & NF_HAS_VAR)) return t;
    int n = nodes[t].nkids;
    int *kids = malloc((size_t)(n ? n : 1) * sizeof(int));
    if (!kids) { fprintf(stderr, "oom canonical term\n"); exit(2); }
    int changed = 0;
    for (int i = 0; i < n; i++) {
        kids[i] = canonical_rec(KID(t, i));
        if (kids[i] != KID(t, i)) changed = 1;
    }
    if (!changed) { free(kids); return t; }
    return newnode(T_APP, nodes[t].sym, n, kids);
}

static int canonical_term(int t, int *nvars) {
    ck_n = 0;
    int out = canonical_rec(t);
    if (nvars) *nvars = ck_n;
    return out;
}

/* ---------------------------------------------------------------- rules */
typedef struct {
    int id;
    int head;
    int nbody;
    int *body;
    int nvars;
    int hsym;
    int harity;
    int next_same_head;
} Rule;
static Rule *rules = NULL; static int nrules = 0, caprules = 0;
static int *rule_id_seen = NULL, rule_id_seen_cap = 0;
static char presentation_error[512];

static void set_presentation_error(const char *message) {
    if (!presentation_error[0]) {
        snprintf(presentation_error, sizeof presentation_error, "%s", message);
    }
}

static int presentation_text_well_formed(const char *text, size_t len) {
    int depth = 0, quoted = 0, escaped = 0, comment = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (comment) { if (c == '\n') comment = 0; continue; }
        if (quoted) {
            if (escaped) { escaped = 0; continue; }
            if (c == '\\') { escaped = 1; continue; }
            if (c == '"') quoted = 0;
            continue;
        }
        if (c == ';') { comment = 1; continue; }
        if (c == '"') { quoted = 1; continue; }
        if (c == '(') depth++;
        else if (c == ')' && --depth < 0) return 0;
    }
    return depth == 0 && !quoted;
}

/* Generic rule-head index.  The presentation still supplies all semantics;
 * this merely avoids trying every unrelated Horn rule for every chart goal. */
#define RULE_HSZ (1u << 16)
static int *rule_heads = NULL;
static unsigned rule_hash(int sym, int arity) {
    uint32_t x = (uint32_t)sym * 2654435761u;
    x ^= (uint32_t)arity * 2246822519u;
    return x & (RULE_HSZ - 1u);
}
static void rule_index_init(void) {
    rule_heads = malloc(RULE_HSZ * sizeof(int));
    if (!rule_heads) { fprintf(stderr, "oom rule index\n"); exit(2); }
    for (unsigned i = 0; i < RULE_HSZ; i++) rule_heads[i] = -1;
}
static void addrule(int id, int head, int nbody, int *body, int nvars) {
    if (id < 0) { set_presentation_error("rule id is not a symbol"); return; }
    if (id >= rule_id_seen_cap) {
        int old = rule_id_seen_cap;
        rule_id_seen_cap = rule_id_seen_cap ? rule_id_seen_cap * 2 : 2048;
        while (rule_id_seen_cap <= id) rule_id_seen_cap *= 2;
        rule_id_seen = realloc(rule_id_seen,
                               (size_t)rule_id_seen_cap * sizeof(int));
        if (!rule_id_seen) { fprintf(stderr, "oom rule-id index\n"); exit(2); }
        for (int i = old; i < rule_id_seen_cap; i++) rule_id_seen[i] = -1;
    }
    if (rule_id_seen[id] >= 0) {
        char message[512];
        snprintf(message, sizeof message, "duplicate rule id: %s", symname(id));
        set_presentation_error(message);
        return;
    }
    rule_id_seen[id] = nrules;
    if (nrules == caprules) { caprules = caprules ? caprules * 2 : 256;
        rules = realloc(rules, (size_t)caprules * sizeof(Rule)); }
    int hsym = -1, harity = -1;
    if (nodes[head].tag == T_APP) {
        hsym = nodes[head].sym;
        harity = nodes[head].nkids;
    } else if (nodes[head].tag == T_SYM) {
        hsym = nodes[head].sym;
        harity = 0;
    }
    rules[nrules].id = id; rules[nrules].head = head;
    rules[nrules].nbody = nbody; rules[nrules].body = body;
    rules[nrules].nvars = nvars;
    rules[nrules].hsym = hsym; rules[nrules].harity = harity;
    rules[nrules].next_same_head = -1;
    if (hsym >= 0) {
        unsigned slot = rule_hash(hsym, harity);
        rules[nrules].next_same_head = rule_heads[slot];
        rule_heads[slot] = nrules;
    }
    nrules++;
}

/* Compact a rule's variables into a local 0..n-1 range.  Source variables are
 * scoped to their own rule; numbering them per file overflows and wastes slots. */
static int cv_from[65536], cv_to[65536]; static int cv_n;
static int compact(int t) {
    if (nodes[t].tag == T_VAR) {
        for (int i = 0; i < cv_n; i++) if (cv_from[i] == nodes[t].sym) return cv_to[i];
        if (cv_n >= 65536) fail_resource("more than 65536 variables in one source rule");
        cv_from[cv_n] = nodes[t].sym;
        cv_to[cv_n] = newnode(T_VAR, cv_n, 0, NULL);
        return cv_to[cv_n++];
    }
    if (nodes[t].tag != T_APP) return t;
    int n = nodes[t].nkids;
    int *kids = malloc((size_t)(n ? n : 1) * sizeof(int));
    if (!kids) { fprintf(stderr, "oom variable compaction\n"); exit(2); }
    for (int i = 0; i < n; i++) kids[i] = compact(KID(t, i));
    return newnode(T_APP, nodes[t].sym, n, kids);
}

/* ------------------------------------------------------- chart / answers
 * Goal and answer identity is indexed by canonical TermIds.  Packed forest
 * alternatives and adjacency lists live in append-only integer arenas: no
 * per-edge malloc, no linked C pointers, and no serialized term keys.
 */
typedef struct {
    uint64_t hash;
    int a, b, value;
} IdSlot;
typedef struct {
    IdSlot *slots;
    uint32_t cap, len;
} IdMap;

static void idmap_init(IdMap *m, uint32_t want) {
    uint32_t cap = 1024;
    while (cap < want) cap <<= 1;
    m->slots = malloc((size_t)cap * sizeof(IdSlot));
    if (!m->slots) { fprintf(stderr, "oom id map\n"); exit(2); }
    for (uint32_t i = 0; i < cap; i++) m->slots[i].value = -1;
    m->cap = cap; m->len = 0;
}
static uint64_t idmap_hash(int a, int b) {
    return mix64((uint64_t)(uint32_t)a ^
                 ((uint64_t)(uint32_t)b << 32) ^
                 UINT64_C(0xd6e8feb86659fd93));
}
static void idmap_rehash(IdMap *m, uint32_t cap) {
    IdSlot *old = m->slots; uint32_t oldcap = m->cap;
    m->slots = malloc((size_t)cap * sizeof(IdSlot));
    if (!m->slots) { fprintf(stderr, "oom id map rehash\n"); exit(2); }
    for (uint32_t i = 0; i < cap; i++) m->slots[i].value = -1;
    m->cap = cap; m->len = 0;
    for (uint32_t i = 0; i < oldcap; i++) if (old[i].value >= 0) {
        uint32_t s = (uint32_t)old[i].hash & (cap - 1u);
        while (m->slots[s].value >= 0) s = (s + 1u) & (cap - 1u);
        m->slots[s] = old[i]; m->len++;
    }
    free(old);
}
static int idmap_get(const IdMap *m, int a, int b) {
    if (!m->cap) return -1;
    uint64_t h = idmap_hash(a, b);
    uint32_t s = (uint32_t)h & (m->cap - 1u);
    for (;;) {
        const IdSlot *q = &m->slots[s];
        if (q->value < 0) return -1;
        if (q->hash == h && q->a == a && q->b == b) return q->value;
        s = (s + 1u) & (m->cap - 1u);
    }
}
static void idmap_put(IdMap *m, int a, int b, int value) {
    if (!m->cap) idmap_init(m, 1024);
    if ((m->len + 1u) * 10u >= m->cap * 7u) idmap_rehash(m, m->cap << 1);
    uint64_t h = idmap_hash(a, b);
    uint32_t s = (uint32_t)h & (m->cap - 1u);
    while (m->slots[s].value >= 0) {
        if (m->slots[s].hash == h && m->slots[s].a == a && m->slots[s].b == b) {
            m->slots[s].value = value; return;
        }
        s = (s + 1u) & (m->cap - 1u);
    }
    m->slots[s] = (IdSlot){ h, a, b, value }; m->len++;
}

typedef struct { int value, next; } IntLink;
typedef struct {
    int ruleid;
    uint32_t child_off;
    uint16_t nkids;
    int next;
} PackedAlt;

typedef struct {
    int key;        /* canonical full instantiated goal */
    int term;       /* same canonical term; retained for clarity */
    int goalidx;
    int nvars;
    int first_alt, last_alt, nalt;
} Answer;

typedef struct {
    int key;        /* canonical variant TermId */
    int pattern;
    int answer_head, nans;
    int consumer_head, ncons;
    int dirty, queued;
} Goal;

static Goal *goals = NULL; static int ngoals = 0, capgoals = 0;
static Answer *ans = NULL; static int nans = 0, capans = 0;
static IntLink *answer_links = NULL; static int nanswer_links = 0, capanswer_links = 0;
static IntLink *consumer_links = NULL; static int nconsumer_links = 0, capconsumer_links = 0;
static PackedAlt *alts = NULL; static int nalts = 0, capalts = 0;
static int *alt_children = NULL; static uint32_t nalt_children = 0, capalt_children = 0;
static IdMap goal_map = {0}, answer_map = {0};
static int *agenda = NULL;
static int agenda_head = 0, agenda_tail = 0, agenda_cap = 0;

static void enqueue_goal(int g) {
    if (goals[g].queued) return;
    if (agenda_tail == agenda_cap) {
        agenda_cap = agenda_cap ? agenda_cap * 2 : 4096;
        agenda = realloc(agenda, (size_t)agenda_cap * sizeof(int));
        if (!agenda) { fprintf(stderr, "oom chart agenda\n"); exit(2); }
    }
    goals[g].queued = 1;
    agenda[agenda_tail++] = g;
}

static int push_link(IntLink **arena, int *n, int *cap, int value, int next) {
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 4096;
        *arena = realloc(*arena, (size_t)*cap * sizeof(IntLink));
        if (!*arena) { fprintf(stderr, "oom chart links\n"); exit(2); }
    }
    (*arena)[*n] = (IntLink){ value, next };
    return (*n)++;
}
static uint32_t append_alt_children(const int *kids, int nk) {
    uint32_t off = nalt_children;
    uint32_t need = nalt_children + (uint32_t)nk;
    if (need > capalt_children) {
        uint32_t cap = capalt_children ? capalt_children : 8192;
        while (cap < need) cap <<= 1;
        alt_children = realloc(alt_children, (size_t)cap * sizeof(int));
        if (!alt_children) { fprintf(stderr, "oom packed children\n"); exit(2); }
        capalt_children = cap;
    }
    if (nk) memcpy(alt_children + off, kids, (size_t)nk * sizeof(int));
    nalt_children = need;
    return off;
}

static void add_consumer(int producer, int consumer) {
    for (int l = goals[producer].consumer_head; l >= 0; l = consumer_links[l].next)
        if (consumer_links[l].value == consumer) return;
    goals[producer].consumer_head = push_link(&consumer_links, &nconsumer_links,
                                               &capconsumer_links, consumer,
                                               goals[producer].consumer_head);
    goals[producer].ncons++;
}
static void mark_consumers(int g) {
    for (int l = goals[g].consumer_head; l >= 0; l = consumer_links[l].next) {
        int c = consumer_links[l].value;
        goals[c].dirty = 1;
        enqueue_goal(c);
    }
}
static int goal_has_answer(int g, int a) {
    for (int l = goals[g].answer_head; l >= 0; l = answer_links[l].next)
        if (answer_links[l].value == a) return 1;
    return 0;
}
static void goal_link_answer(int g, int a) {
    if (goal_has_answer(g, a)) return;
    goals[g].answer_head = push_link(&answer_links, &nanswer_links,
                                     &capanswer_links, a, goals[g].answer_head);
    goals[g].nans++;
}
static int collect_goal_answers(int g, int *out) {
    int n = 0;
    for (int l = goals[g].answer_head; l >= 0; l = answer_links[l].next)
        out[n++] = answer_links[l].value;
    return n;
}

static long derivations = 0;

static int demand(int pattern) {
    int k = canonical_term(pattern, NULL);
    int g = idmap_get(&goal_map, k, 0);
    if (g >= 0) return g;
    if (ngoals == capgoals) {
        capgoals = capgoals ? capgoals * 2 : 1024;
        goals = realloc(goals, (size_t)capgoals * sizeof(Goal));
        if (!goals) { fprintf(stderr, "oom goals\n"); exit(2); }
    }
    goals[ngoals] = (Goal){
        .key = k, .pattern = k,
        .answer_head = -1, .nans = 0,
        .consumer_head = -1, .ncons = 0,
        .dirty = 1, .queued = 0
    };
    idmap_put(&goal_map, k, 0, ngoals);
    int out = ngoals++;
    enqueue_goal(out);
    return out;
}

static int same_alt(int alt, int ruleid, int nk, const int *kids) {
    if (alts[alt].ruleid != ruleid || alts[alt].nkids != nk) return 0;
    for (int i = 0; i < nk; i++)
        if (alt_children[alts[alt].child_off + (uint32_t)i] != kids[i]) return 0;
    return 1;
}
static void add_packing(int a, int ruleid, int nk, const int *kids) {
    if (nk < 0 || nk > UINT16_MAX) fail_resource("packed alternative has more than 65535 children");
    for (int d = ans[a].first_alt; d >= 0; d = alts[d].next)
        if (same_alt(d, ruleid, nk, kids)) return;
    if (nalts == capalts) {
        capalts = capalts ? capalts * 2 : 4096;
        alts = realloc(alts, (size_t)capalts * sizeof(PackedAlt));
        if (!alts) { fprintf(stderr, "oom packed alternatives\n"); exit(2); }
    }
    uint32_t off = append_alt_children(kids, nk);
    int d = nalts++;
    alts[d] = (PackedAlt){ ruleid, off, (uint16_t)nk, -1 };
    if (ans[a].last_alt >= 0) alts[ans[a].last_alt].next = d;
    else ans[a].first_alt = d;
    ans[a].last_alt = d; ans[a].nalt++;
}

static void add_answer(int g, int term, int ruleid, int nk, const int *kids) {
    int nvars = 0;
    int key = canonical_term(term, &nvars);
    int a = idmap_get(&answer_map, g, key);
    if (a < 0) {
        if (nans == capans) {
            capans = capans ? capans * 2 : 4096;
            ans = realloc(ans, (size_t)capans * sizeof(Answer));
            if (!ans) { fprintf(stderr, "oom answers\n"); exit(2); }
        }
        a = nans++;
        ans[a] = (Answer){
            .key = key, .term = key, .goalidx = g, .nvars = nvars,
            .first_alt = -1, .last_alt = -1, .nalt = 0
        };
        idmap_put(&answer_map, g, key, a);
        goal_link_answer(g, a);
        mark_consumers(g);
    }
    add_packing(a, ruleid, nk, kids);
}

/* solve body goals left-to-right against the table.  Stored answer variables
 * are canonical 0..n-1 and are freshened into a temporary range before use. */
static void solve_body(int g, int headterm, int ruleid, Rule *r, int base,
                       int i, int *kids, int fresh_top) {
    if (i == r->nbody) {
        derivations++;
        add_answer(g, headterm, ruleid, r->nbody, kids);
        return;
    }
    int sub = resolve(rename_t(r->body[i], base));
    int sg = demand(sub);
    add_consumer(sg, g);
    int first = goals[sg].answer_head; /* snapshot; new answers wait for next round */
    for (int l = first; l >= 0; l = answer_links[l].next) {
        int a = answer_links[l].value;
        int mark = ntrail;
        if (ans[a].nvars > 0) {
            if (fresh_top > INT_MAX - ans[a].nvars - 2) fail_resource("variable namespace overflow");
            ensure_bind(fresh_top + ans[a].nvars + 2);
        }
        int at = ans[a].nvars ? rename_t(ans[a].term, fresh_top) : ans[a].term;
        if (unify(sub, at)) {
            kids[i] = a;
            solve_body(g, headterm, ruleid, r, base, i + 1, kids,
                       fresh_top + ans[a].nvars + 1);
        }
        undo(mark);
    }
}

static long node_limit = 40000000;
static int oom_hit = 0;
static int sym_different, sym_intrinsic_ground_different;
static int ground_different_enabled = 0;
#define CHART_VAR_BASE (1 << 20)
static void process_goal(int g) {
    if (!goals[g].dirty) return;
    goals[g].dirty = 0;
    if (nnodes > node_limit) { oom_hit = 1; return; }
    int pat = goals[g].pattern;
    int psym = -1, parity = -1;
    if (nodes[pat].tag == T_APP) { psym = nodes[pat].sym; parity = nodes[pat].nkids; }
    else if (nodes[pat].tag == T_SYM) { psym = nodes[pat].sym; parity = 0; }
    if (ground_different_enabled && psym == sym_different && parity == 2) {
        int left = resolve(KID(pat, 0));
        int right = resolve(KID(pat, 1));
        if (!(nodes[left].flags & NF_HAS_VAR) &&
            !(nodes[right].flags & NF_HAS_VAR) && left != right)
            add_answer(g, pat, sym_intrinsic_ground_different, 0, NULL);
    }
    int ri = (psym >= 0) ? rule_heads[rule_hash(psym, parity)] : -1;
    for (; ri >= 0; ri = rules[ri].next_same_head) {
        Rule *r = &rules[ri];
        if (r->hsym != psym || r->harity != parity) continue;
        int mark = ntrail;
        int base = CHART_VAR_BASE;
        int fresh_top = base + r->nvars + 1;
        ensure_bind(fresh_top + 65536);
        int h = rename_t(r->head, base);
        if (unify(h, pat)) {
            int *kids = malloc((size_t)(r->nbody ? r->nbody : 1) * sizeof(int));
            if (!kids) { fprintf(stderr, "oom derivation scratch\n"); exit(2); }
            solve_body(g, h, r->id, r, base, 0, kids, fresh_top);
            free(kids);
        }
        undo(mark);
    }
}

/* ------------------------------------------------------------ presentation */
static int sym_rule, sym_head, sym_body;

static void load_forms(const char *text, size_t len) {
    Reader r = { text, text + len, 0 };
    for (;;) {
        skipws(&r);
        if (r.failed) {
            set_presentation_error("executable term reader rejected presentation bytes");
            return;
        }
        if (r.p >= r.end) break;
        static int *varmap = NULL; if (!varmap) varmap = malloc(sizeof(int) * 4000000);
        int nv = 0;
        int t = rd_term(&r, varmap, &nv);
        if (t < 0 || r.failed) {
            set_presentation_error("executable term reader rejected presentation term");
            return;
        }
        /* walk for (rule id (head H) (body G...)) anywhere in the form */
        /* iterative stack walk */
        static int *stack = NULL; static int stkcap = 0;
        int sp = 0;
        if (!stack) { stkcap = 1 << 16; stack = malloc((size_t)stkcap * sizeof(int)); }
        stack[sp++] = t;
        while (sp) {
            int c = stack[--sp];
            if (nodes[c].tag == T_APP) {
                if (nodes[c].sym == sym_rule) {
                    if (nodes[c].nkids != 3) {
                        set_presentation_error("rule must have id, head, and body fields");
                        return;
                    }
                    int idt = KID(c, 0);
                    int hd = KID(c, 1), bd = KID(c, 2);
                    if (nodes[idt].tag != T_SYM ||
                        nodes[hd].tag != T_APP || nodes[hd].sym != sym_head ||
                        nodes[hd].nkids != 1 || nodes[bd].tag != T_APP ||
                        nodes[bd].sym != sym_body) {
                        set_presentation_error("malformed rule id/head/body field");
                        return;
                    }
                    int nb = nodes[bd].nkids;
                    int *body = malloc((size_t)(nb ? nb : 1) * sizeof(int));
                    cv_n = 0;
                    int chead = compact(KID(hd, 0));
                    for (int i = 0; i < nb; i++) body[i] = compact(KID(bd, i));
                    addrule(nodes[idt].sym, chead, nb, body, cv_n);
                    if (presentation_error[0]) return;
                    continue;
                }
                for (int i = 0; i < nodes[c].nkids; i++) {
                    if (sp + 2 >= stkcap) { stkcap *= 2; stack = realloc(stack, (size_t)stkcap * sizeof(int)); }
                    stack[sp++] = KID(c, i);
                }
            }
        }
    }
}

/* ---------------------------------------------------------------- input
 * Every input suffix is itself an interned immutable term.  A direct TermId
 * map records its byte position; this replaces the previous O(n^2) array of
 * serialized suffix strings and the linear position search used by every
 * certificate node. */
static IdMap suffix_pos_map = {0};
static int nsuffix = 0;
static char input_error[160];

static int posof(int t) {
    if (!suffix_pos_map.cap) return -1;
    t = resolve(t);
    return idmap_get(&suffix_pos_map, t, 0);
}
static int utf8_decode_one(const unsigned char *bytes, size_t len, size_t pos,
                           uint32_t *codepoint, size_t *width) {
    if (pos >= len) return 0;
    unsigned char first = bytes[pos];
    if (first <= 0x7fu) {
        *codepoint = first;
        *width = 1;
        return 1;
    }
    uint32_t value;
    size_t count;
    uint32_t minimum;
    if (first >= 0xc2u && first <= 0xdfu) {
        value = (uint32_t)(first & 0x1fu);
        count = 2;
        minimum = 0x80u;
    } else if (first >= 0xe0u && first <= 0xefu) {
        value = (uint32_t)(first & 0x0fu);
        count = 3;
        minimum = 0x800u;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        value = (uint32_t)(first & 0x07u);
        count = 4;
        minimum = 0x10000u;
    } else {
        return 0;
    }
    if (count > len - pos) return 0;
    for (size_t index = 1; index < count; index++) {
        unsigned char continuation = bytes[pos + index];
        if ((continuation & 0xc0u) != 0x80u) return 0;
        value = (value << 6u) | (uint32_t)(continuation & 0x3fu);
    }
    if (value < minimum || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu))
        return 0;
    *codepoint = value;
    *width = count;
    return 1;
}

static int encode_codepoints(const char *s, size_t n) {
    uint32_t *codepoints = malloc((n ? n : 1u) * sizeof(uint32_t));
    size_t *offsets = malloc((n ? n : 1u) * sizeof(size_t));
    if (!codepoints || !offsets) fail_resource("UTF-8 input index");
    size_t count = 0;
    size_t pos = 0;
    while (pos < n) {
        size_t width = 0;
        uint32_t codepoint = 0;
        if (!utf8_decode_one((const unsigned char *)s, n, pos,
                             &codepoint, &width)) {
            snprintf(input_error, sizeof input_error,
                     "invalid UTF-8 at byte offset %zu", pos);
            free(codepoints);
            free(offsets);
            return -1;
        }
        offsets[count] = pos;
        codepoints[count] = codepoint;
        count++;
        pos += width;
    }
    idmap_init(&suffix_pos_map, (uint32_t)(2 * count + 16));
    nsuffix = (int)count + 1;
    int lst = newnode(T_SYM, intern("nil", 3), 0, NULL);
    idmap_put(&suffix_pos_map, lst, 0, (int)n);
    for (size_t i = count; i > 0; i--) {
        int *cpk = malloc(sizeof(int));
        cpk[0] = newnode(T_INT, (int32_t)codepoints[i - 1], 0, NULL);
        int cp = newnode(T_APP, intern("cp", 2), 1, cpk);
        int *ck = malloc(2 * sizeof(int));
        ck[0] = cp; ck[1] = lst;
        lst = newnode(T_APP, intern("cons", 4), 2, ck);
        idmap_put(&suffix_pos_map, lst, 0, (int)offsets[i - 1]);
    }
    free(codepoints);
    free(offsets);
    return lst;
}

/* --------------------------------------------------------- certificates */
static void span_of(int a, int *s, int *e) {
    int g = ans[a].term; *s = -1; *e = -1;
    if (nodes[g].tag == T_APP && nodes[g].nkids >= 4) {
        *s = posof(KID(g, 1));
        *e = posof(KID(g, 3));
    }
}
static void print_cert(FILE *f, int a, int depth) {
    int d = ans[a].first_alt;
    int s, e; span_of(a, &s, &e);
    if (d < 0) { fputs("(cert ? -1 -1 ())", f); return; }
    if (depth > 16384) {
        fprintf(stderr, "certificate depth exceeds 16384; refusing a truncated certificate\n");
        exit(2);
    }
    fprintf(f, "(cert %s %d %d (", symname(alts[d].ruleid), s, e);
    for (int i = 0; i < alts[d].nkids; i++) {
        if (i) fputc(' ', f);
        print_cert(f, alt_children[alts[d].child_off + (uint32_t)i], depth + 1);
    }
    fputs("))", f);
}

/* --------------------------------------------------------------- replay
 * Re-derive a certificate from the admitted rules.  This does not compare
 * strings: it re-applies each cited rule, checks the span it claims, and
 * recurses into the body goals.  Any mutated field breaks one of those checks.
 */
static int sym_cert;
static int replay(int cert, int goal, char *why, size_t wn) {
    cert = deref(cert);
    if (nodes[cert].tag != T_APP || nodes[cert].sym != sym_cert || nodes[cert].nkids != 4) {
        snprintf(why, wn, "malformed certificate node"); return 0; }
    int idn = deref(KID(cert, 0));
    int stn = deref(KID(cert, 1));
    int enn = deref(KID(cert, 2));
    int kidsn = deref(KID(cert, 3));
    if (nodes[idn].tag != T_SYM) { snprintf(why, wn, "rule id not a symbol"); return 0; }
    int want_start = (nodes[stn].tag == T_INT) ? nodes[stn].sym : -1;
    int want_end   = (nodes[enn].tag == T_INT) ? nodes[enn].sym : -1;

    int nk = (nodes[kidsn].tag == T_APP) ? nodes[kidsn].nkids : 0;
    int *kidv = NULL;
    if (nk) {
        kidv = malloc((size_t)nk * sizeof(int));
        /* the kids list is parsed as (#list c1 c2 ...) or (cert ...) if single */
        if (nodes[kidsn].sym == sym_cert) { nk = 1; kidv[0] = kidsn; }
        else for (int i = 0; i < nk; i++) kidv[i] = KID(kidsn, i);
    }

    if (nodes[idn].sym == sym_intrinsic_ground_different) {
        int resolved_goal = resolve(goal);
        int ok = ground_different_enabled && nk == 0 &&
                 want_start == -1 && want_end == -1 &&
                 nodes[resolved_goal].tag == T_APP &&
                 nodes[resolved_goal].sym == sym_different &&
                 nodes[resolved_goal].nkids == 2;
        if (ok) {
            int left = resolve(KID(resolved_goal, 0));
            int right = resolve(KID(resolved_goal, 1));
            ok = !(nodes[left].flags & NF_HAS_VAR) &&
                 !(nodes[right].flags & NF_HAS_VAR) && left != right;
        }
        if (!ok)
            snprintf(why, wn, "invalid declared ground-different certificate");
        free(kidv);
        return ok;
    }

    for (int ri = 0; ri < nrules; ri++) {
        if (rules[ri].id != nodes[idn].sym) continue;
        int mark = ntrail;
        int base = varbase; varbase += rules[ri].nvars + 1;
        ensure_bind(varbase + 1);
        int h = rename_t(rules[ri].head, base);
        if (!unify(h, goal)) { undo(mark); continue; }
        /* A Horn rule may bind its output/remainder only through its body.
         * Check child proofs first, then verify the span of the fully
         * instantiated conclusion.  Checking here, before the premises, would
         * reject honest certificates whose end position is still a rule-local
         * variable at rule entry. */
        if (nk != rules[ri].nbody) {
            snprintf(why, wn, "arity mismatch for %s: certificate has %d children, rule has %d",
                     symname(nodes[idn].sym), nk, rules[ri].nbody);
            undo(mark); free(kidv); return 0; }
        int ok = 1;
        for (int i = 0; i < rules[ri].nbody && ok; i++) {
            int sub = resolve(rename_t(rules[ri].body[i], base));
            if (!replay(kidv[i], sub, why, wn)) ok = 0;
        }
        if (ok) {
            int gg = resolve(goal);
            int s = -1, e = -1;
            if (nodes[gg].tag == T_APP && nodes[gg].nkids >= 4) {
                s = posof(KID(gg, 1));
                e = posof(KID(gg, 3));
            }
            /* Spans are checked for EVERY node.  A goal with no input span
             * must claim -1/-1; otherwise an unchecked field could be forged. */
            if (s != want_start || e != want_end) {
                snprintf(why, wn,
                         "span mismatch: claimed %d..%d, actual %d..%d",
                         want_start, want_end, s, e);
                ok = 0;
            }
        }
        if (ok) { free(kidv); return 1; }
        undo(mark);
        free(kidv);
        return 0;
    }
    snprintf(why, wn, "no admitted rule named %s applies here", symname(nodes[idn].sym));
    free(kidv);
    return 0;
}


/* ------------------------------------------------------ direct interpreter
 * A deliberately simple, non-tabled Horn interpreter used only as an
 * independent semantic falsifier.  It reads the same admitted rules as the
 * packed chart but does not consult Goal/Answer tables or packed derivations.
 * Productive left recursion is therefore outside this mode's completeness
 * envelope; input-consuming reader presentations terminate in this mode.
 */
static char **direct_keys = NULL;
static int ndirect = 0, capdirect = 0;
static long direct_steps = 0, direct_step_limit = 10000000;
static int direct_depth_limit = 20000;
static double direct_timeout = 600.0;
static clock_t direct_t0;
static int direct_stop = 0; /* 0 none, 1 timeout, 2 step, 3 depth */

static void direct_record(int topgoal) {
    int at = resolve(topgoal);
    int val = (nodes[at].tag == T_APP && nodes[at].nkids >= 3) ? KID(at, 2) : at;
    char *key = tostr(val);
    for (int i = 0; i < ndirect; i++) {
        if (!strcmp(direct_keys[i], key)) { free(key); return; }
    }
    if (ndirect == capdirect) {
        capdirect = capdirect ? capdirect * 2 : 16;
        direct_keys = realloc(direct_keys, (size_t)capdirect * sizeof(char *));
        if (!direct_keys) { fprintf(stderr, "oom direct answers\n"); exit(2); }
    }
    direct_keys[ndirect++] = key;
}

static void direct_solve_goals(int *gs, int ng, int depth, int topgoal) {
    if (direct_stop) return;
    if (++direct_steps > direct_step_limit) { direct_stop = 2; return; }
    if (depth > direct_depth_limit) { direct_stop = 3; return; }
    if ((double)(clock() - direct_t0) / CLOCKS_PER_SEC > direct_timeout) {
        direct_stop = 1; return;
    }
    if (ng == 0) { direct_record(topgoal); return; }

    int goal = deref(gs[0]);
    int psym = -1, parity = -1;
    if (nodes[goal].tag == T_APP) { psym = nodes[goal].sym; parity = nodes[goal].nkids; }
    else if (nodes[goal].tag == T_SYM) { psym = nodes[goal].sym; parity = 0; }
    if (ground_different_enabled && psym == sym_different && parity == 2) {
        int left = resolve(KID(goal, 0));
        int right = resolve(KID(goal, 1));
        if (!(nodes[left].flags & NF_HAS_VAR) &&
            !(nodes[right].flags & NF_HAS_VAR) && left != right)
            direct_solve_goals(gs + 1, ng - 1, depth + 1, topgoal);
    }
    int ri = (psym >= 0) ? rule_heads[rule_hash(psym, parity)] : -1;
    for (; ri >= 0 && !direct_stop; ri = rules[ri].next_same_head) {
        Rule *r = &rules[ri];
        if (r->hsym != psym || r->harity != parity) continue;
        int mark = ntrail;
        int base = varbase; varbase += r->nvars + 1;
        ensure_bind(varbase + 1);
        int h = rename_t(r->head, base);
        if (unify(h, goal)) {
            int total = r->nbody + ng - 1;
            int *next = malloc((size_t)(total ? total : 1) * sizeof(int));
            if (!next) { fprintf(stderr, "oom direct goals\n"); exit(2); }
            for (int i = 0; i < r->nbody; i++) next[i] = rename_t(r->body[i], base);
            for (int i = 1; i < ng; i++) next[r->nbody + i - 1] = gs[i];
            direct_solve_goals(next, total, depth + 1, topgoal);
            free(next);
        }
        undo(mark);
    }
}

static void direct_sort(void) {
    for (int i = 1; i < ndirect; i++) {
        char *v = direct_keys[i]; int j = i - 1;
        while (j >= 0 && strcmp(direct_keys[j], v) > 0) {
            direct_keys[j + 1] = direct_keys[j]; j--;
        }
        direct_keys[j + 1] = v;
    }
}

/* ------------------------------------------------------------------ main */
static char *slurp(const char *path, size_t *outlen) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(3); }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    size_t rd = fread(b, 1, (size_t)n, f);
    b[rd] = 0; *outlen = rd; fclose(f);
    return b;
}

static int write_unique_certificate(const char *path, int answer_count,
                                    const int *ordered_answers) {
    if (!path) return 1;
    if (answer_count != 1) {
        fprintf(stderr,
                "--cert-out requires exactly one completed answer, got %d\n",
                answer_count);
        return 0;
    }
    FILE *output = fopen(path, "wb");
    if (!output) {
        fprintf(stderr, "cannot open certificate output %s\n", path);
        return 0;
    }
    print_cert(output, ordered_answers[0], 0);
    fputc('\n', output);
    if (fclose(output) != 0) {
        fprintf(stderr, "cannot finish certificate output %s\n", path);
        return 0;
    }
    return 1;
}

static int write_answer_stream(const char *path, int answer_count,
                               char *const *ordered_terms) {
    char temporary[PATH_MAX];
    int descriptor = -1;
    FILE *output = NULL;
    int published = 0;

    if (!path) return 1;
    if (snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path) >=
        (int)sizeof(temporary)) {
        fprintf(stderr, "answer-stream output path is too long\n");
        return 0;
    }
    descriptor = mkstemp(temporary);
    if (descriptor < 0) {
        fprintf(stderr, "cannot create answer-stream output %s: %s\n",
                path, strerror(errno));
        return 0;
    }
    output = fdopen(descriptor, "wb");
    if (!output) {
        fprintf(stderr, "cannot open answer-stream output %s: %s\n",
                path, strerror(errno));
        close(descriptor);
        unlink(temporary);
        return 0;
    }
    descriptor = -1;
    for (int index = 0; index < answer_count; index++) {
        if (fputs(ordered_terms[index], output) == EOF ||
            fputc('\n', output) == EOF) {
            fprintf(stderr, "cannot write answer-stream output %s\n", path);
            goto done;
        }
    }
    if (fclose(output) != 0) {
        output = NULL;
        fprintf(stderr, "cannot finish answer-stream output %s\n", path);
        goto done;
    }
    output = NULL;
    if (rename(temporary, path) != 0) {
        fprintf(stderr, "cannot publish answer-stream output %s: %s\n",
                path, strerror(errno));
        goto done;
    }
    published = 1;

done:
    if (output) (void)fclose(output);
    if (!published) (void)unlink(temporary);
    return published;
}

static void ordered_text_digest(char *const *texts,
                                int text_count,
                                char digest[65]) {
    static const char prefix[] = "FiniteHornAnswerSetV1\n";
    size_t total = sizeof(prefix) - 1u;
    for (int index = 0; index < text_count; index++) {
        size_t length = strlen(texts[index]);
        if (length > SIZE_MAX - total - 1u)
            fail_resource("answer-set digest payload");
        total += length + 1u;
    }
    uint8_t *payload = malloc(total ? total : 1u);
    if (!payload) fail_resource("answer-set digest payload");
    size_t offset = 0u;
    memcpy(payload + offset, prefix, sizeof(prefix) - 1u);
    offset += sizeof(prefix) - 1u;
    for (int index = 0; index < text_count; index++) {
        size_t length = strlen(texts[index]);
        memcpy(payload + offset, texts[index], length);
        offset += length;
        payload[offset++] = (uint8_t)'\n';
    }
    cetta_native_sha256_hex(payload, offset, digest);
    free(payload);
}

int main(int argc, char **argv) {
    const char *root = NULL, *infile = NULL, *intext = NULL;
    const char *query_text = NULL, *query_file = NULL;
    const char *replay_arg = NULL, *cert_out = NULL;
    const char *answer_out = NULL;
    const char *pres[64]; int npres = 0;
    const char *reflect_pres[64]; int nreflect_pres = 0;
    int show_certs = 0, summary_only = 0, interpreted = 0, max_rounds = 10000000;
    double timeout = 600.0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--input-file") && i + 1 < argc) infile = argv[++i];
        else if (!strcmp(argv[i], "--input-text") && i + 1 < argc) intext = argv[++i];
        else if (!strcmp(argv[i], "--query-text") && i + 1 < argc) query_text = argv[++i];
        else if (!strcmp(argv[i], "--query-file") && i + 1 < argc) query_file = argv[++i];
        else if (!strcmp(argv[i], "--certs")) show_certs = 1;
        else if (!strcmp(argv[i], "--cert-out") && i + 1 < argc) cert_out = argv[++i];
        else if (!strcmp(argv[i], "--answer-out") && i + 1 < argc && !answer_out)
            answer_out = argv[++i];
        else if (!strcmp(argv[i], "--summary")) summary_only = 1;
        else if (!strcmp(argv[i], "--interpreted")) interpreted = 1;
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay_arg = argv[++i];
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout = atof(argv[++i]);
        else if (!strcmp(argv[i], "--max-rounds") && i + 1 < argc) max_rounds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-nodes") && i + 1 < argc) node_limit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--max-depth") && i + 1 < argc) direct_depth_limit = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--max-steps") && i + 1 < argc) direct_step_limit = atol(argv[++i]);
        else if (!strcmp(argv[i], "--reflect-source") && i + 1 < argc) {
            if (npres >= 64 || nreflect_pres >= 64) {
                fprintf(stderr, "too many presentation files\n");
                return 1;
            }
            const char *path = argv[++i];
            pres[npres++] = path;
            reflect_pres[nreflect_pres++] = path;
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown or incomplete option: %s\n", argv[i]);
            return 1;
        } else if (npres < 64) {
            pres[npres++] = argv[i];
        } else {
            fprintf(stderr, "too many presentation files\n");
            return 1;
        }
    }
    if (!npres || ((!query_text && !query_file) && !root)) {
        fprintf(stderr,
            "usage: finite_horn_chart_v1 <presentation-file>...\n"
            "       [--reflect-source <presentation-file>]...\n"
            "       (--query-text <term> | --query-file <file>) [--certs]\n"
            "       [--answer-out <finite-Horn-answer-stream>]\n"
            "   or: <presentation-file>... --root <name>\n"
            "       (--input-file <f> | --input-text <s>) [--certs] [--summary]\n"
            "       [--cert-out <file>] [--replay <file>]\n"
            "       [--interpreted] [--timeout SEC] [--max-rounds N]\n"
            "       [--max-nodes N] [--max-depth N] [--max-steps N]\n");
        return 1;
    }
    if (answer_out && !query_text && !query_file) {
        fprintf(stderr, "--answer-out is available only for generic queries\n");
        return 1;
    }

    FHGSLTPackage *checked_package = NULL;
    char schema_error[512] = {0};
    if (!fhgslt_package_from_paths(pres, (size_t)npres, &checked_package,
                                   schema_error, sizeof schema_error)) {
        printf("{\"outcome\":\"MalformedPresentation\",\"reason\":\"%s\"}\n",
               schema_error[0] ? schema_error : "finite-Horn schema rejection");
        return 0;
    }
    size_t checked_rule_count = fhgslt_package_rule_count(checked_package);
    ground_different_enabled =
        fhgslt_package_declares_operator(checked_package, "different", 2u);
    uint8_t *reflected_text = NULL;
    size_t reflected_text_len = 0u;
    size_t reflected_rule_count = 0u;
    if (nreflect_pres > 0) {
        size_t reflected_source_rules = 0u;
        size_t reflected_source_operators = 0u;
        if (!fhgslt_package_reflected_sources(
                checked_package,
                reflect_pres,
                (size_t)nreflect_pres,
                &reflected_text,
                &reflected_text_len,
                &reflected_source_operators,
                &reflected_source_rules,
                schema_error,
                sizeof schema_error)) {
            printf("{\"outcome\":\"MalformedPresentation\","
                   "\"reason\":\"%s\"}\n",
                   schema_error[0] ? schema_error :
                       "finite-Horn reflection rejection");
            fhgslt_package_free(checked_package);
            return 0;
        }
        if (reflected_source_rules >
            SIZE_MAX - reflected_source_operators) {
            printf("{\"outcome\":\"MalformedPresentation\","
                   "\"reason\":\"reflected declaration count overflow\"}\n");
            fhgslt_package_free(checked_package);
            free(reflected_text);
            return 0;
        }
        reflected_rule_count =
            reflected_source_rules + reflected_source_operators;
    }

    idmap_init(&goal_map, 4096);
    idmap_init(&answer_map, 16384);
    rule_index_init();
    ensure_bind(65536);

    sym_rule = intern("rule", 4); sym_head = intern("head", 4); sym_body = intern("body", 4);
    sym_cert = intern("cert", 4);
    sym_different = intern("different", 9);
    sym_intrinsic_ground_different =
        intern("$intrinsic-ground-different", 27);

    for (int i = 0; i < npres; i++) {
        size_t n; char *t = slurp(pres[i], &n);
        if (!presentation_text_well_formed(t, n)) {
            set_presentation_error("unbalanced or unterminated presentation syntax");
            free(t);
            break;
        }
        load_forms(t, n);
        free(t);
        if (presentation_error[0]) break;
    }
    if (presentation_error[0]) {
        printf("{\"outcome\":\"MalformedPresentation\",\"reason\":\"%s\"}\n",
               presentation_error);
        free(reflected_text);
        fhgslt_package_free(checked_package);
        return 0;
    }
    if ((size_t)nrules != checked_rule_count) {
        printf("{\"outcome\":\"MalformedPresentation\","
               "\"reason\":\"validated/executable rule-count mismatch: %zu/%d\"}\n",
               checked_rule_count, nrules);
        fhgslt_package_free(checked_package);
        free(reflected_text);
        return 0;
    }
    if (reflected_text != NULL) {
        load_forms((const char *)reflected_text, reflected_text_len);
        free(reflected_text);
        reflected_text = NULL;
        if (presentation_error[0] ||
            (size_t)nrules != checked_rule_count + reflected_rule_count) {
            printf("{\"outcome\":\"MalformedPresentation\","
                   "\"reason\":\"%s\"}\n",
                   presentation_error[0] ? presentation_error :
                       "reflected rule-count mismatch");
            fhgslt_package_free(checked_package);
            return 0;
        }
    }
    fhgslt_package_free(checked_package);
    if (nrules == 0) { printf("{\"outcome\":\"MalformedPresentation\",\"reason\":\"no rules\"}\n"); return 0; }

    if (query_text || query_file) {
        char *owned_query = NULL;
        size_t query_len = 0;
        if (query_file) owned_query = slurp(query_file, &query_len);
        else {
            query_len = strlen(query_text);
            owned_query = strdup(query_text);
        }
        static int *query_vars = NULL;
        if (!query_vars) query_vars = malloc(sizeof(int) * 65536u);
        if (!owned_query || !query_vars) fail_resource("generic query");
        int query_var_count = 0;
        Reader query_reader = { owned_query, owned_query + query_len, 0 };
        int parsed_query = rd_term(&query_reader, query_vars, &query_var_count);
        skipws(&query_reader);
        if (parsed_query < 0 || query_reader.failed ||
            query_reader.p != query_reader.end) {
            printf("{\"outcome\":\"MalformedQuery\"}\n");
            free(owned_query);
            return 0;
        }
        ensure_bind(1000000 + query_var_count + 16);
        int query = rename_t(parsed_query, 1000000);
        varbase = 2000000;

        if (replay_arg) {
            size_t cert_len = 0;
            char *cert_text = slurp(replay_arg, &cert_len);
            static int *cert_vars = NULL;
            if (!cert_vars) cert_vars = malloc(sizeof(int) * 65536u);
            int cert_var_count = 0;
            Reader cert_reader = { cert_text, cert_text + cert_len, 0 };
            int cert = rd_term(&cert_reader, cert_vars, &cert_var_count);
            skipws(&cert_reader);
            char why[512] = {0};
            int ok = cert >= 0 && !cert_reader.failed &&
                     cert_reader.p == cert_reader.end &&
                     replay(cert, query, why, sizeof why);
            printf("{\"replay\":\"%s\"", ok ? "ACCEPT" : "REJECT");
            if (!ok)
                printf(",\"reason\":\"%s\"",
                       why[0] ? why : "certificate did not re-derive");
            printf("}\n");
            free(cert_text);
            free(owned_query);
            return ok ? 0 : 1;
        }

        int query_goal = demand(query);
        clock_t query_start = clock();
        enum { QUERY_STOP_NONE, QUERY_STOP_TIMEOUT,
               QUERY_STOP_ROUNDS, QUERY_STOP_RESOURCE } query_stop = QUERY_STOP_NONE;
        int query_rounds = 0;
        while (agenda_head < agenda_tail) {
            double elapsed = (double)(clock() - query_start) / CLOCKS_PER_SEC;
            if (elapsed >= timeout) { query_stop = QUERY_STOP_TIMEOUT; break; }
            int goal = agenda[agenda_head++];
            goals[goal].queued = 0;
            process_goal(goal);
            query_rounds++;
            if (oom_hit) { query_stop = QUERY_STOP_RESOURCE; break; }
            if (query_rounds >= max_rounds) { query_stop = QUERY_STOP_ROUNDS; break; }
        }
        double query_elapsed =
            (double)(clock() - query_start) / CLOCKS_PER_SEC;
        int answer_count = goals[query_goal].nans;
        const char *query_outcome = NULL;
        if (query_stop == QUERY_STOP_RESOURCE)
            query_outcome = answer_count ? "ResourceExhaustionWithResults" : "ResourceExhaustion";
        else if (query_stop == QUERY_STOP_TIMEOUT)
            query_outcome = answer_count ? "TimeoutWithResults" : "Timeout";
        else if (query_stop == QUERY_STOP_ROUNDS)
            query_outcome = answer_count ? "RoundLimitWithResults" : "RoundLimit";
        else
            query_outcome = answer_count == 0 ? "NoAnswer" :
                            (answer_count == 1 ? "Unique" : "Ambiguous");

        int *ordered = malloc((size_t)(answer_count ? answer_count : 1) * sizeof(int));
        char **keys = malloc((size_t)(answer_count ? answer_count : 1) * sizeof(char *));
        if (!ordered || !keys) fail_resource("query result ordering");
        int collected = collect_goal_answers(query_goal, ordered);
        if (collected != answer_count) fail_resource("query answer-count mismatch");
        for (int i = 0; i < answer_count; i++) keys[i] = tostr(ans[ordered[i]].term);
        for (int i = 1; i < answer_count; i++) {
            int answer = ordered[i];
            char *key = keys[i];
            int j = i - 1;
            while (j >= 0 && strcmp(keys[j], key) > 0) {
                ordered[j + 1] = ordered[j];
                keys[j + 1] = keys[j];
                j--;
            }
            ordered[j + 1] = answer;
            keys[j + 1] = key;
        }
        char term_digest[65];
        ordered_text_digest(keys, answer_count, term_digest);
        if (query_stop != QUERY_STOP_NONE && answer_out) {
            fprintf(stderr,
                    "refusing to publish an incomplete answer stream (%s)\n",
                    query_outcome);
            for (int i = 0; i < answer_count; i++) free(keys[i]);
            free(keys);
            free(ordered);
            free(owned_query);
            return 3;
        }
        if (!write_answer_stream(answer_out, answer_count, keys)) {
            for (int i = 0; i < answer_count; i++) free(keys[i]);
            free(keys);
            free(ordered);
            free(owned_query);
            return 3;
        }
        if (!write_unique_certificate(cert_out, answer_count, ordered)) {
            for (int i = 0; i < answer_count; i++) free(keys[i]);
            free(keys);
            free(ordered);
            free(owned_query);
            return 3;
        }

        printf("{\"outcome\":\"%s\",\"answers\":%d,\"rules\":%d,"
               "\"goals\":%d,\"derivations\":%ld,\"rounds\":%d,"
               "\"seconds\":%.6f,\"term_digest\":\"%s\"",
               query_outcome, answer_count, nrules, ngoals, derivations,
               query_rounds, query_elapsed, term_digest);
        if (!summary_only) {
            printf(",\"terms\":[");
            for (int i = 0; i < answer_count; i++) {
                printf("%s\"", i ? "," : "");
                for (char *cursor = keys[i]; *cursor; cursor++) {
                    if (*cursor == '\"' || *cursor == '\\') putchar('\\');
                    putchar(*cursor);
                }
                putchar('\"');
            }
            printf("]");
        }
        if (show_certs) {
            printf(",\"certs\":[");
            for (int i = 0; i < answer_count; i++) {
                printf("%s\"", i ? "," : "");
                FILE *memory_file = tmpfile();
                print_cert(memory_file, ordered[i], 0);
                fflush(memory_file);
                long size = ftell(memory_file);
                fseek(memory_file, 0, SEEK_SET);
                char *certificate = malloc((size_t)size + 1u);
                size_t read = fread(certificate, 1, (size_t)size, memory_file);
                certificate[read] = 0;
                fclose(memory_file);
                for (char *cursor = certificate; *cursor; cursor++) {
                    if (*cursor == '\"' || *cursor == '\\') putchar('\\');
                    putchar(*cursor);
                }
                free(certificate);
                putchar('\"');
            }
            printf("]");
        }
        printf("}\n");
        for (int i = 0; i < answer_count; i++) free(keys[i]);
        free(keys);
        free(ordered);
        free(owned_query);
        return 0;
    }

    /* input */
    char *text = NULL; size_t tlen = 0;
    if (infile) text = slurp(infile, &tlen);
    else if (intext) { tlen = strlen(intext); text = strdup(intext); }
    else { text = strdup(""); tlen = 0; }
    if (!text) fail_resource("input text");
    int input = encode_codepoints(text, tlen);
    if (input < 0) {
        printf("{\"outcome\":\"MalformedInput\",\"reason\":\"%s\"}\n",
               input_error[0] ? input_error : "invalid UTF-8");
        free(text);
        return 0;
    }

    /* top goal: (parse (ref ROOT) INPUT ?v nil)  -- built from ordinary terms.
       The relation name and shape come from the presentation's own rules; we
       only supply the root name and the input the user asked about. */
    int rootsym = intern(root, (int)strlen(root));
    int *refk = malloc(sizeof(int)); refk[0] = newnode(T_SYM, rootsym, 0, NULL);
    int refroot = newnode(T_APP, intern("ref", 3), 1, refk);
    int vvar = newnode(T_VAR, 1000000, 0, NULL);
    ensure_bind(1000002);
    int nilN = newnode(T_SYM, intern("nil", 3), 0, NULL);
    int *gk = malloc(4 * sizeof(int));
    gk[0] = refroot; gk[1] = input; gk[2] = vvar; gk[3] = nilN;
    int topgoal = newnode(T_APP, intern("parse", 5), 4, gk);

    varbase = 2000000;
    if (replay_arg) {
        size_t cn; char *ct = slurp(replay_arg, &cn);
        static int *vm = NULL; if (!vm) vm = malloc(sizeof(int) * 65536);
        int nv2 = 0; Reader rr = { ct, ct + cn, 0 };
        int cert = rd_term(&rr, vm, &nv2);
        skipws(&rr);
        char why[512]; why[0] = 0;
        int ok = (cert >= 0) && !rr.failed && rr.p == rr.end &&
                 replay(cert, topgoal, why, sizeof why);
        printf("{\"replay\":\"%s\"", ok ? "ACCEPT" : "REJECT");
        if (!ok) printf(",\"reason\":\"%s\"", why[0] ? why : "certificate did not re-derive");
        printf("}\n");
        free(ct);
        free(text);
        return ok ? 0 : 1;
    }
    if (interpreted) {
        direct_timeout = timeout;
        direct_t0 = clock();
        int only[1] = { topgoal };
        direct_solve_goals(only, 1, 0, topgoal);
        double elapsed = (double)(clock() - direct_t0) / CLOCKS_PER_SEC;
        direct_sort();
        const char *outcome = direct_stop == 1 ? (ndirect ? "TimeoutWithResults" : "Timeout")
                            : direct_stop == 2 ? (ndirect ? "StepLimitWithResults" : "StepLimit")
                            : direct_stop == 3 ? (ndirect ? "DepthLimitWithResults" : "DepthLimit")
                            : ndirect == 0 ? "NoParse" : (ndirect == 1 ? "Unique" : "Ambiguous");
        printf("{\"mode\":\"interpreted\",\"outcome\":\"%s\",\"trees\":%d,"
               "\"rules\":%d,\"steps\":%ld,\"bytes\":%zu,\"seconds\":%.4f",
               outcome, ndirect, nrules, direct_steps, tlen, elapsed);
        if (!summary_only) {
            printf(",\"asts\":[");
            for (int i = 0; i < ndirect; i++) {
                printf("%s\"", i ? "," : "");
                for (char *q = direct_keys[i]; *q; q++) {
                    if (*q == '"' || *q == '\\') putchar('\\');
                    putchar(*q);
                }
                putchar('"');
            }
            putchar(']');
        }
        printf("}\n");
        free(text);
        return 0;
    }

    int tg = demand(topgoal);

    clock_t t0 = clock();
    enum { STOP_NONE, STOP_TIMEOUT, STOP_ROUNDS, STOP_RESOURCE } stop_reason = STOP_NONE;
    int rounds = 0; /* agenda expansions; name retained for receipt compatibility */
    while (agenda_head < agenda_tail) {
        double before = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (before >= timeout) { stop_reason = STOP_TIMEOUT; break; }
        int g = agenda[agenda_head++];
        goals[g].queued = 0;
        process_goal(g);
        rounds++;
        double el = (double)(clock() - t0) / CLOCKS_PER_SEC;
        if (oom_hit) { stop_reason = STOP_RESOURCE; break; }
        if (el >= timeout) { stop_reason = STOP_TIMEOUT; break; }
        if (rounds >= max_rounds) { stop_reason = STOP_ROUNDS; break; }
    }
    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;

    int n = goals[tg].nans;
    const char *outcome = NULL;
    if (stop_reason == STOP_RESOURCE) outcome = n ? "ResourceExhaustionWithResults" : "ResourceExhaustion";
    else if (stop_reason == STOP_TIMEOUT) outcome = n ? "TimeoutWithResults" : "Timeout";
    else if (stop_reason == STOP_ROUNDS) outcome = n ? "RoundLimitWithResults" : "RoundLimit";
    else outcome = n == 0 ? "NoParse" : (n == 1 ? "Unique" : "Ambiguous");

    /* deterministic canonical ordering: serialization occurs only for the
       final, usually tiny result set—not for chart identity. */
    int *idx = malloc((size_t)(n ? n : 1) * sizeof(int));
    char **out_keys = malloc((size_t)(n ? n : 1) * sizeof(char *));
    if (!idx || !out_keys) { fprintf(stderr, "oom result ordering\n"); exit(2); }
    int got = collect_goal_answers(tg, idx);
    if (got != n) { fprintf(stderr, "internal answer-count mismatch\n"); exit(2); }
    for (int i = 0; i < n; i++) {
        int at = ans[idx[i]].term;
        int val = (nodes[at].tag == T_APP && nodes[at].nkids >= 3) ? KID(at, 2) : at;
        out_keys[i] = tostr(val);
    }
    for (int i = 1; i < n; i++) {
        int v = idx[i]; char *vk = out_keys[i]; int j = i - 1;
        while (j >= 0 && strcmp(out_keys[j], vk) > 0) {
            idx[j + 1] = idx[j]; out_keys[j + 1] = out_keys[j]; j--;
        }
        idx[j + 1] = v; out_keys[j + 1] = vk;
    }

    if (!write_unique_certificate(cert_out, n, idx)) {
        for (int i = 0; i < n; i++) free(out_keys[i]);
        free(out_keys);
        free(idx);
        free(text);
        return 3;
    }

    printf("{\"outcome\":\"%s\",\"trees\":%d,\"rules\":%d,\"goals\":%d,\"answers\":%d,"
           "\"derivations\":%ld,\"rounds\":%d,\"bytes\":%zu,\"seconds\":%.4f,"
           "\"term_nodes\":%d,\"term_child_ids\":%u,\"packed_alternatives\":%d,"
           "\"packed_child_refs\":%u,\"chart_links\":%d",
           outcome, n, nrules, ngoals, nans, derivations, rounds, tlen, elapsed,
           nnodes, nchild_ids, nalts, nalt_children, nanswer_links + nconsumer_links);
    if (!summary_only) {
            printf(",\"asts\":[");
            for (int i = 0; i < n; i++) {
                printf("%s\"", i ? "," : "");
                for (char *p = out_keys[i]; *p; p++) {
                    if (*p == '\"' || *p == '\\') putchar('\\');
                    putchar(*p);
                }
                printf("\"");
            }
            printf("]");
    }
    if (show_certs) {
        printf(",\"certs\":[");
        for (int i = 0; i < n; i++) {
            printf("%s\"", i ? "," : "");
            /* certificate to a temp buffer */
            FILE *mf = tmpfile();
            print_cert(mf, idx[i], 0);
            fflush(mf); long sz = ftell(mf); fseek(mf, 0, SEEK_SET);
            char *cb = malloc((size_t)sz + 1);
            size_t rr = fread(cb, 1, (size_t)sz, mf); cb[rr] = 0; fclose(mf);
            for (char *p = cb; *p; p++) { if (*p == '"' || *p == '\\') putchar('\\'); putchar(*p); }
            free(cb);
            printf("\"");
        }
        printf("]");
    }
    printf("}\n");
    for (int i = 0; i < n; i++) free(out_keys[i]);
    free(out_keys); free(idx);
    free(text);
    return 0;
}

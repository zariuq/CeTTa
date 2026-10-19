#define _GNU_SOURCE
#include "atom_wire.h"
#include "match.h"
#include "mm2_lower.h"
#include "native_sha256.h"
#include "space.h"
#include "space_path_index.h"
#include "symbol.h"
#include "term_universe.h"
#include "mork_space_bridge_runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* Storage runs on one owner. Transport is nonblocking and bounded. There is
 * no evaluator entry point in this protocol: terms are stored and matched. */
enum { CLIENTS = 32, STORES = 32, SNAPSHOTS = 64, RECEIPTS = 256,
       RECEIPT_BYTES = 16 * 1024 * 1024, QUERY_BYTES = 8 * 1024 * 1024,
       SNAPSHOT_BYTES = 32 * 1024 * 1024, PAGE_ROWS = 1024,
       QUERY_ROWS = 65536, FACT_BYTES = 512 * 1024 };

typedef struct { unsigned char *data; size_t size; } Bytes;
typedef struct {
    uint64_t id, revision;
    char name[129];
    SpaceEngine engine;
    Arena arena;
    TermUniverse universe;
    Space *space;
    CettaMorkSpaceHandle *backend;
    uint64_t live_bytes, churn;
} Store;
typedef struct {
    uint64_t id, source, revision, expires;
    Bytes *rows;
    size_t count, size;
} Snapshot;
typedef struct { char id[129]; Bytes request, reply; } Receipt;
typedef struct {
    Store *stores[STORES];
    Snapshot snapshots[SNAPSHOTS];
    Receipt receipts[RECEIPTS];
    size_t receipt_count, receipt_bytes, snapshot_bytes;
    uint64_t next_store, next_cursor, catalog_revision;
    uint64_t max_facts, max_live_bytes, max_journal_bytes, journal_bytes, checkpoint_bytes;
    uint64_t max_query_bytes, max_snapshot_bytes;
    unsigned query_ms;
    size_t watchers, max_watchers;
    char uuid[33], boot[33];
    int journal, directory, lock;
    bool poisoned, restoring;
} Service;
typedef struct {
    int fd;
    unsigned char header[8];
    size_t header_read, input_read;
    Bytes input, output;
    size_t output_sent;
    uint64_t deadline;
    uint64_t watch_source, watch_revision, watch_due;
    bool watching;
} Client;

static volatile sig_atomic_t stopping;
static void stop(int signal_number) { (void)signal_number; stopping = 1; }
static uint64_t now_ms(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}
static void bytes_free(Bytes *b) { free(b->data); *b = (Bytes){0}; }
static bool bytes_copy(Bytes *out, const unsigned char *data, size_t size) {
    out->data = malloc(size ? size : 1);
    if (!out->data) return false;
    if (size) memcpy(out->data, data, size);
    out->size = size; return true;
}
static bool same_bytes(Bytes a, Bytes b) {
    return a.size == b.size && !memcmp(a.data, b.data, a.size);
}
static bool random_id(char out[33]) {
    unsigned char raw[16]; size_t pos = 0;
    while (pos < sizeof(raw)) {
        ssize_t n = getrandom(raw + pos, sizeof(raw) - pos, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        pos += (size_t)n;
    }
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) { out[2*i] = hex[raw[i] >> 4]; out[2*i+1] = hex[raw[i] & 15]; }
    out[32] = 0; return true;
}
static bool text(Atom *a, const char **out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_STRING ||
        memchr(a->ground.sval, 0, atom_string_len(a))) return false;
    *out = a->ground.sval; return true;
}
static bool token(Atom *a, const char **out) {
    if (!text(a, out)) return false;
    size_t n = atom_string_len(a);
    if (!n || n > 128) return false;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)(*out)[i] < 33 ||
        (unsigned char)(*out)[i] > 126) return false;
    return true;
}
static bool integer(Atom *a, uint64_t *out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT || a->ground.ival < 0)
        return false;
    *out = (uint64_t)a->ground.ival; return true;
}
static Atom *part(Atom *a, size_t i) {
    return a && a->kind == ATOM_EXPR && i < a->expr.len ? a->expr.elems[i] : NULL;
}
static bool shape(Atom *a, const char *head, size_t n) {
    return a && a->kind == ATOM_EXPR && a->expr.len == n && atom_is_symbol(part(a,0), head);
}
static Atom *form(Arena *a, const char *head, Atom **args, size_t count) {
    Atom **items = arena_alloc(a, (count + 1) * sizeof(*items));
    items[0] = atom_symbol(a, head);
    for (size_t i = 0; i < count; ++i) items[i+1] = args[i];
    return atom_expr(a, items, count+1);
}
static Atom *reply(Arena *a, Atom *request, bool ok, Atom *value) {
    Atom *args[] = {part(request,1), atom_symbol(a, ok ? "ok" : "error"), value};
    return form(a, "reply", args, 3);
}
static Atom *error(Arena *a, Atom *request, const char *reason) {
    return reply(a, request, false, atom_symbol(a, reason));
}
static bool wire(Atom *atom, Bytes *out) {
    return cetta_atom_wire_encode(atom, &out->data, &out->size) == ATOM_WIRE_OK;
}
static bool decode(Arena *arena, Bytes bytes, Atom **out) {
    return cetta_atom_wire_decode(arena, bytes.data, bytes.size, out) == ATOM_WIRE_OK;
}
static Store *find_store(Service *s, uint64_t id) {
    for (size_t i = 0; i < STORES; ++i) if (s->stores[i] && s->stores[i]->id == id) return s->stores[i];
    return NULL;
}
static Store *find_name(Service *s, const char *name) {
    for (size_t i = 0; i < STORES; ++i) if (s->stores[i] && !strcmp(s->stores[i]->name, name)) return s->stores[i];
    return NULL;
}
static size_t store_slot(Service *s) {
    for (size_t i = 0; i < STORES; ++i) if (!s->stores[i]) return i;
    return STORES;
}
static bool engine(Atom *a, SpaceEngine *out) {
    if (atom_is_symbol(a, "native")) *out = SPACE_ENGINE_NATIVE;
    else if (atom_is_symbol(a, "pathmap")) *out = SPACE_ENGINE_PATHMAP;
    else if (atom_is_symbol(a, "mork")) *out = SPACE_ENGINE_MORK;
    else return false;
    return true;
}
static const char *engine_name(SpaceEngine e) {
    return e == SPACE_ENGINE_MORK ? "mork" : e == SPACE_ENGINE_PATHMAP ? "pathmap" : "native";
}
static void store_free(Store *st) {
    if (!st) return;
    if (st->space) { space_free(st->space); free(st->space); }
    if (st->backend) cetta_mork_bridge_space_free(st->backend);
    term_universe_free(&st->universe); arena_free(&st->arena); free(st);
}
static Store *store_new(uint64_t id, const char *name, SpaceEngine e, uint64_t revision) {
    Store *st = calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->id = id; st->revision = revision; st->engine = e;
    snprintf(st->name, sizeof(st->name), "%s", name);
    arena_init_detached(&st->arena);
    term_universe_init(&st->universe);
    term_universe_set_persistent_arena(&st->universe, &st->arena);
    st->space = calloc(1, sizeof(*st->space));
    if (!st->space) { store_free(st); return NULL; }
    space_init_with_universe(st->space, &st->universe);
    if (e != SPACE_ENGINE_NATIVE) {
        st->backend=e==SPACE_ENGINE_PATHMAP ? cetta_mork_bridge_space_new_pathmap()
                                           : cetta_mork_bridge_space_new();
        if (!st->backend) { store_free(st); return NULL; }
    }
    return st;
}
/* Rust storage holds lossless, ground portable keys. The native Space is the
 * maintained matching projection, never a text reconstruction of a Rust row.
 * The service exclusively owns both; every acknowledged mutation changes both.
 * This profile does not expose MM2 execution or Rust query acceleration. */
static bool backend_key(Bytes key,Bytes *encoded,bool packet) {
    Arena a; arena_init_detached(&a);
    /* Compact bridge symbols have a 63-byte limit. A canonical bounded-arity
     * rope keeps arbitrary bytes lossless without permanent symbol interning,
     * hashing collisions, hex expansion or a text parser. */
    size_t count=(key.size+62)/63;
    Atom **nodes=arena_alloc(&a,count*sizeof(*nodes));
    for (size_t i=0;i<count;++i) {
        size_t size=key.size-i*63; if (size>63) size=63;
        nodes[i]=atom_symbol_data_n(&a,(const char *)key.data+i*63,(uint32_t)size);
    }
    while (count>62) {
        size_t groups=(count+61)/62;
        Atom **next=arena_alloc(&a,groups*sizeof(*next));
        for (size_t i=0;i<groups;++i) {
            size_t size=count-i*62; if (size>62) size=62;
            next[i]=form(&a,"cav1:branch",nodes+i*62,size);
        }
        nodes=next; count=groups;
    }
    Atom *symbol=form(&a,"cav1:key",nodes,count);
    const char *reason=NULL;
    bool ok=packet
        ? cetta_mm2_atom_to_bridge_expr_packet(&a,symbol,&encoded->data,&encoded->size,&reason)
        : cetta_mm2_atom_to_bridge_expr_bytes(&a,symbol,&encoded->data,&encoded->size,&reason);
    arena_free(&a); return ok;
}
static bool backend_change(Store *st,Bytes key,bool add,uint64_t expected) {
    if (!st->backend) return true;
    Bytes encoded={0};
    if (!backend_key(key,&encoded,false)) return false;
    uint64_t changed=0;
    bool ok=add ? cetta_mork_bridge_space_add_expr_bytes(st->backend,encoded.data,encoded.size,&changed)
                : cetta_mork_bridge_space_remove_expr_bytes(st->backend,encoded.data,encoded.size,&changed);
    bytes_free(&encoded); return ok && changed==expected;
}
static bool store_consistent(Store *st) {
    uint64_t count=0;
    return !st->backend || (cetta_mork_bridge_space_size(st->backend,&count) &&
                           count==space_length64(st->space));
}
static int byte_order(const void *left,const void *right) {
    const Bytes *a=left,*b=right;
    size_t length=a->size<b->size ? a->size : b->size;
    int order=memcmp(a->data,b->data,length);
    return order ? order : a->size>b->size ? 1 : a->size<b->size ? -1 : 0;
}
/* Diagnostic comparison of complete key multisets, including multiplicities.
 * The dump's stable expr bytes are compared directly, without a text parser or
 * Rust-to-MeTTa projection. It is intentionally outside the mutation hot path. */
static bool verify_backend(Store *st) {
    if (!st->backend) return true;
    uint64_t count=space_length64(st->space),reported=0;
    Bytes packet={0};
    if (!cetta_mork_bridge_space_dump_expr_rows(st->backend,&packet.data,&packet.size,&reported))
        return false;
    Bytes *expected=calloc(count ? (size_t)count : 1,sizeof(*expected));
    Bytes *actual=calloc(count ? (size_t)count : 1,sizeof(*actual));
    bool ok=expected && actual && reported==count;
    size_t position=0;
    for (uint64_t i=0;ok && i<count;++i) {
        Bytes key={0};
        ok=wire(space_get_at64(st->space,i),&key) && backend_key(key,&expected[i],true);
        bytes_free(&key);
        if (!ok || packet.size-position<4) { ok=false; break; }
        size_t length=0;
        for (size_t j=0;j<4;++j) length=(length<<8)|packet.data[position++];
        if (length>packet.size-position) { ok=false; break; }
        actual[i]=(Bytes){packet.data+position,length}; position+=length;
    }
    if (ok) ok=position==packet.size;
    if (ok) {
        qsort(expected,(size_t)count,sizeof(*expected),byte_order);
        qsort(actual,(size_t)count,sizeof(*actual),byte_order);
        for (uint64_t i=0;ok && i<count;++i) ok=same_bytes(expected[i],actual[i]);
    }
    if (expected) for (uint64_t i=0;i<count;++i) bytes_free(&expected[i]);
    free(expected); free(actual);
    cetta_mork_bridge_bytes_free(packet.data,packet.size); return ok;
}
static bool atom_same(Atom *left, Atom *right, bool *equal) {
    Bytes a = {0}, b = {0};
    *equal=false;
    bool ok = wire(left, &a) && wire(right, &b);
    if (ok) *equal=same_bytes(a,b);
    bytes_free(&a); bytes_free(&b); return ok;
}
static bool has_nan(Atom *atom) {
    if (atom->kind == ATOM_GROUNDED)
        return atom->ground.gkind == GV_FLOAT && isnan(atom->ground.fval);
    if (atom->kind == ATOM_EXPR)
        for (CettaExprIndex i=0;i<atom->expr.len;++i)
            if (has_nan(atom->expr.elems[i])) return true;
    return false;
}
/* Alpha equality within a term. Ground membership uses the native index;
 * open terms and NaNs use canonical wire spelling, never query unification.
 * IEEE numeric equality cannot establish membership of a NaN's storage key.
 * Failed lookup is distinct from absence: it must never acknowledge a removal
 * as a successful no-op or admit a second occurrence to a set backend. */
static bool find_equal(Store *st, Atom *atom, Atom **found) {
    *found=NULL;
    if (!atom_has_vars(atom) && !has_nan(atom)) {
        /* Exact-index absence is authoritative only for indexable atoms.
         * Immutable tensor leaves use the retained-value fallback. */
        space_match_backend_clear_error();
        if (space_atom_is_exact_indexable(atom)) {
            bool present=space_contains_exact(st->space,atom);
            if (space_match_backend_last_error_code()!=SPACE_MATCH_BACKEND_ERROR_NONE) return false;
            if (!present) return true;
        }
        CettaIndex *indices = NULL;
        CettaIndex n = space_match_candidates64(st->space, atom, &indices);
        bool ok=space_match_backend_last_error_code()==SPACE_MATCH_BACKEND_ERROR_NONE && (!n || indices);
        for (CettaIndex i = 0; ok && i < n && !*found; ++i) {
            Atom *candidate = space_match_candidate_at64(st->space, indices[i]);
            bool equal=false;
            ok=candidate && atom_same(candidate,atom,&equal);
            if (ok && equal) *found=candidate;
        }
        free(indices); return ok;
    }
    CettaCount n = space_length64(st->space);
    for (CettaIndex i = 0; i < n; ++i) {
        Atom *candidate = space_get_at64(st->space, i);
        bool equal=false;
        if (!candidate || !atom_same(candidate,atom,&equal)) return false;
        if (equal) { *found=candidate; return true; }
    }
    return true;
}
static bool store_add(Store *st, Atom *atom) {
    Atom *present=NULL;
    if (st->engine==SPACE_ENGINE_MORK && !find_equal(st,atom,&present)) return false;
    Bytes b = {0};
    if (!wire(atom, &b)) return false;
    size_t size = b.size;
    /* Raw MORK reports one accepted upsert even when its set is unchanged. */
    bool ok=backend_change(st,b,true,1);
    bytes_free(&b); if (!ok) return false;
    if (present) return true;
    if (!space_admit_atom(st->space, &st->arena, atom)) return false;
    st->live_bytes += size; st->churn += size;
    return true;
}
static bool store_remove(Store *st, Atom *atom) {
    Atom *found=NULL;
    if (!find_equal(st,atom,&found)) return false;
    if (!found) return true;
    Bytes b = {0};
    if (!wire(found, &b)) return false;
    size_t size = b.size;
    bool ok=backend_change(st,b,false,1);
    bytes_free(&b); if (!ok) return false;
    AtomId identity=term_universe_lookup_atom_id(&st->universe,found);
    if (!space_remove_atom_id(st->space,identity)) return false;
    if (st->live_bytes < size) return false;
    st->live_bytes -= size; st->churn += size;
    return true;
}
static Atom *describe(Service *s, Arena *a, Store *st) {
    Atom *args[] = {atom_string(a,s->uuid), atom_int(a,st->id), atom_string(a,st->name),
        atom_symbol(a,engine_name(st->engine)), atom_symbol(a,st->engine == SPACE_ENGINE_MORK ? "set" : "bag"),
        atom_int(a,st->revision), atom_int(a,space_length64(st->space))};
    return form(a,"space",args,7);
}
static void snapshot_free(Service *s, Snapshot *q) {
    if (!q->id) return;
    for (size_t i = 0; i < q->count; ++i) bytes_free(&q->rows[i]);
    free(q->rows); s->snapshot_bytes -= q->size; *q = (Snapshot){0};
}
static void expire(Service *s) {
    uint64_t now = now_ms();
    for (size_t i = 0; i < SNAPSHOTS; ++i)
        if (s->snapshots[i].id && s->snapshots[i].expires <= now) snapshot_free(s,&s->snapshots[i]);
}
static Snapshot *find_snapshot(Service *s, uint64_t id) {
    for (size_t i = 0; i < SNAPSHOTS; ++i) if (s->snapshots[i].id == id && id) return &s->snapshots[i];
    return NULL;
}
static void receipt_free(Receipt *r) {
    bytes_free(&r->request); bytes_free(&r->reply); *r = (Receipt){0};
}
static Receipt *find_receipt(Service *s, const char *id) {
    for (size_t i = 0; i < s->receipt_count; ++i) if (!strcmp(s->receipts[i].id,id)) return &s->receipts[i];
    return NULL;
}
/* Prepare owned messages before publication. Installation only moves ownership
 * and evicts old receipts; it cannot fail after the durable decision. Recovery
 * already owns its decoded messages and uses the same preparation boundary. */
static bool receipt_prepare(Receipt *r, const char *id, Bytes *request, Bytes *answer) {
    if (request->size > RECEIPT_BYTES || answer->size > RECEIPT_BYTES-request->size)
        return false;
    snprintf(r->id,sizeof(r->id),"%s",id);
    r->request=*request; r->reply=*answer;
    *request=(Bytes){0}; *answer=(Bytes){0};
    return true;
}
static void remember(Service *s, Receipt *r) {
    size_t size=r->request.size+r->reply.size;
    while (s->receipt_count && (s->receipt_count == RECEIPTS ||
            s->receipt_bytes + size > RECEIPT_BYTES)) {
        s->receipt_bytes -= s->receipts[0].request.size + s->receipts[0].reply.size;
        receipt_free(&s->receipts[0]);
        memmove(s->receipts,s->receipts+1,(--s->receipt_count)*sizeof(*s->receipts));
    }
    s->receipts[s->receipt_count++]=*r;
    s->receipt_bytes+=size; *r=(Receipt){0};
}
static bool write_all(int fd, const void *data, size_t size) {
    const unsigned char *p = data;
    while (size) {
        ssize_t n = write(fd,p,size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n; size -= (size_t)n;
    }
    return true;
}
/* Complete records have a bounded length and SHA-256. A short last record is
 * an unsynchronized tail. A complete invalid record fails recovery closed. */
static bool journal_bytes(int fd, Bytes b, bool synchronize, uint64_t *written) {
    unsigned char header[72] = {'C','S','L','1'};
    for (size_t i = 0; i < 4; ++i) header[4+i] = (unsigned char)(b.size >> (24-8*i));
    char digest[65]; cetta_native_sha256_hex(b.data,b.size,digest);
    memcpy(header+8,digest,64);
    bool ok = write_all(fd,header,sizeof(header)) && write_all(fd,b.data,b.size);
    if (ok && synchronize) ok = fsync(fd) == 0;
    if (ok && written) *written += sizeof(header)+b.size;
    return ok;
}
static bool journal_record(int fd, Atom *record, bool synchronize, uint64_t *written) {
    Bytes b={0};
    bool ok=wire(record,&b) && journal_bytes(fd,b,synchronize,written);
    bytes_free(&b); return ok;
}
/* Live open moves the backend prepared at admission. Replay creates it from
 * the journal. Other mutations still fail closed if application cannot finish. */
static bool apply(Service *s, Atom *request, Store **prepared_open) {
    if (atom_is_symbol(part(request,2),"open")) {
        const char *name = NULL; SpaceEngine e; uint64_t rev;
        if (!token(part(request,4),&name) || !engine(part(request,5),&e) ||
            !integer(part(request,6),&rev) || rev != s->catalog_revision ||
            s->next_store >= INT64_MAX || find_name(s,name)) return false;
        size_t slot = store_slot(s);
        if (slot == STORES) return false;
        Store *st = prepared_open ? *prepared_open : store_new(s->next_store,name,e,0);
        if (!st) return false;
        if (prepared_open && (st->id!=s->next_store || strcmp(st->name,name) ||
            st->engine!=e || st->revision)) return false;
        s->stores[slot] = st; ++s->next_store; ++s->catalog_revision;
        if (prepared_open) *prepared_open=NULL;
        return true;
    }
    uint64_t id, rev;
    if (!integer(part(request,4),&id) || !integer(part(request,5),&rev)) return false;
    Store *st = find_store(s,id);
    if (!st || st->revision != rev || rev >= INT64_MAX) return false;
    if (atom_is_symbol(part(request,2),"drop")) {
        uint64_t catalog;
        if (!integer(part(request,6),&catalog) || catalog != s->catalog_revision) return false;
        for (size_t i = 0; i < STORES; ++i) if (s->stores[i] == st) {
            store_free(st); s->stores[i] = NULL; ++s->catalog_revision; return true;
        }
        return false;
    }
    Atom *ops = part(request,6);
    if (!ops || ops->kind != ATOM_EXPR || !atom_is_symbol(part(ops,0),"ops")) return false;
    for (size_t i = 1; i < ops->expr.len; ++i) {
        Atom *op = part(ops,i);
        if (shape(op,"add",2)) { if (!store_add(st,part(op,1))) return false; }
        else if (shape(op,"remove",2)) { if (!store_remove(st,part(op,1))) return false; }
        else return false;
    }
    ++st->revision; return store_consistent(st);
}
static Atom *committed(Service *s, Arena *a, Atom *request, uint64_t id, uint64_t rev) {
    Atom *args[] = {atom_string(a,s->uuid),atom_int(a,id),atom_int(a,rev)};
    return reply(a,request,true,form(a,"committed",args,3));
}
static Atom *commit(Service *s, Arena *a, Atom *request, Bytes raw, Atom *answer,
                    Store *prepared_open) {
    const char *id = NULL;
    Bytes input={0}, result={0}, record_bytes={0}; Receipt receipt={0};
    const char *reason="invalid-id";
    if (!token(part(request,1),&id)) goto refused;
    reason="limit";
    if (!wire(answer,&result)) goto refused;
    reason="allocation";
    if (!bytes_copy(&input,raw.data,raw.size)) goto refused;
    reason="receipt-limit";
    if (!receipt_prepare(&receipt,id,&input,&result)) goto refused;
    /* Persist the exact input bytes: number parsing can normalize spelling,
     * and decoding/re-encoding cannot reconstruct an idempotence key. */
    Atom *parts[] = {atom_string_n(a,(const char *)receipt.request.data,receipt.request.size),
                    atom_string_n(a,(const char *)receipt.reply.data,receipt.reply.size)};
    Atom *record = form(a,"event-bytes",parts,2);
    reason="limit";
    if (!wire(record,&record_bytes)) goto refused;
    bool recorded=journal_bytes(s->journal,record_bytes,true,&s->journal_bytes);
    bytes_free(&record_bytes);
    if (!recorded || !apply(s,request,&prepared_open)) {
        s->poisoned=true;
        receipt_free(&receipt); store_free(prepared_open);
        return NULL;
    }
    remember(s,&receipt);
    return answer;
refused:
    bytes_free(&input); bytes_free(&result); bytes_free(&record_bytes);
    receipt_free(&receipt); store_free(prepared_open);
    return error(a,request,reason);
}
static bool service_matches(Service *s, Atom *a) {
    const char *uuid = NULL; return text(a,&uuid) && !strcmp(uuid,s->uuid);
}
static bool boot_matches(Service *s, Atom *a) {
    const char *boot = NULL; return text(a,&boot) && !strcmp(boot,s->boot);
}
static Atom *query(Service *s, Arena *a, Atom *request, Store *st, bool counting) {
    bool profile=!counting && (atom_is_symbol(part(request,2),"query-profile") ||
                              atom_is_symbol(part(request,2),"query-first-profile"));
    bool first=!counting && (atom_is_symbol(part(request,2),"query-first") ||
                            atom_is_symbol(part(request,2),"query-first-profile"));
    uint64_t limit = 0, lease = 0;
    if (!counting && (!integer(part(request,7),&limit) || !integer(part(request,8),&lease) ||
        !limit || limit > QUERY_ROWS || lease < 1 || lease > 300000)) return error(a,request,"limit");
    expire(s);
    size_t slot = SNAPSHOTS;
    if (!counting) {
        for (size_t i = 0; i < SNAPSHOTS; ++i) if (!s->snapshots[i].id) { slot = i; break; }
        if (slot == SNAPSHOTS || s->next_cursor >= INT64_MAX) return error(a,request,"snapshot-limit");
    }
    Atom *pattern = part(request,5), *projection = counting ? pattern : part(request,6);
    SpaceOccurrenceCursor cursor;
    space_occurrence_cursor_init_empty(&cursor);
    if (!store_consistent(st) || !space_occurrence_cursor_init(st->space,pattern,&cursor))
        return error(a,request,"backend");
    Snapshot q = {.id=s->next_cursor,.source=st->id,.revision=st->revision};
    if (!counting) {
        q.rows = calloc((size_t)limit,sizeof(*q.rows));
        if (!q.rows) { space_occurrence_cursor_release(&cursor); return error(a,request,"memory"); }
    }
    uint64_t matches = 0, examined = 0, deadline = now_ms() + s->query_ms;
    const char *failure = NULL;
    CettaIndex index;
    SpaceOccurrenceCursorStep step=SPACE_OCCURRENCE_CURSOR_END;
    for (;;) {
        step=space_occurrence_cursor_next(&cursor,&index);
        if (step!=SPACE_OCCURRENCE_CURSOR_ITEM) break;
        if ((examined++ & 255u) == 0 && now_ms() >= deadline) { failure="query-time-limit"; break; }
        Atom *source = space_occurrence_cursor_atom(&cursor,index);
        if (!source) { failure="backend"; break; }
        Arena scratch; arena_init_detached(&scratch);
        Atom *row = source;
        Bytes fresh = {0};
        if (atom_has_vars(source)) {
            if (!wire(source,&fresh) || !decode(&scratch,fresh,&row)) failure="backend";
            bytes_free(&fresh);
        }
        Bindings bindings; bindings_init(&bindings);
        if (!failure && match_atoms(pattern,row,&bindings,&scratch)) {
            ++matches;
            if (!counting) {
                if (q.count == limit) failure="query-row-limit";
                else {
                    Atom *value = bindings_apply(&bindings,&scratch,projection);
                    Bytes b = {0};
                    if (!value || !wire(value,&b)) failure="query-value-limit";
                    else if (b.size > s->max_query_bytes - q.size ||
                        b.size > s->max_snapshot_bytes - s->snapshot_bytes - q.size) {
                        bytes_free(&b); failure="query-byte-limit";
                    } else { q.rows[q.count++] = b; q.size += b.size; }
                }
            }
        }
        bindings_free(&bindings); arena_free(&scratch);
        if (failure || (first && q.count)) break;
    }
    space_occurrence_cursor_release(&cursor);
    if (!failure && step == SPACE_OCCURRENCE_CURSOR_INVALIDATED) failure="backend";
    if (failure) {
        for (size_t i = 0; i < q.count; ++i) bytes_free(&q.rows[i]);
        free(q.rows); return error(a,request,failure);
    }
    if (counting) return reply(a,request,true,atom_int(a,matches));
    q.expires = now_ms()+lease;
    s->snapshots[slot] = q; s->snapshot_bytes += q.size; ++s->next_cursor;
    Atom *args[] = {atom_string(a,s->boot),atom_int(a,q.id),atom_int(a,q.source),
        atom_int(a,q.revision),atom_int(a,q.count),atom_int(a,lease)};
    /* A first-occurrence selection is explicitly different from an
     * exhaustive snapshot. It preserves the native occurrence order. */
    Atom *view=form(a,first ? "selection" : "snapshot",args,6);
    if (profile) {
        CettaSpacePathIndexInfo info={0};
        space_path_indexes_info(st->space,&info);
        Atom *metrics[]={atom_int(a,examined),atom_int(a,info.index_count),
            atom_int(a,info.source_rows),atom_int(a,info.keyed_rows),atom_int(a,info.uncertain_rows)};
        Atom *values[]={view,form(a,"query-work",metrics,5)};
        view=form(a,"profile",values,2);
    }
    return reply(a,request,true,view);
}
static bool compact(Service *s);
static Atom *dispatch(Service *s, Client *client, Arena *a, Atom *request, Bytes raw) {
    const char *id = NULL;
    if (!request || request->kind != ATOM_EXPR || request->expr.len < 3 ||
        !atom_is_symbol(part(request,0),"request") || !token(part(request,1),&id)) return NULL;
    if (s->poisoned) return NULL;
    Receipt *old = find_receipt(s,id);
    if (old) {
        if (!same_bytes(old->request,raw)) return error(a,request,"reused-id");
        Atom *answer = NULL;
        return decode(a,old->reply,&answer) ? answer : NULL;
    }
    if (shape(request,"request",3) && atom_is_symbol(part(request,2),"hello")) {
        Atom *caps[] = {atom_symbol(a,"conditional-batches"),atom_symbol(a,"snapshot-pages"),
            atom_symbol(a,"bounded-receipts"),atom_symbol(a,"journal-checkpoint"),atom_symbol(a,"no-eval"),
            atom_symbol(a,"portable-value-keys"),atom_symbol(a,"native-match-index"),
            atom_symbol(a,"tensor-ctn1-native"),atom_symbol(a,"tensor-f32-native"),
            atom_symbol(a,"revision-waits"),atom_symbol(a,"first-occurrence")};
        Atom *args[] = {atom_int(a,1),atom_string(a,s->uuid),atom_string(a,s->boot),
            atom_int(a,s->catalog_revision),form(a,"capabilities",caps,11)};
        return reply(a,request,true,form(a,"service",args,5));
    }
    if (atom_is_symbol(part(request,2),"fetch") || atom_is_symbol(part(request,2),"close")) {
        bool closing = atom_is_symbol(part(request,2),"close"); uint64_t cursor;
        if (request->expr.len != (closing ? 5 : 7) || !boot_matches(s,part(request,3)) ||
            !integer(part(request,4),&cursor)) return error(a,request,"cursor-identity");
        Snapshot *q = find_snapshot(s,cursor);
        if (!q) return error(a,request,"expired-or-closed");
        if (q->expires <= now_ms()) { snapshot_free(s,q); return error(a,request,"expired-or-closed"); }
        if (closing) { snapshot_free(s,q); return reply(a,request,true,atom_symbol(a,"closed")); }
        uint64_t offset,count;
        if (!integer(part(request,5),&offset) || !integer(part(request,6),&count) ||
            offset > q->count || !count || count > PAGE_ROWS) return error(a,request,"page-limit");
        if (count > q->count - offset) count = q->count - offset;
        Atom **rows = arena_alloc(a,count*sizeof(*rows));
        size_t total = 0;
        for (size_t i = 0; i < count; ++i) {
            total += q->rows[offset+i].size;
            if (total > CETTA_ATOM_WIRE_BYTES - 1024) return error(a,request,"page-byte-limit");
            if (!decode(a,q->rows[offset+i],&rows[i])) return error(a,request,"corrupt");
        }
        Atom *args[] = {atom_int(a,q->revision),atom_int(a,offset),form(a,"rows",rows,count),
            atom_int(a,offset+count),atom_bool(a,offset+count == q->count)};
        return reply(a,request,true,form(a,"page",args,5));
    }
    if (!service_matches(s,part(request,3))) return error(a,request,"service-identity");
    if (atom_is_symbol(part(request,2),"list") && request->expr.len == 4) {
        Atom *spaces[STORES]; size_t count=0;
        for (size_t i=0;i<STORES;++i) if (s->stores[i])
            spaces[count++]=describe(s,a,s->stores[i]);
        Atom *args[]={atom_string(a,s->uuid),atom_int(a,s->catalog_revision),
                      form(a,"spaces",spaces,count)};
        return reply(a,request,true,form(a,"catalog",args,3));
    }
    if (atom_is_symbol(part(request,2),"receipt") && request->expr.len == 5) {
        const char *target = NULL;
        if (!token(part(request,4),&target)) return error(a,request,"invalid-id");
        Receipt *r = find_receipt(s,target); Atom *value = NULL;
        if (r && !decode(a,r->reply,&value)) return NULL;
        return reply(a,request,true,r ? value : atom_symbol(a,"unknown-receipt"));
    }
    if (atom_is_symbol(part(request,2),"compact") && request->expr.len == 4) {
        if (!compact(s)) { s->poisoned=true; return NULL; }
        return reply(a,request,true,atom_symbol(a,"compacted"));
    }
    if (atom_is_symbol(part(request,2),"open") && request->expr.len == 7) {
        const char *name = NULL; SpaceEngine e; uint64_t revision;
        if (!token(part(request,4),&name) || !engine(part(request,5),&e) ||
            !integer(part(request,6),&revision)) return error(a,request,"invalid-open");
        Store *existing = find_name(s,name);
        if (existing) return existing->engine == e ? reply(a,request,true,describe(s,a,existing))
                                                  : error(a,request,"backend-mismatch");
        if (revision != s->catalog_revision) return error(a,request,"stale-catalog");
        if (store_slot(s) == STORES || s->next_store >= INT64_MAX || s->catalog_revision >= INT64_MAX)
            return error(a,request,"space-limit");
        if (e != SPACE_ENGINE_NATIVE && !cetta_mork_bridge_is_available())
            return error(a,request,"backend-unavailable");
        /* Allocate the empty backend before durably promising its creation. */
        Store *probe = store_new(s->next_store,name,e,0);
        if (!probe) return error(a,request,"backend");
        Atom *answer = reply(a,request,true,describe(s,a,probe));
        return commit(s,a,request,raw,answer,probe);
    }
    uint64_t store_id;
    if (!integer(part(request,4),&store_id)) return error(a,request,"invalid-space");
    Store *st = find_store(s,store_id);
    if (!st) return error(a,request,"missing-space");
    if (atom_is_symbol(part(request,2),"watch") && request->expr.len == 7) {
        uint64_t revision, duration;
        if (!integer(part(request,5),&revision) || revision > st->revision ||
            !integer(part(request,6),&duration) || duration > INT_MAX)
            return error(a,request,"invalid-watch");
        if (revision != st->revision || !duration) {
            Atom *args[]={atom_string(a,s->uuid),atom_int(a,st->id),atom_int(a,st->revision)};
            return reply(a,request,true,form(a,revision != st->revision ? "changed" : "unchanged",args,3));
        }
        /* Registration and revision comparison run on the storage owner.
         * No commit can occur between the check and the saved wait. This is
         * a read notification, never an application receive or a receipt. */
        if (s->watchers >= s->max_watchers) return error(a,request,"watch-capacity");
        client->watching=true; ++s->watchers;
        client->watch_source=st->id; client->watch_revision=revision;
        client->watch_due=now_ms()+duration;
        client->deadline=client->watch_due+30000;
        return NULL;
    }
    if (atom_is_symbol(part(request,2),"describe") && request->expr.len == 5)
        return reply(a,request,true,describe(s,a,st));
    if (atom_is_symbol(part(request,2),"verify") && request->expr.len == 5) {
        if (space_length64(st->space)>QUERY_ROWS || st->live_bytes>QUERY_BYTES)
            return error(a,request,"verification-limit");
        if (!store_consistent(st) || !verify_backend(st))
            return error(a,request,"backend-divergence");
        Atom *args[]={atom_string(a,s->uuid),atom_int(a,st->id),
                      atom_int(a,st->revision),atom_int(a,space_length64(st->space))};
        return reply(a,request,true,form(a,"verified",args,4));
    }
    if ((atom_is_symbol(part(request,2),"query") || atom_is_symbol(part(request,2),"query-first") ||
         atom_is_symbol(part(request,2),"query-profile") || atom_is_symbol(part(request,2),"query-first-profile")) && request->expr.len == 9)
        return query(s,a,request,st,false);
    if (atom_is_symbol(part(request,2),"count") && request->expr.len == 6)
        return query(s,a,request,st,true);
    uint64_t revision;
    if (!integer(part(request,5),&revision) || revision != st->revision)
        return error(a,request,"stale-revision");
    if (st->revision >= INT64_MAX) return error(a,request,"revision-limit");
    if (atom_is_symbol(part(request,2),"drop") && request->expr.len == 7) {
        uint64_t catalog;
        if (!integer(part(request,6),&catalog) || catalog != s->catalog_revision)
            return error(a,request,"stale-catalog");
        if (catalog >= INT64_MAX) return error(a,request,"revision-limit");
        return commit(s,a,request,raw,reply(a,request,true,atom_symbol(a,"dropped")),NULL);
    }
    if (!atom_is_symbol(part(request,2),"batch") || request->expr.len != 7)
        return error(a,request,"unsupported-operation");
    Atom *ops = part(request,6);
    if (!ops || ops->kind != ATOM_EXPR || !atom_is_symbol(part(ops,0),"ops") ||
        ops->expr.len > 4097) return error(a,request,"invalid-batch");
    uint64_t added = 0, added_bytes = 0;
    for (size_t i = 1; i < ops->expr.len; ++i) {
        Atom *op = part(ops,i); Bytes b = {0};
        if (!(shape(op,"add",2) || shape(op,"remove",2))) return error(a,request,"invalid-batch");
        if (!wire(part(op,1),&b)) return error(a,request,"unsupported-value");
        size_t size = b.size; bytes_free(&b);
        if (size > FACT_BYTES) return error(a,request,"fact-limit");
        if (shape(op,"add",2)) { ++added; added_bytes += size; }
    }
    if (space_length64(st->space) + added > s->max_facts ||
        st->live_bytes + added_bytes > s->max_live_bytes) return error(a,request,"space-limit");
    /* Admission is conservative: a batch at the limit may remove first and
     * add later, but no effects are attempted until the whole batch fits. */
    return commit(s,a,request,raw,committed(s,a,request,st->id,st->revision+1),NULL);
}

static bool record_messages(Arena *a,Atom *record,bool raw,Bytes *req,Bytes *ans,
                            Atom **request,Atom **answer) {
    if (raw) {
        Atom *r=part(record,1),*v=part(record,2);
        if (!r || !v || r->kind!=ATOM_GROUNDED || r->ground.gkind!=GV_STRING ||
            v->kind!=ATOM_GROUNDED || v->ground.gkind!=GV_STRING ||
            !bytes_copy(req,(const unsigned char *)r->ground.sval,atom_string_len(r)) ||
            !bytes_copy(ans,(const unsigned char *)v->ground.sval,atom_string_len(v)) ||
            !decode(a,*req,request) || !decode(a,*ans,answer)) return false;
    } else {
        /* Read the initial syntax-record prototype without changing its values.
         * New journals/checkpoints use exact byte messages exclusively. */
        *request=part(record,1); *answer=part(record,2);
        if (!wire(*request,req) || !wire(*answer,ans)) return false;
    }
    bool same=false;
    return *request && (*request)->kind==ATOM_EXPR && (*request)->expr.len>=3 &&
        atom_is_symbol(part(*request,0),"request") && shape(*answer,"reply",4) &&
        atom_is_symbol(part(*answer,2),"ok") &&
        atom_same(part(*request,1),part(*answer,1),&same) && same;
}
static bool restore_record(Service *s, Arena *a, Atom *record, bool first) {
    if (first) {
        const char *uuid = NULL;
        if (!shape(record,"header",3) || !text(part(record,1),&uuid) || strlen(uuid)!=32 ||
            !part(record,2) || part(record,2)->kind!=ATOM_GROUNDED ||
            part(record,2)->ground.gkind!=GV_INT || part(record,2)->ground.ival!=1) return false;
        for (size_t i=0;i<32;++i) if (!strchr("0123456789abcdef",uuid[i])) return false;
        memcpy(s->uuid,uuid,33); s->next_store=1; return true;
    }
    if (shape(record,"catalog",3)) {
        if (!integer(part(record,1),&s->next_store) || !s->next_store ||
            !integer(part(record,2),&s->catalog_revision)) return false;
        s->restoring=true; return true;
    }
    if (shape(record,"restore-space",5) && s->restoring) {
        uint64_t id,rev; const char *name=NULL; SpaceEngine e;
        if (!integer(part(record,1),&id) || !id || id>=s->next_store || find_store(s,id) ||
            !token(part(record,2),&name) || find_name(s,name) || !engine(part(record,3),&e) ||
            !integer(part(record,4),&rev)) return false;
        size_t slot=store_slot(s); if (slot==STORES) return false;
        s->stores[slot]=store_new(id,name,e,rev); return s->stores[slot]!=NULL;
    }
    if (shape(record,"restore-rows",3) && s->restoring) {
        uint64_t id; Atom *rows=part(record,2);
        if (!integer(part(record,1),&id) || !rows || rows->kind!=ATOM_EXPR ||
            !atom_is_symbol(part(rows,0),"rows")) return false;
        Store *st=find_store(s,id); if (!st) return false;
        for (size_t i=1;i<rows->expr.len;++i) if (!store_add(st,part(rows,i))) return false;
        return store_consistent(st) && space_length64(st->space)<=s->max_facts && st->live_bytes<=s->max_live_bytes;
    }
    bool raw_receipt=shape(record,"receipt-bytes",3);
    if ((raw_receipt || shape(record,"receipt",3)) && s->restoring) {
        const char *id=NULL; Bytes req={0},ans={0}; Atom *request=NULL,*answer=NULL;
        Receipt receipt={0};
        bool ok=record_messages(a,record,raw_receipt,&req,&ans,&request,&answer) &&
            token(part(request,1),&id) && !find_receipt(s,id) &&
            receipt_prepare(&receipt,id,&req,&ans);
        if (ok) remember(s,&receipt);
        bytes_free(&req); bytes_free(&ans); return ok;
    }
    if (shape(record,"sealed",4) && s->restoring) {
        uint64_t spaces=0,rows=0,expected_spaces,expected_rows,expected_receipts;
        for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
            ++spaces; rows+=space_length64(s->stores[i]->space);
        }
        if (!integer(part(record,1),&expected_spaces) ||
            !integer(part(record,2),&expected_rows) ||
            !integer(part(record,3),&expected_receipts) || spaces!=expected_spaces ||
            rows!=expected_rows || s->receipt_count!=expected_receipts) return false;
        s->restoring=false; return true;
    }
    if (s->restoring) return false;
    bool raw_event=shape(record,"event-bytes",3);
    if (!raw_event && !shape(record,"event",3)) return false;
    s->restoring=false;
    Atom *request=NULL,*answer=NULL;
    const char *id=NULL; Bytes req={0},ans={0};
    if (!record_messages(a,record,raw_event,&req,&ans,&request,&answer) ||
        !token(part(request,1),&id) || !service_matches(s,part(request,3)) ||
        find_receipt(s,id) || !apply(s,request,NULL)) {
        bytes_free(&req); bytes_free(&ans); return false;
    }
    Receipt receipt={0};
    bool ok=receipt_prepare(&receipt,id,&req,&ans);
    if (ok) remember(s,&receipt);
    bytes_free(&req); bytes_free(&ans); return ok;
}
static bool recover(Service *s) {
    struct stat st; if (fstat(s->journal,&st)) return false;
    if (!st.st_size) {
        if (!random_id(s->uuid)) return false;
        Arena a; arena_init_detached(&a);
        Atom *args[]={atom_string(&a,s->uuid),atom_int(&a,1)};
        bool ok=journal_record(s->journal,form(&a,"header",args,2),true,&s->journal_bytes) && fsync(s->directory)==0;
        arena_free(&a); s->next_store=1; return ok;
    }
    off_t position=0; bool first=true;
    while (position<st.st_size) {
        unsigned char header[72];
        ssize_t got=pread(s->journal,header,sizeof(header),position);
        if (got<0 && errno==EINTR) continue;
        if (got<0) return false;
        if (got<(ssize_t)sizeof(header)) break;
        if (memcmp(header,"CSL1",4)) return false;
        size_t n=0; for (size_t i=4;i<8;++i) n=(n<<8)|header[i];
        if (n<4 || n>CETTA_ATOM_WIRE_BYTES) return false;
        if ((uint64_t)n>(uint64_t)(st.st_size-position-(off_t)sizeof(header))) break;
        Bytes b={malloc(n),n}; if (!b.data) return false;
        size_t pos=0;
        while (pos<n) {
            got=pread(s->journal,b.data+pos,n-pos,position+(off_t)sizeof(header)+(off_t)pos);
            if (got<0 && errno==EINTR) continue;
            if (got<=0) { bytes_free(&b); return false; }
            pos+=(size_t)got;
        }
        char digest[65]; cetta_native_sha256_hex(b.data,b.size,digest);
        if (memcmp(digest,header+8,64)) { bytes_free(&b); return false; }
        Arena a; arena_init_detached(&a); Atom *record=NULL;
        bool ok=decode(&a,b,&record) && restore_record(s,&a,record,first);
        if (ok && shape(record,"sealed",4))
            s->checkpoint_bytes=(uint64_t)(position+(off_t)(sizeof(header)+n));
        arena_free(&a); bytes_free(&b); if (!ok) return false;
        first=false; position+=(off_t)(sizeof(header)+n);
    }
    if (first || s->restoring) return false;
    if (position!=st.st_size) {
        fprintf(stderr,"space-service: discarding incomplete journal tail\n");
        if (ftruncate(s->journal,position) || fsync(s->journal)) return false;
    }
    if (lseek(s->journal,position,SEEK_SET)<0) return false;
    s->journal_bytes=(uint64_t)position; s->restoring=false; return true;
}

static bool write_checkpoint(Service *s,int fd,uint64_t *size) {
    Arena a; arena_init_detached(&a);
    Atom *header[]={atom_string(&a,s->uuid),atom_int(&a,1)};
    Atom *catalog[]={atom_int(&a,s->next_store),atom_int(&a,s->catalog_revision)};
    bool ok=journal_record(fd,form(&a,"header",header,2),false,size) &&
        journal_record(fd,form(&a,"catalog",catalog,2),false,size);
    arena_free(&a);
    for (size_t i=0;ok && i<STORES;++i) {
        Store *st=s->stores[i]; if (!st) continue;
        arena_init_detached(&a);
        Atom *args[]={atom_int(&a,st->id),atom_string(&a,st->name),
            atom_symbol(&a,engine_name(st->engine)),atom_int(&a,st->revision)};
        ok=journal_record(fd,form(&a,"restore-space",args,4),false,size);
        arena_free(&a);
        CettaCount count=space_length64(st->space);
        for (CettaIndex row=0;ok && row<count;) {
            Atom *rows[128]; size_t used=0,bytes=0;
            while (row<count && used<128) {
                Atom *value=space_get_at64(st->space,row); Bytes b={0};
                if (!value || !wire(value,&b)) { ok=false; break; }
                size_t n=b.size; bytes_free(&b);
                if (used && (bytes+n>32768 || atom_has_vars(value) ||
                             atom_has_vars(rows[0]))) break;
                rows[used++]=value; bytes+=n; ++row;
            }
            if (!ok) break;
            arena_init_detached(&a);
            Atom *parts[]={atom_int(&a,st->id),form(&a,"rows",rows,used)};
            ok=journal_record(fd,form(&a,"restore-rows",parts,2),false,size);
            arena_free(&a);
        }
    }
    for (size_t i=0;ok && i<s->receipt_count;++i) {
        arena_init_detached(&a);
        Receipt *r=&s->receipts[i];
        Atom *parts[]={atom_string_n(&a,(const char *)r->request.data,r->request.size),
                      atom_string_n(&a,(const char *)r->reply.data,r->reply.size)};
        ok=journal_record(fd,form(&a,"receipt-bytes",parts,2),false,size);
        arena_free(&a);
    }
    if (ok) {
        uint64_t spaces=0,rows=0;
        for (size_t i=0;i<STORES;++i) if (s->stores[i]) {
            ++spaces; rows+=space_length64(s->stores[i]->space);
        }
        arena_init_detached(&a);
        Atom *parts[]={atom_int(&a,spaces),atom_int(&a,rows),atom_int(&a,s->receipt_count)};
        ok=journal_record(fd,form(&a,"sealed",parts,3),false,size);
        arena_free(&a);
    }
    return ok;
}
static bool compact(Service *s) {
    /* Rebuild ownership from live terms. Query rows are independently owned,
     * so removing the old universe releases removed terms and old frontiers. */
    for (size_t i=0;i<STORES;++i) {
        Store *old=s->stores[i]; if (!old || !old->churn) continue;
        Store *fresh=store_new(old->id,old->name,old->engine,old->revision);
        if (!fresh) return false;
        bool ok=true;
        CettaCount count=space_length64(old->space);
        for (CettaIndex row=0;ok && row<count;++row) {
            Arena a; arena_init_detached(&a); Bytes b={0}; Atom *value=NULL;
            ok=wire(space_get_at64(old->space,row),&b) && decode(&a,b,&value) && store_add(fresh,value);
            bytes_free(&b); arena_free(&a);
        }
        if (!ok) { store_free(fresh); return false; }
        fresh->churn=0; s->stores[i]=fresh; store_free(old);
    }
    int fd=openat(s->directory,"checkpoint.new",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    if (fd<0) return false;
    uint64_t size=0;
    bool ok=write_checkpoint(s,fd,&size) && fsync(fd)==0;
    if (ok) ok=renameat(s->directory,"checkpoint.new",s->directory,"journal.csl")==0;
    if (ok) ok=fsync(s->directory)==0;
    if (ok) { close(s->journal); s->journal=fd; s->journal_bytes=size; s->checkpoint_bytes=size; }
    else { close(fd); unlinkat(s->directory,"checkpoint.new",0); }
    return ok;
}
static void client_close(Service *s,Client *c) {
    if (c->watching) --s->watchers;
    if (c->fd>=0) close(c->fd);
    bytes_free(&c->input); bytes_free(&c->output);
    *c=(Client){.fd=-1};
}
static bool client_reply(Service *s,Client *c,Arena *a,Atom *request,Atom *answer) {
    Bytes b={0}; bool ok=answer && wire(answer,&b);
    if (!ok && answer && !s->poisoned) ok=wire(error(a,request,"reply-limit"),&b);
    if (ok) {
        c->output.size=b.size+8; c->output.data=malloc(c->output.size);
        if (!c->output.data) ok=false;
        else {
            memcpy(c->output.data,"CSP1",4);
            for (size_t i=0;i<4;++i) c->output.data[4+i]=(unsigned char)(b.size>>(24-8*i));
            memcpy(c->output.data+8,b.data,b.size);
            c->deadline=now_ms()+30000;
        }
    }
    bytes_free(&b); return ok;
}
static bool respond(Service *s,Client *c) {
    Arena a; arena_init_detached(&a); Atom *request=NULL;
    bool ok=decode(&a,c->input,&request);
    Atom *answer=ok ? dispatch(s,c,&a,request,c->input) : NULL;
    if (c->watching) ok=true;
    else ok=client_reply(s,c,&a,request,answer);
    arena_free(&a); return ok;
}
static bool client_wake(Service *s,Client *c) {
    if (!c->watching) return true;
    Store *st=find_store(s,c->watch_source);
    if (st && st->revision==c->watch_revision && now_ms()<c->watch_due) return true;
    Arena a; arena_init_detached(&a); Atom *request=NULL,*answer=NULL;
    bool ok=decode(&a,c->input,&request);
    if (ok && !st) answer=error(&a,request,"missing-space");
    else if (ok) {
        Atom *args[]={atom_string(&a,s->uuid),atom_int(&a,st->id),atom_int(&a,st->revision)};
        answer=reply(&a,request,true,form(&a,st->revision!=c->watch_revision ? "changed" : "unchanged",args,3));
    }
    c->watching=false; --s->watchers;
    ok=ok && client_reply(s,c,&a,request,answer);
    arena_free(&a); return ok;
}
static bool client_read(Service *s,Client *c) {
    while (c->header_read<8) {
        ssize_t n=recv(c->fd,c->header+c->header_read,8-c->header_read,0);
        if (n>0) c->header_read+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else return n<0 && (errno==EAGAIN || errno==EWOULDBLOCK);
    }
    if (!c->input.data) {
        if (memcmp(c->header,"CSP1",4)) return false;
        size_t size=0; for (size_t i=4;i<8;++i) size=(size<<8)|c->header[i];
        if (size<4 || size>CETTA_ATOM_WIRE_BYTES) return false;
        c->input=(Bytes){malloc(size),size}; if (!c->input.data) return false;
    }
    while (c->input_read<c->input.size) {
        ssize_t n=recv(c->fd,c->input.data+c->input_read,c->input.size-c->input_read,0);
        if (n>0) c->input_read+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else return n<0 && (errno==EAGAIN || errno==EWOULDBLOCK);
    }
    return respond(s,c);
}
static bool client_write(Client *c) {
    while (c->output_sent<c->output.size) {
        ssize_t n=send(c->fd,c->output.data+c->output_sent,c->output.size-c->output_sent,MSG_NOSIGNAL);
        if (n>0) c->output_sent+=(size_t)n;
        else if (n<0 && errno==EINTR) continue;
        else return n<0 && (errno==EAGAIN || errno==EWOULDBLOCK);
    }
    bytes_free(&c->input); bytes_free(&c->output);
    c->header_read=c->input_read=c->output_sent=0; c->deadline=now_ms()+30000;
    return true;
}
static int listen_socket(const char *path) {
    struct sockaddr_un address={.sun_family=AF_UNIX};
    size_t n=strlen(path);
    if (path[0]!='/' || n>=sizeof(address.sun_path)) return -1;
    memcpy(address.sun_path,path,n+1);
    struct stat st;
    if (!lstat(path,&st)) {
        if (!S_ISSOCK(st.st_mode) || st.st_uid!=geteuid()) return -1;
        int probe=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
        if (probe<0) return -1;
        int result=connect(probe,(struct sockaddr *)&address,sizeof(address));
        int saved=errno; close(probe);
        if (!result || (saved!=ECONNREFUSED && saved!=ENOENT)) return -1;
        if (unlink(path)) return -1;
    } else if (errno!=ENOENT) return -1;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if (fd<0) return -1;
    if (bind(fd,(struct sockaddr *)&address,sizeof(address)) || chmod(path,0600) || listen(fd,CLIENTS)) {
        close(fd); return -1;
    }
    return fd;
}
static bool directory_open(Service *s,const char *path) {
    if (mkdir(path,0700) && errno!=EEXIST) return false;
    s->directory=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (s->directory<0) return false;
    struct stat st;
    if (fstat(s->directory,&st) || st.st_uid!=geteuid() || (st.st_mode&077)) return false;
    s->lock=openat(s->directory,"writer.lock",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if (s->lock<0 || flock(s->lock,LOCK_EX|LOCK_NB)) return false;
    s->journal=openat(s->directory,"journal.csl",O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    return s->journal>=0;
}
static bool argument_number(const char *text,uint64_t *value) {
    char *end=NULL; errno=0;
    unsigned long long n=strtoull(text,&end,10);
    if (!*text || text[0]=='-' || errno || *end || !n || n>INT64_MAX) return false;
    *value=(uint64_t)n; return true;
}
int main(int argc,char **argv) {
    const char *path=NULL,*directory=NULL;
    Service *s=calloc(1,sizeof(*s)); if (!s) return 1;
    s->journal=s->directory=s->lock=-1;
    s->next_cursor=1;
    s->max_facts=1000000; s->max_live_bytes=64*1024*1024;
    s->max_query_bytes=QUERY_BYTES; s->max_snapshot_bytes=SNAPSHOT_BYTES;
    s->max_journal_bytes=32*1024*1024; s->query_ms=5000;
    s->max_watchers=CLIENTS/2;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--help")) {
            puts("cetta-space-service --socket ABSOLUTE_PATH --data PRIVATE_DIRECTORY\n"
                 "  [--max-facts N] [--max-live-bytes N] [--max-journal-bytes N] [--query-ms N]\n"
                 "  [--max-query-bytes N] [--max-snapshot-bytes N] [--max-watchers N]");
            free(s); return 0;
        }
        if (i+1>=argc) goto invalid;
        const char *option=argv[i],*value=argv[++i];
        if (!strcmp(option,"--socket")) path=value;
        else if (!strcmp(option,"--data")) directory=value;
        else {
            uint64_t n; if (!argument_number(value,&n)) goto invalid;
            if (!strcmp(option,"--max-facts")) s->max_facts=n;
            else if (!strcmp(option,"--max-live-bytes")) s->max_live_bytes=n;
            else if (!strcmp(option,"--max-journal-bytes")) s->max_journal_bytes=n;
            else if (!strcmp(option,"--max-query-bytes")) s->max_query_bytes=n;
            else if (!strcmp(option,"--max-snapshot-bytes")) s->max_snapshot_bytes=n;
            else if (!strcmp(option,"--query-ms") && n<=60000) s->query_ms=(unsigned)n;
            else if (!strcmp(option,"--max-watchers") && n<CLIENTS) s->max_watchers=(size_t)n;
            else goto invalid;
        }
    }
    if (!path || !directory || directory[0]!='/') goto invalid;
    umask(0077);
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols=&symbols;
    symbol_table_init_builtins(&symbols,&g_builtin_syms);
    int listener=-1, result=1;
    if (!random_id(s->boot) || !directory_open(s,directory) || !recover(s)) {
        fprintf(stderr,"space-service: storage unavailable or corrupt\n"); goto done;
    }
    listener=listen_socket(path);
    if (listener<0) { fprintf(stderr,"space-service: socket unavailable\n"); goto done; }
    struct stat socket_identity;
    if (lstat(path,&socket_identity)) goto done;
    struct sigaction action={.sa_handler=stop}; sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,NULL); sigaction(SIGTERM,&action,NULL);
    signal(SIGPIPE,SIG_IGN);
    /* Check the stop flag with these signals blocked, then unblock them
     * atomically in ppoll. A stop between the check and poll must not wait
     * for the ordinary 30-second idle deadline. */
    sigset_t stop_signals, wait_signals;
    sigemptyset(&stop_signals);
    sigaddset(&stop_signals,SIGINT); sigaddset(&stop_signals,SIGTERM);
    if (sigprocmask(SIG_BLOCK,&stop_signals,&wait_signals)) goto done;
    Client clients[CLIENTS]; for (size_t i=0;i<CLIENTS;++i) clients[i]=(Client){.fd=-1};
    fprintf(stderr,"space-service: ready\n");
    while (!stopping && !s->poisoned) {
        struct pollfd pollfds[CLIENTS+1]={{listener,POLLIN,0}};
        uint64_t current=now_ms(), deadline=current+30000;
        for (size_t i=0;i<CLIENTS;++i) {
            Client *c=&clients[i];
            if (c->fd>=0 && !client_wake(s,c)) client_close(s,c);
            pollfds[i+1]=(struct pollfd){c->fd,c->output.data ? POLLOUT : POLLIN,0};
            if (c->fd>=0 && c->deadline<deadline) deadline=c->deadline;
            if (c->watching && c->watch_due<deadline) deadline=c->watch_due;
        }
        for (size_t i=0;i<SNAPSHOTS;++i)
            if (s->snapshots[i].id && s->snapshots[i].expires<deadline) deadline=s->snapshots[i].expires;
        current=now_ms();
        int timeout=deadline>current ? (int)(deadline-current) : 0;
        struct timespec wait_time={timeout/1000,(timeout%1000)*1000000L};
        int n=ppoll(pollfds,CLIENTS+1,&wait_time,&wait_signals);
        if (n<0 && errno==EINTR) continue;
        if (n<0) { s->poisoned=true; break; }
        if (pollfds[0].revents&POLLIN) {
            int fd=accept4(listener,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);
            if (fd>=0) {
                size_t slot=CLIENTS;
                for (size_t i=0;i<CLIENTS;++i) if (clients[i].fd<0) {slot=i;break;}
                if (slot==CLIENTS) close(fd);
                else clients[slot]=(Client){.fd=fd,.deadline=now_ms()+30000};
            }
        }
        for (size_t i=0;i<CLIENTS && !s->poisoned;++i) {
            Client *c=&clients[i]; if (c->fd<0) continue;
            short events=pollfds[i+1].revents;
            bool ok=true;
            if (events&POLLOUT) ok=client_write(c);
            else if (events&POLLIN) {
                if (c->watching) {
                    /* A watch occupies this RPC connection until its reply.
                     * Detect a closed peer and refuse pipelined requests. */
                    char byte; ssize_t got=recv(c->fd,&byte,1,MSG_PEEK);
                    ok=got<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR);
                } else ok=client_read(s,c);
            }
            if (!ok || (events&(POLLERR|POLLNVAL)) ||
                ((events&POLLHUP) && !(events&POLLIN)) || now_ms()>=c->deadline) client_close(s,c);
        }
        expire(s);
        if (!s->poisoned && s->journal_bytes-s->checkpoint_bytes>s->max_journal_bytes && !compact(s))
            s->poisoned=true;
    }
    for (size_t i=0;i<CLIENTS;++i) client_close(s,&clients[i]);
    (void)sigprocmask(SIG_SETMASK,&wait_signals,NULL);
    struct stat current;
    if (!lstat(path,&current) && current.st_dev==socket_identity.st_dev &&
        current.st_ino==socket_identity.st_ino) unlink(path);
    result=s->poisoned ? 1 : 0;
    if (s->poisoned) fprintf(stderr,"space-service: publication or checkpoint failed; recovery required\n");
done:
    if (listener>=0) close(listener);
    for (size_t i=0;i<STORES;++i) store_free(s->stores[i]);
    for (size_t i=0;i<SNAPSHOTS;++i) snapshot_free(s,&s->snapshots[i]);
    for (size_t i=0;i<s->receipt_count;++i) receipt_free(&s->receipts[i]);
    if (s->journal>=0) close(s->journal);
    if (s->lock>=0) close(s->lock);
    if (s->directory>=0) close(s->directory);
    symbol_table_free(&symbols); free(s); return result;
invalid:
    fprintf(stderr,"space-service: invalid arguments; use --help\n"); free(s); return 2;
}

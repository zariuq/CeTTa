/* holr_bridge.c - see holr_bridge.h. */
#include "holr_bridge.h"
#include "generated/hol_module_ids.h"
#include "../rule_components/rc_intern.h"
#include "../rule_components/rc_bytes.h"
#include "../rule_components/rc_table.h"
#include "../rule_module/rc_instance_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUL(c) ((T *)&rc_nul[(c)])

/* the representations rulec gave the module's numerals and names (rc_sort_rep: 2 binary naturals as
 * immediates, 3 heap byte strings, 6 interned byte strings; otherwise symbolic) */
static bool rep_nat, rep_str, rep_bytes;

/* ------------------------------------------------------------ the store */
void holr_store_fresh(bool fresh) { rc_intern_fresh(fresh); }
bool holr_store_active(void) { return rc_intern_active(); }
void holr_store_begin(void) {
    rc_table_reset();
    rc_intern_begin();
    rc_foreign_hook = rc_intern_safepoint;        /* the store's collections at the module's safepoints */
    rc_extra_roots_hook = rc_table_roots;         /* the cells of the id tables' arrays */
}
void holr_store_end(void) {
    rc_table_reset();
    rc_intern_end();
}
void holr_store_free(void) {
    rc_table_reset();
    rc_intern_free();
}

/* ------------------------------------------------------------ the reader
 * Streams are read by the HOLR reader compiled from its GSLT modules (HOLRStream, theory Format), its
 * own instance in this binary; its values are read through its table only. */
extern const RcInstanceApi cetta_holr_reader;
static const RcInstanceApi *R = &cetta_holr_reader;
static int RK_read, RK_decoded, RK_malformed, RK_need, RK_header, RK_rcons, RK_rnil, RK_record, RK_ocons, RK_onil, RK_word, RK_bytes,
    RK_words, RK_pairs, RK_wcons, RK_wnil, RK_pcons, RK_pnil, RK_pair, RK_ok, RK_error, RK_incomplete;
static int RS_state, RS_header, RS_records, RS_nat, RS_terminal, RS_reason;
/* the reader's refusal reasons, by name, as the module's terminal constructors */
static const struct { const char *reason; int terminal; } reason_map[] = {
    {"BadMagic", C_hl_bad_magic}, {"BadHeader", C_hl_bad_header}, {"BadVersion", C_hl_bad_version},
    {"Truncated", C_hl_truncated}, {"IntegerEncoding", C_hl_bad_integer}, {"BadOpcode", C_hl_bad_opcode},
    {"TrailingBytes", C_hl_trailing_bytes}, {"BadChecksum", C_hl_checksum_mismatch}, {"BadCount", C_hl_count_mismatch},
    {"FieldLimit", C_hl_limit_exceeded}, {"MissingEnd", C_hl_missing_end},
};
#define NREASONS ((int)(sizeof reason_map / sizeof reason_map[0]))
static int reason_ctor[NREASONS];
static int reader_ctor(const char *name) {
    int c = R->ctor_named(name);
    if (c < 0) { fprintf(stderr, "holr_bridge: the stream reader has no constructor %s\n", name); exit(3); }
    return c;
}
static int reader_sort(const char *name) {
    int s = R->sort_named(name);
    if (s < 0) { fprintf(stderr, "holr_bridge: the stream reader has no sort %s\n", name); exit(3); }
    return s;
}
/* the reader allocates its records and their steps without collecting (about 350 bytes a record):
 * its young memory is kept from stream to stream up to 1 GiB, so a stream no larger than an earlier
 * one reads into memory already mapped */
#define READER_RETAIN ((size_t)1 << 30)
static void reader_init(void) {
    R->init();
    R->retain(READER_RETAIN);
    RK_read = reader_ctor("ReadHOLRPrefix"); RK_decoded = reader_ctor("HOLRDecoded"); RK_malformed = reader_ctor("HOLRMalformed");
    RK_need = reader_ctor("NeedInput"); RK_header = reader_ctor("HOLRHeaderBytes"); RK_rcons = reader_ctor("RecordsCons");
    RK_rnil = reader_ctor("RecordsNil"); RK_record = reader_ctor("BRecord"); RK_ocons = reader_ctor("OperandsCons");
    RK_onil = reader_ctor("OperandsNil"); RK_word = reader_ctor("BWord"); RK_bytes = reader_ctor("BBytes");
    RK_words = reader_ctor("BWords"); RK_pairs = reader_ctor("BPairs"); RK_wcons = reader_ctor("WordsCons");
    RK_wnil = reader_ctor("WordsNil"); RK_pcons = reader_ctor("PairsCons"); RK_pnil = reader_ctor("PairsNil");
    RK_pair = reader_ctor("BPair");
    RK_ok = reader_ctor("HOLRPrefixOK"); RK_error = reader_ctor("HOLRPrefixError"); RK_incomplete = reader_ctor("HOLRPrefixIncomplete");
    RS_state = reader_sort("CodecState"); RS_header = reader_sort("HOLRHeader"); RS_records = reader_sort("Records");
    RS_nat = reader_sort("Nat"); RS_terminal = reader_sort("HOLRTerminal"); RS_reason = reader_sort("HOLRReason");
    for (int i = 0; i < NREASONS; i++) reason_ctor[i] = reader_ctor(reason_map[i].reason);
}

void holr_bridge_init(void) {
    static bool done;
    if (done) return;
    done = true;
    reader_init();
    rep_nat = rc_sort_rep[S_Nat] == 2;
    rep_str = rc_sort_rep[S_Str] == 3;
    rep_bytes = rc_sort_rep[S_Str] == 6;
    rc_components_init();
    if (!rep_bytes) {
        /* byte terms for the symbolic and heap representations of names */
        static const uint16_t nib[16] = {C_hl_x0, C_hl_x1, C_hl_x2, C_hl_x3, C_hl_x4, C_hl_x5, C_hl_x6, C_hl_x7,
                                         C_hl_x8, C_hl_x9, C_hl_xa, C_hl_xb, C_hl_xc, C_hl_xd, C_hl_xe, C_hl_xf};
        rc_bytes_init(C_hl_nnil, C_hl_ncons, C_hl_byte, nib);
    }
}

static T *nat_term(uint64_t v) {
    if (rep_nat) return RC_IMM(v);
    int bits[64], n = 0;
    while (v) { bits[n++] = (int)(v & 1); v >>= 1; }
    T *t = NUL(C_hl_nz);
    while (n--) t = rc_mk1(bits[n] ? C_hl_nb1 : C_hl_nb0, t);
    return t;
}
static T *str_term(const uint8_t *b, size_t n) {
    if (rep_bytes) return rc_intern_bytes(b, n);
    if (rep_str) return rc_bytes_new(b, n);
    T *t = NUL(C_hl_nnil);
    while (n--) t = rc_mk2(C_hl_ncons, rc_byte_term(b[n]), t);
    return t;
}
static T *ids_term(const uint64_t *v, uint32_t n) {
    T *t = NUL(C_hl_ids_nil);
    while (n--) t = rc_mk2(C_hl_ids_cons, nat_term(v[n]), t);
    return t;
}
static T *pairs_term(const uint64_t *v, uint32_t n) {
    T *t = NUL(C_hl_idp_nil);
    for (uint32_t i = n / 2; i-- > 0;) t = rc_mk3(C_hl_idp_cons, nat_term(v[2 * i]), nat_term(v[2 * i + 1]), t);
    return t;
}

/* decoded records, owned here; lists and names in one growing pool */
typedef struct { uint8_t op; uint64_t f[4]; uint32_t nl; size_t list; uint32_t nb; size_t bytes; } Rec;
static Rec *recs;
static size_t nrecs, reccap;
static uint8_t *pool;
static size_t pool_len, pool_cap;
uint64_t rc_record_count;

static size_t pool_add(const void *p, size_t n) {
    size_t at = (pool_len + 7u) & ~(size_t)7u;
    if (at + n > pool_cap) {
        size_t cap = pool_cap ? pool_cap : (size_t)1 << 16;
        while (cap < at + n) cap *= 2;
        pool = realloc(pool, cap);
        if (!pool) { fputs("holr_bridge: out of memory\n", stderr); exit(3); }
        pool_cap = cap;
    }
    if (n) memcpy(pool + at, p, n);
    pool_len = at + n;
    return at;
}

/* The records are read in bulk: the items of whole records (rc_list_items, in pre-order), each record
 * BRecord OPCODE OPERANDS, an operand BWord N | BBytes S | BWords (WordsCons N ...) | BPairs (PairsCons
 * (BPair N M) ...), in the order of the record's schema. */
static RcItem *items;
static size_t items_cap;
/* a list of numbers (words) or of pairs, flattened, appended to the pool */
static uint64_t *lst;
static size_t lst_cap;
static void lst_put(size_t i, uint64_t v) {
    if (i == lst_cap) { lst_cap = lst_cap ? 2 * lst_cap : 64; lst = realloc(lst, lst_cap * sizeof *lst); if (!lst) exit(3); }
    lst[i] = v;
}
/* one record from the items at *pi; false if they are not a record of this shape */
static bool keep_record(const RcItem *it, size_t n, size_t *pi) {
    size_t i = *pi;
    #define ITEM(c) (i < n && it[i].ctor == (c))
    if (!ITEM(RK_record)) return false;
    i++;
    if (!ITEM(-1) || it[i].v > 0xff) return false;
    if (nrecs == reccap) { reccap = reccap ? 2 * reccap : 1024; recs = realloc(recs, reccap * sizeof(Rec)); if (!recs) exit(3); }
    Rec *x = &recs[nrecs];
    memset(x, 0, sizeof *x);
    x->op = (uint8_t)it[i++].v;
    unsigned nf = 0;
    while (!ITEM(RK_onil)) {
        if (!ITEM(RK_ocons)) return false;
        i++;
        if (ITEM(RK_word)) {
            i++;
            if (!ITEM(-1) || nf == 4) return false;
            x->f[nf++] = it[i++].v;
        } else if (ITEM(RK_bytes)) {
            i++;
            if (!ITEM(-2)) return false;
            x->nb = it[i].n;
            x->bytes = pool_add(it[i].p, it[i].n);
            i++;
        } else if (ITEM(RK_words)) {
            i++;
            size_t m = 0;
            while (ITEM(RK_wcons)) {
                i++;
                if (!ITEM(-1)) return false;
                lst_put(m++, it[i++].v);
            }
            if (!ITEM(RK_wnil)) return false;
            i++;
            x->nl = (uint32_t)m;
            x->list = pool_add(lst, m * sizeof(uint64_t));
        } else if (ITEM(RK_pairs)) {
            i++;
            size_t m = 0;
            while (ITEM(RK_pcons)) {
                i++;
                if (!ITEM(RK_pair)) return false;
                i++;
                if (!ITEM(-1)) return false;
                lst_put(m++, it[i++].v);
                if (!ITEM(-1)) return false;
                lst_put(m++, it[i++].v);
            }
            if (!ITEM(RK_pnil)) return false;
            i++;
            x->nl = (uint32_t)m;
            x->list = pool_add(lst, m * sizeof(uint64_t));
        } else return false;
    }
    i++;
    #undef ITEM
    nrecs++;
    *pi = i;
    return true;
}
/* every record of a list of records; false if one is not a record */
static bool keep_records(T *list) {
    if (!items) { items_cap = 4096; items = malloc(items_cap * sizeof *items); if (!items) exit(3); }
    T *cursor = list;
    for (;;) {
        size_t n = R->list_items(RS_records, &cursor, items, items_cap);
        if (!n) {
            T *k[2];
            int c = R->view(RS_records, cursor, k);
            if (c == RK_rnil) return true;
            if (c != RK_rcons) return false;
            /* the next record does not fit: a larger buffer */
            items_cap *= 2;
            items = realloc(items, items_cap * sizeof *items);
            if (!items) exit(3);
            continue;
        }
        for (size_t i = 0; i < n;) if (!keep_record(items, n, &i)) return false;
    }
}
/* the reader's numbers: a header field is below 2^62 (the reader refuses longer integers) */
static uint64_t reader_u64(int sort, T *x) {
    uint64_t v;
    if (!R->value_u64(sort, x, &v)) { fputs("holr_bridge: a number of the stream reader above 2^64\n", stderr); exit(3); }
    return v;
}

size_t holr_bridge_records(void) { return nrecs; }

T *rc_record_term(uint64_t i) {
    Rec *r = &recs[i];
    const uint64_t *a = r->f;
    const uint64_t *l = (const uint64_t *)(pool + r->list);
    switch (r->op) {
    case HOLR_NAME: return rc_mk1(C_holr_NAME, str_term(pool + r->bytes, r->nb));
    case HOLR_TYVAR: return rc_mk1(C_holr_TYVAR, nat_term(a[0]));
    case HOLR_TYAPP: return rc_mk2(C_holr_TYAPP, nat_term(a[0]), ids_term(l, r->nl));
    case HOLR_VAR: return rc_mk2(C_holr_VAR, nat_term(a[0]), nat_term(a[1]));
    case HOLR_CONST: return rc_mk2(C_holr_CONST, nat_term(a[0]), nat_term(a[1]));
    case HOLR_COMB: return rc_mk2(C_holr_COMB, nat_term(a[0]), nat_term(a[1]));
    case HOLR_ABS: return rc_mk2(C_holr_ABS, nat_term(a[0]), nat_term(a[1]));
    case HOLR_REFL: return rc_mk1(C_holr_REFL, nat_term(a[0]));
    case HOLR_TRANS: return rc_mk2(C_holr_TRANS, nat_term(a[0]), nat_term(a[1]));
    case HOLR_MK_COMB: return rc_mk2(C_holr_MK_COMB, nat_term(a[0]), nat_term(a[1]));
    case HOLR_ABS_RULE: return rc_mk2(C_holr_ABS_RULE, nat_term(a[0]), nat_term(a[1]));
    case HOLR_BETA: return rc_mk1(C_holr_BETA, nat_term(a[0]));
    case HOLR_ASSUME: return rc_mk1(C_holr_ASSUME, nat_term(a[0]));
    case HOLR_EQ_MP: return rc_mk2(C_holr_EQ_MP, nat_term(a[0]), nat_term(a[1]));
    case HOLR_DEDUCT: return rc_mk2(C_holr_DEDUCT_ANTISYM_RULE, nat_term(a[0]), nat_term(a[1]));
    case HOLR_INST_TYPE: return rc_mk2(C_holr_INST_TYPE, pairs_term(l, r->nl), nat_term(a[0]));
    case HOLR_INST: return rc_mk2(C_holr_INST, pairs_term(l, r->nl), nat_term(a[0]));
    case HOLR_NEW_TYPE: return rc_mk2(C_holr_NEW_TYPE, nat_term(a[0]), nat_term(a[1]));
    case HOLR_NEW_CONSTANT: return rc_mk2(C_holr_NEW_CONSTANT, nat_term(a[0]), nat_term(a[1]));
    case HOLR_NEW_AXIOM: return rc_mk1(C_holr_NEW_AXIOM, nat_term(a[0]));
    case HOLR_NEW_DEFINITION: return rc_mk1(C_holr_NEW_DEFINITION, nat_term(a[0]));
    case HOLR_NEW_TYPEDEF: return rc_mk4(C_holr_NEW_TYPEDEF, nat_term(a[0]), nat_term(a[1]), nat_term(a[2]), nat_term(a[3]));
    case HOLR_POLICY: return rc_mk1(C_holr_POLICY, nat_term(a[0]));
    case HOLR_EXPECT: return rc_mk3(C_holr_EXPECT, nat_term(a[0]), ids_term(l, r->nl), nat_term(a[1]));
    case HOLR_EXPECT_ALPHA: return rc_mk3(C_holr_EXPECT_ALPHA, nat_term(a[0]), ids_term(l, r->nl), nat_term(a[1]));
    case HOLR_EXPECT_AXIOMS: return rc_mk2(C_holr_EXPECT_AXIOMS, nat_term(a[0]), ids_term(l, r->nl));
    case HOLR_EXPECT_REFUSED: return NUL(C_holr_EXPECT_REFUSED);
    case HOLR_LABEL: return rc_mk2(C_holr_LABEL, nat_term(a[0]), nat_term(a[1]));
    case HOLR_EMIT: return rc_mk1(C_holr_EMIT, nat_term(a[0]));
    case HOLR_DROP_TYPE: return rc_mk1(C_holr_DROP_TYPE, nat_term(a[0]));
    case HOLR_DROP_TERM: return rc_mk1(C_holr_DROP_TERM, nat_term(a[0]));
    case HOLR_DROP_THM: return rc_mk1(C_holr_DROP_THM, nat_term(a[0]));
    default: rc_refinement_fail("a decoded record has no module form");
    }
}

/* a refusal reason of the reader as the module's terminal constructor */
static int reason_terminal(T *reason) {
    int c = R->view(RS_reason, reason, NULL);
    for (int i = 0; i < NREASONS; i++) if (reason_ctor[i] == c) return reason_map[i].terminal;
    return C_hl_decoder_other;
}

/* The reader's value for the stream (ReadHOLRPrefix): HOLRDecoded HEADER RECORDS TERMINAL, the records
 * decoded before the end or the first fault and how the stream ended; HOLRMalformed REASON when the
 * header is refused; NeedInput when the stream ends inside the header. */
T *holr_bridge_decode(const uint8_t *bytes, size_t len) {
    holr_store_begin();
    nrecs = 0;
    pool_len = 0;
    bool decoded = false;
    uint64_t policy = 0, flags = 0;
    int terminal = C_hl_decoder_other;   /* a value the reader does not give: no end of its own */
    bool end_ok = false;
    R->run_begin();
    T *in = R->bytes_input(bytes, len);
    T *v = in ? R->call(RK_read, &in) : NULL;
    T *k[4], *h[5], *t[1];
    int c = v ? R->view(RS_state, v, k) : -1;
    if (c == RK_decoded && R->view(RS_header, k[0], h) == RK_header) {
        decoded = true;
        policy = reader_u64(RS_nat, h[3]);
        flags = reader_u64(RS_nat, h[4]);
        bool records_ok = keep_records(k[1]);
        int e = R->view(RS_terminal, k[2], t);
        if (!records_ok) terminal = C_hl_decoder_other;
        else if (e == RK_ok) end_ok = true;
        else if (e == RK_error) terminal = reason_terminal(t[0]);
        else if (e == RK_incomplete) terminal = C_hl_truncated;
    } else if (c == RK_malformed) terminal = reason_terminal(k[0]);
    else if (c == RK_need) terminal = C_hl_truncated;
    R->run_end();
    rc_record_count = nrecs;
    T *header = !decoded
        ? NUL(C_hl_header_malformed)
        : rc_mk2(C_hl_header, NUL(policy == HOL_AXIOMS_OPEN ? C_hl_open : policy == HOL_AXIOMS_STANDARD ? C_hl_standard : C_hl_sealed),
                 NUL((flags & HOLR_FLAG_PROBE) ? C_hl_probe : C_hl_strict));
    T *term = end_ok ? NUL(C_hl_end_ok) : rc_mk1(C_hl_end_malformed, NUL(terminal));
    return rc_mk3(C_hl_decoded, RC_IMM(0), header, term);
}

T *holr_bridge_policy(HolAxiomPolicy p) {
    return NUL(p == HOL_AXIOMS_OPEN ? C_hl_open : p == HOL_AXIOMS_STANDARD ? C_hl_standard : C_hl_sealed);
}

T *holr_bridge_check(T *host, T *decoded) {
    T *args[2] = {host, decoded};
    return rc_call(C_hl_check, args);
}

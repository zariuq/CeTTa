/* hlm_feed.c - HOLR streams as the input of the HOL Light kernel module: the description of the feed
 * (rc_feed.h) from the HOLR reader compiled from its GSLT modules (HOLRStream, theory Format; its table
 * cetta_holr_reader) to the module's hl:decoded CURSOR HEADER END.
 *
 * The reader's loop HOLRLoop FULL HEADER COUNT REST is its safepoint (HEADER its argument 1); each
 * record waits in a frame of StreamPush RECORD STATE (the record its argument 0), and StreamPush conses
 * its record onto the records of every HOLRDecoded the loop ends with (rule StreamCons; the loop ends
 * with nothing else).  The module's record of each opcode is the table below, its children the record's
 * operands in the order of the record's schema (HOLRSchema).  Of native/hol_light only the stream
 * format's constants (opcodes, flags, policies) are used.
 */
#include "../rule_module/rc_feed.h"
#include "../hol_light/hol_replay.h"

/* the module's record for each opcode */
static const RcFeedRecord hol_stream_records[] = {
    {HOLR_NAME, C_holr_NAME}, {HOLR_TYVAR, C_holr_TYVAR}, {HOLR_TYAPP, C_holr_TYAPP}, {HOLR_VAR, C_holr_VAR},
    {HOLR_CONST, C_holr_CONST}, {HOLR_COMB, C_holr_COMB}, {HOLR_ABS, C_holr_ABS},
    {HOLR_REFL, C_holr_REFL}, {HOLR_TRANS, C_holr_TRANS}, {HOLR_MK_COMB, C_holr_MK_COMB}, {HOLR_ABS_RULE, C_holr_ABS_RULE},
    {HOLR_BETA, C_holr_BETA}, {HOLR_ASSUME, C_holr_ASSUME}, {HOLR_EQ_MP, C_holr_EQ_MP}, {HOLR_DEDUCT, C_holr_DEDUCT_ANTISYM_RULE},
    {HOLR_INST_TYPE, C_holr_INST_TYPE}, {HOLR_INST, C_holr_INST},
    {HOLR_NEW_TYPE, C_holr_NEW_TYPE}, {HOLR_NEW_CONSTANT, C_holr_NEW_CONSTANT}, {HOLR_NEW_AXIOM, C_holr_NEW_AXIOM},
    {HOLR_NEW_DEFINITION, C_holr_NEW_DEFINITION}, {HOLR_NEW_TYPEDEF, C_holr_NEW_TYPEDEF}, {HOLR_POLICY, C_holr_POLICY},
    {HOLR_EXPECT, C_holr_EXPECT}, {HOLR_LABEL, C_holr_LABEL}, {HOLR_EMIT, C_holr_EMIT}, {HOLR_EXPECT_REFUSED, C_holr_EXPECT_REFUSED},
    {HOLR_EXPECT_AXIOMS, C_holr_EXPECT_AXIOMS}, {HOLR_EXPECT_ALPHA, C_holr_EXPECT_ALPHA},
    {HOLR_DROP_TYPE, C_holr_DROP_TYPE}, {HOLR_DROP_TERM, C_holr_DROP_TERM}, {HOLR_DROP_THM, C_holr_DROP_THM},
};
/* the reader's refusals, by name, as the module's reasons */
static const struct { const char *reason; int ctor; } hol_stream_reasons[] = {
    {"BadMagic", C_hl_bad_magic}, {"BadHeader", C_hl_bad_header}, {"BadVersion", C_hl_bad_version},
    {"Truncated", C_hl_truncated}, {"IntegerEncoding", C_hl_bad_integer}, {"BadOpcode", C_hl_bad_opcode},
    {"TrailingBytes", C_hl_trailing_bytes}, {"BadChecksum", C_hl_checksum_mismatch}, {"BadCount", C_hl_count_mismatch},
    {"FieldLimit", C_hl_limit_exceeded}, {"MissingEnd", C_hl_missing_end},
};
#define HOL_STREAM_NREASONS (sizeof hol_stream_reasons / sizeof hol_stream_reasons[0])

/* the reader's constructors and sorts used here */
static struct {
    bool ready;
    int decoded, malformed, need, header, rnil, ok, error, incomplete;
    int s_state, s_header, s_records, s_nat, s_terminal, s_reason;
    int reason[HOL_STREAM_NREASONS];
} hs;
static int hs_ctor(const RcInstanceApi *R, const char *name) {
    int c = R->ctor_named(name);
    if (c < 0) { fprintf(stderr, "hol stream: the reader has no constructor %s\n", name); exit(3); }
    return c;
}
static int hs_sort(const RcInstanceApi *R, const char *name) {
    int s = R->sort_named(name);
    if (s < 0) { fprintf(stderr, "hol stream: the reader has no sort %s\n", name); exit(3); }
    return s;
}
static void hs_init(const RcInstanceApi *R) {
    if (hs.ready) return;
    hs.decoded = hs_ctor(R, "HOLRDecoded"); hs.malformed = hs_ctor(R, "HOLRMalformed"); hs.need = hs_ctor(R, "NeedInput");
    hs.header = hs_ctor(R, "HOLRHeaderBytes"); hs.rnil = hs_ctor(R, "RecordsNil");
    hs.ok = hs_ctor(R, "HOLRPrefixOK"); hs.error = hs_ctor(R, "HOLRPrefixError"); hs.incomplete = hs_ctor(R, "HOLRPrefixIncomplete");
    hs.s_state = hs_sort(R, "CodecState"); hs.s_header = hs_sort(R, "HOLRHeader"); hs.s_records = hs_sort(R, "Records");
    hs.s_nat = hs_sort(R, "Nat"); hs.s_terminal = hs_sort(R, "HOLRTerminal"); hs.s_reason = hs_sort(R, "HOLRReason");
    for (size_t i = 0; i < HOL_STREAM_NREASONS; i++) hs.reason[i] = hs_ctor(R, hol_stream_reasons[i].reason);
    hs.ready = true;
}
static T *hs_mk(int ctor, T *a, T *b, T *c) { T *k[3] = {a, b, c}; return rc_construct(ctor, k); }

/* the header: hl:header POLICY MODE from the reader's header fields (its policy and its flags) */
static T *hs_header(const RcInstanceApi *R, T *header) {
    T *h[5];
    uint64_t policy, flags;
    if (!header || R->view(hs.s_header, header, h) != hs.header) return hs_mk(C_hl_header_malformed, NULL, NULL, NULL);
    if (!R->value_u64(hs.s_nat, h[3], &policy) || !R->value_u64(hs.s_nat, h[4], &flags))
        rc_refinement_fail("hol stream: a header field above 2^64");
    int pol = policy == HOL_AXIOMS_OPEN ? C_hl_open : policy == HOL_AXIOMS_STANDARD ? C_hl_standard : C_hl_sealed;
    int mode = (flags & HOLR_FLAG_PROBE) ? C_hl_probe : C_hl_strict;
    return hs_mk(C_hl_header, hs_mk(pol, NULL, NULL, NULL), hs_mk(mode, NULL, NULL, NULL), NULL);
}
/* a reader's value that has ended its loop: HOLRDecoded HEADER RECORDS END, no record left in it */
static bool hs_decoded(const RcInstanceApi *R, T *final, T **k) {
    if (!final || R->view(hs.s_state, final, k) != hs.decoded) return false;
    if (R->view(hs.s_records, k[1], NULL) != hs.rnil) rc_refinement_fail("hol stream: the reader's value holds records outside its frames");
    return true;
}
static T *hol_stream_value(const RcInstanceApi *R, T *header, T *final, T *cursor, T *end) {
    hs_init(R);
    T *k[3];
    if (!header && hs_decoded(R, final, k)) header = k[0];
    return hs_mk(C_hl_decoded, cursor, hs_header(R, header), end);
}
/* the end: hl:end-ok, or hl:end-malformed REASON */
static T *hs_reason(const RcInstanceApi *R, T *reason) {
    int c = R->view(hs.s_reason, reason, NULL);
    for (size_t i = 0; i < HOL_STREAM_NREASONS; i++)
        if (hs.reason[i] == c) return hs_mk(C_hl_end_malformed, hs_mk(hol_stream_reasons[i].ctor, NULL, NULL, NULL), NULL, NULL);
    return hs_mk(C_hl_end_malformed, hs_mk(C_hl_decoder_other, NULL, NULL, NULL), NULL, NULL);
}
static T *hol_stream_end(const RcInstanceApi *R, T *final) {
    hs_init(R);
    T *k[3], *t[1];
    if (hs_decoded(R, final, k)) {
        int e = R->view(hs.s_terminal, k[2], t);
        if (e == hs.ok) return hs_mk(C_hl_end_ok, NULL, NULL, NULL);
        if (e == hs.error) return hs_reason(R, t[0]);
        if (e == hs.incomplete) return hs_mk(C_hl_end_malformed, hs_mk(C_hl_truncated, NULL, NULL, NULL), NULL, NULL);
    } else if (final) {
        int c = R->view(hs.s_state, final, k);
        if (c == hs.malformed) return hs_reason(R, k[0]);
        if (c == hs.need) return hs_mk(C_hl_end_malformed, hs_mk(C_hl_truncated, NULL, NULL, NULL), NULL, NULL);
    }
    return hs_mk(C_hl_end_malformed, hs_mk(C_hl_decoder_other, NULL, NULL, NULL), NULL, NULL);
}

extern const RcInstanceApi cetta_holr_reader;
const RcFeed hol_stream_feed = {
    .reader = &cetta_holr_reader, .entry = "ReadHOLRPrefix", .wrapper = "StreamPush", .element = 0, .header_root = 1,
    .stream_sort = "Decoded", .records = hol_stream_records, .nrecords = sizeof hol_stream_records / sizeof hol_stream_records[0],
    .stream = hol_stream_value, .end = hol_stream_end,
};

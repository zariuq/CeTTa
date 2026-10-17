#include "hol_internal.h"
#include "hol_replay.h"
#include "test_util.h"

static void owner_reuse(void) {
    SECTION("bounded owners and stale authority");
    HolKernel *k = hol_kernel_new(NULL), *foreign = hol_kernel_new(NULL);
    CHECK(k && foreign);
    HolOwner old = hol_owner_new(k);
    CHECK(old != HOL_OWNER_KERNEL);
    CHECK(hol_owner_release(k, old) == HOL_OK);
    HolStats before, after;
    hol_gc(k); /* Include the collector's lazily allocated workspace. */
    hol_stats(k, &before);
    bool passed = true;
    for (unsigned i = 0; i < 1000000; ++i) {
        HolOwner owner = hol_owner_new(k);
        if (!owner || owner == old || hol_owner_release(k, owner) != HOL_OK) {
            passed = false;
            break;
        }
    }
    CHECK(passed);
    hol_gc(k);
    hol_stats(k, &after);
    CHECK(after.bytes_live <= before.bytes_live);
    CHECK(after.refs == 0);
    HolOwner current = hol_owner_new(k);
    CHECK(hol_owner_release(k, old) == HOL_STALE);
    CHECK(hol_owner_release(foreign, current) == HOL_FOREIGN);
    hol_owner_set_current(k, current);
    CHECK(hol_owner_set_current(k, old) == current);
    CHECK(hol_last_status(k) == HOL_STALE);
    HolRef value;
    CHECK(hol_mk_vartype(k, "owned", &value) == HOL_OK);
    CHECK(hol_owner_release(k, current) == HOL_OK);
    CHECK(hol_ref_kind(k, value) == HOL_KIND_NONE);

    /* Force the final generation without a sixteen-million-iteration test. */
    HolOwner last = hol_owner_new(k);
    uint32_t index = (uint32_t)(last & UINT32_C(0xffffff));
    k->owners[index].generation = UINT32_C(0xffffff);
    last |= UINT64_C(0xffffff) << 24;
    CHECK(hol_owner_release(k, last) == HOL_OK);
    HolOwner fresh = hol_owner_new(k);
    CHECK((fresh & UINT64_C(0xffffff)) != index);
    CHECK(hol_owner_release(k, last) == HOL_STALE);
    CHECK(hol_owner_release(k, fresh) == HOL_OK);
    hol_kernel_free(foreign);
    hol_kernel_free(k);
}

static void named_beta_observation(void) {
    SECTION("identity substitution and named BETA");
    HolKernel *k = hol_kernel_new(NULL);
    HolRef env, a, b, aa, aaa, x, z, f, fx, fxz, lam, h, hi, hs;
    HolRef xb, zb, conclusion, left, right, app, beta, binder;
#define OK(call) CHECK((call) == HOL_OK)
    OK(hol_env_bare(k, &env));
    OK(hol_mk_vartype(k, "A", &a));
    OK(hol_mk_vartype(k, "B", &b));
    HolRef a2[] = {a, a};
    OK(hol_mk_type(k, env, "fun", a2, 2, &aa));
    HolRef a3[] = {a, aa};
    OK(hol_mk_type(k, env, "fun", a3, 2, &aaa));
    OK(hol_mk_var(k, "x", a, &x));
    OK(hol_mk_var(k, "z", a, &z));
    OK(hol_mk_var(k, "f", aaa, &f));
    OK(hol_mk_comb(k, f, x, &fx));
    OK(hol_mk_comb(k, fx, z, &fxz));
    OK(hol_mk_abs(k, z, fxz, &lam));
    OK(hol_REFL(k, lam, &h));
    HolRef types[] = {b, a};
    OK(hol_INST_TYPE(k, types, 1, h, &hi));
    OK(hol_mk_var(k, "x", b, &xb));
    OK(hol_mk_var(k, "z", b, &zb));
    HolRef terms[] = {xb, xb, zb, xb};
    OK(hol_INST(k, terms, 2, hi, &hs));
    OK(hol_thm_concl(k, hs, &conclusion));
    OK(hol_dest_eq(k, conclusion, &left, &right));
    OK(hol_term_part(k, right, 0, &binder));
    CHECK(strcmp(hol_term_name(k, binder), "z") == 0);
    OK(hol_mk_comb(k, right, zb, &app));
    OK(hol_BETA(k, app, &beta));
    /* The rule remains primitive BETA, not general beta conversion. */
    OK(hol_mk_comb(k, right, xb, &app));
    CHECK(hol_BETA(k, app, &beta) == HOL_REFUSED);
    CHECK(hol_last_reason(k) == HOL_R_BETA);
    hol_kernel_free(k);
#undef OK
}

static void proof_lifetimes(void) {
    SECTION("checking, recording and proof retirement");
    for (unsigned recording = 0; recording < 2; ++recording) {
        HolKernel *k = hol_kernel_new(NULL);
        CHECK(k != NULL);
        HolRef a, x, h, base;
        CHECK(hol_mk_vartype(k, "A", &a) == HOL_OK);
        CHECK(hol_mk_var(k, "x", a, &x) == HOL_OK);
        hol_journal_enable(k, recording != 0);
        CHECK(hol_REFL(k, x, &h) == HOL_OK);
        CHECK(hol_ref_dup(k, h, &base) == HOL_OK);
        bool valid = true;
        for (unsigned i = 0; i < 10000; ++i) {
            HolRef next;
            if (hol_TRANS(k, h, base, &next) != HOL_OK ||
                hol_ref_release(k, h) != HOL_OK) { valid = false; break; }
            h = next;
        }
        CHECK(valid);
        HolRef short_root;
        CHECK(hol_TRANS(k, base, base, &short_root) == HOL_OK);
        bool same = false;
        CHECK(hol_thm_same_payload(k, h, short_root, &same) == HOL_OK && same);
        hol_gc(k);
        HolStats live;
        hol_stats(k, &live);
        CHECK(live.journal_events == (recording ? 10002u : 0u));
        CHECK(recording ? live.journal_bytes > 10000u * sizeof(HolJEvent)
                        : live.journal_bytes == 0);
        bool equal = false;
        CHECK(hol_thm_equal(k, h, base, &equal) == HOL_OK && equal);
        CHECK(hol_ref_release(k, h) == HOL_OK);

        /* The short root keeps its own derivation across a large serial gap,
         * not the discarded chain with the identical theorem payload. */
        hol_gc(k);
        HolStats partial;
        hol_stats(k, &partial);
        CHECK(partial.journal_events == recording * 2u);
        CHECK(!recording || partial.journal_bytes < live.journal_bytes);
        uint8_t *certificate = NULL;
        size_t length = 0;
        HolStatus exported = hol_export(k, &short_root, 1, &certificate, &length);
        CHECK(exported == (recording ? HOL_OK : HOL_REFUSED));
        if (recording && exported == HOL_OK) {
            HolKernel *receiver = hol_kernel_new(NULL);
            HolReplay *replay = hol_replay_new(receiver, NULL, HOL_AXIOMS_STANDARD);
            CHECK(replay != NULL);
            hol_replay_feed(replay, certificate, length, true);
            CHECK(hol_replay_resume(replay, 0) == HOL_REPLAY_ACCEPTED);
            hol_replay_free(replay);
            hol_kernel_free(receiver);
        }
        free(certificate);
        CHECK(hol_ref_release(k, base) == HOL_OK);
        CHECK(hol_ref_release(k, short_root) == HOL_OK);
        hol_gc(k);
        HolStats retired;
        hol_stats(k, &retired);
        CHECK(retired.journal_events == 0);
        CHECK(retired.journal_bytes == 0);
        hol_kernel_free(k);
    }
}

int main(void) {
    owner_reuse();
    named_beta_observation();
    proof_lifetimes();
    return test_summary("test-hol-boundaries");
}

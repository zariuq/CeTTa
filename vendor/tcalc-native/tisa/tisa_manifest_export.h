/*
 * tisa_manifest_export.h — Live 23-Rule Theory & Layout Manifest Exporter
 *
 * Distills CeTTa's `library_json` / `json_value_v1.h` and Mettapedia's
 * `KernelFoundationManifest.lean` / `BindingWireData.lean` principles:
 *   1. Authoritative live reflection over `tcalc`'s `R[]`, `FL[]`, `C[]`, `S[]`
 *      tables after `lean_tc_init_kernel()` (zero hand-transcribed duplicates).
 *   2. Explicit `checking_role` separation between the 12 privileged leaf
 *      publishers (`0..11`) and the 11 premise-bearing flat rules (`12..22`).
 *   3. Exact compiled `sizeof`, `_Alignof`, and `offsetof` layout metadata for
 *      `TcLeanGenTuple` and `TcIsaRuntimeFrame`.
 *   4. Exact 16-hex-digit U64 payloads (`"0x%016" PRIx64`) alongside bounded
 *      small integer indices.
 */
#pragma once

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lean_tcalc_theory.h"
#include "../lean_tcalc_gen.h"
#include "tc_theory_inspect.h"
#include "tisa.h"

/* =========================================================================
 * 1. Self-Contained FIPS 180-4 SHA-256 (for deterministic source/artifact IDs)
 * ========================================================================= */

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t  buffer[64];
} TcSha256Ctx;

static inline uint32_t tc_rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

static inline void tc_sha256_transform(TcSha256Ctx *ctx, const uint8_t data[64]) {
    static const uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4 + 0] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8)  |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = tc_rotr32(w[i - 15], 7) ^ tc_rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = tc_rotr32(w[i - 2], 17) ^ tc_rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = tc_rotr32(e, 6) ^ tc_rotr32(e, 11) ^ tc_rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = tc_rotr32(a, 2) ^ tc_rotr32(a, 13) ^ tc_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static inline void tc_sha256_init(TcSha256Ctx *ctx) {
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->bitcount = 0;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

static inline void tc_sha256_update(TcSha256Ctx *ctx, const uint8_t *data, size_t len) {
    size_t idx = (size_t)((ctx->bitcount >> 3) & 63u);
    ctx->bitcount += (uint64_t)len << 3;
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[idx++] = data[i];
        if (idx == 64u) {
            tc_sha256_transform(ctx, ctx->buffer);
            idx = 0u;
        }
    }
}

static inline void tc_sha256_final_hex(TcSha256Ctx *ctx, char out_hex[65]) {
    size_t idx = (size_t)((ctx->bitcount >> 3) & 63u);
    ctx->buffer[idx++] = 0x80u;
    if (idx > 56u) {
        while (idx < 64u) ctx->buffer[idx++] = 0u;
        tc_sha256_transform(ctx, ctx->buffer);
        idx = 0u;
    }
    while (idx < 56u) ctx->buffer[idx++] = 0u;
    for (int i = 0; i < 8; i++) {
        ctx->buffer[56 + i] = (uint8_t)(ctx->bitcount >> ((7 - i) * 8));
    }
    tc_sha256_transform(ctx, ctx->buffer);
    for (int i = 0; i < 8; i++) {
        snprintf(out_hex + i * 8, 9, "%08x", ctx->state[i]);
    }
    out_hex[64] = '\0';
}

static inline bool tc_sha256_file_hex(const char *rel_or_abs, char out_hex[65]) {
    const char *candidates[3] = { rel_or_abs, NULL, NULL };
    char buf1[512], buf2[512];
    snprintf(buf1, sizeof(buf1), "tcalc/%s", rel_or_abs);
    const char *root = getenv("TCALC_DIR");
    snprintf(buf2, sizeof(buf2), "%s/%s", root ? root : ".", rel_or_abs);
    candidates[1] = buf1;
    candidates[2] = buf2;
    FILE *fp = NULL;
    for (int i = 0; i < 3 && !fp; i++) {
        if (candidates[i]) fp = fopen(candidates[i], "rb");
    }
    if (!fp) {
        memset(out_hex, '0', 64);
        out_hex[64] = '\0';
        return false;
    }
    TcSha256Ctx ctx;
    tc_sha256_init(&ctx);
    uint8_t chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        tc_sha256_update(&ctx, chunk, n);
    }
    fclose(fp);
    tc_sha256_final_hex(&ctx, out_hex);
    return true;
}

/* =========================================================================
 * 2. Symbolic Metadata Tables for the 23-Rule Lean Theory
 * ========================================================================= */

static inline const char *lean_tc_space_name(uint8_t sp) {
    switch (sp) {
        case LEAN_SPACE_WORK: return "LEAN_SPACE_WORK";
        case LEAN_SPACE_ENV:  return "LEAN_SPACE_ENV";
        case LEAN_SPACE_THM:  return "LEAN_SPACE_THM";
        default:              return "LEAN_SPACE_UNKNOWN";
    }
}

static inline const char *lean_tc_sort_name(int s) {
    if (s == 0)      return "TC_ANY";
    if (s == S_ID)   return "S_ID";
    if (s == S_LVL)  return "S_LVL";
    if (s == S_EXPR) return "S_EXPR";
    if (s == S_DECL) return "S_DECL";
    if (s == S_THM)  return "S_THM";
    return "S_UNKNOWN";
}

static inline const char *lean_tc_con_name(int c) {
    if (c == C_ID_ZERO)     return "C_ID_ZERO";
    if (c == C_LVL_NODE)    return "C_LVL_NODE";
    if (c == C_EXPR_NODE)   return "C_EXPR_NODE";
    if (c == C_DECL_AXIOM)  return "C_DECL_AXIOM";
    if (c == C_DECL_DEFN)   return "C_DECL_DEFN";
    if (c == C_DECL_THM)    return "C_DECL_THM";
    if (c == C_DECL_OPAQUE) return "C_DECL_OPAQUE";
    if (c == C_DECL_QUOT)   return "C_DECL_QUOT";
    if (c == C_DECL_IND)    return "C_DECL_IND";
    if (c == C_DECL_CTOR)   return "C_DECL_CTOR";
    if (c == C_DECL_REC)    return "C_DECL_REC";
    if (c == C_TYPED)       return "C_TYPED";
    if (c == C_DEFEQ)       return "C_DEFEQ";
    if (c == C_IS_TYPE)     return "C_IS_TYPE";
    if (c == C_IS_PROP)     return "C_IS_PROP";
    if (c == C_SORT_OF)     return "C_SORT_OF";
    if (c == C_DECL_REQ2)   return "C_DECL_REQ2";
    if (c == C_DECL_REQ3)   return "C_DECL_REQ3";
    return "C_UNKNOWN";
}

static inline const char *lean_tc_rule_name(int r) {
    if (r == R_CHECK_SORT)      return "R_CHECK_SORT";
    if (r == R_CHECK_CONST)     return "R_CHECK_CONST";
    if (r == R_CHECK_APP)       return "R_CHECK_APP";
    if (r == R_CHECK_LAM)       return "R_CHECK_LAM";
    if (r == R_CHECK_FORALL)    return "R_CHECK_FORALL";
    if (r == R_CHECK_LET)       return "R_CHECK_LET";
    if (r == R_CHECK_PROJ)      return "R_CHECK_PROJ";
    if (r == R_CHECK_LIT)       return "R_CHECK_LIT";
    if (r == R_EMIT_DEFEQ)      return "R_EMIT_DEFEQ";
    if (r == R_EMIT_SORT_OF)    return "R_EMIT_SORT_OF";
    if (r == R_EMIT_DECL_REQ2)  return "R_EMIT_DECL_REQ2";
    if (r == R_EMIT_DECL_REQ3)  return "R_EMIT_DECL_REQ3";
    if (r == R_DERIVE_IS_TYPE)  return "R_DERIVE_IS_TYPE";
    if (r == R_DERIVE_IS_PROP)  return "R_DERIVE_IS_PROP";
    if (r == R_CHECK_CONV)      return "R_CHECK_CONV";
    if (r == R_ADMIT_AXIOM)     return "R_ADMIT_AXIOM";
    if (r == R_ADMIT_QUOT)      return "R_ADMIT_QUOT";
    if (r == R_ADMIT_IND)       return "R_ADMIT_IND";
    if (r == R_ADMIT_CTOR)      return "R_ADMIT_CTOR";
    if (r == R_ADMIT_REC)       return "R_ADMIT_REC";
    if (r == R_ADMIT_DEFN)      return "R_ADMIT_DEFN";
    if (r == R_ADMIT_OPAQUE)    return "R_ADMIT_OPAQUE";
    if (r == R_ADMIT_THM)       return "R_ADMIT_THM";
    return "R_UNKNOWN";
}

static inline const char *lean_tc_rule_obligation(int r) {
    if (r == R_CHECK_SORT)     return "trusted_c_typing_sort_level_succ";
    if (r == R_CHECK_CONST)    return "trusted_c_typing_const_universe_inst";
    if (r == R_CHECK_APP)      return "trusted_c_typing_app_whnf_forall_defeq_subst";
    if (r == R_CHECK_LAM)      return "trusted_c_typing_lambda_binder_fvar_abstraction";
    if (r == R_CHECK_FORALL)   return "trusted_c_typing_forall_imax_sort";
    if (r == R_CHECK_LET)      return "trusted_c_typing_let_val_defeq_subst";
    if (r == R_CHECK_PROJ)     return "trusted_c_typing_struct_proj_telescope";
    if (r == R_CHECK_LIT)      return "trusted_c_typing_nat_str_literal";
    if (r == R_EMIT_DEFEQ)     return "trusted_c_defeq_whnf_eta_proof_irrel_lazy_delta";
    if (r == R_EMIT_SORT_OF)   return "trusted_c_whnf_sort_extraction";
    if (r == R_EMIT_DECL_REQ2) return "trusted_c_decl_header_validation_arity2";
    if (r == R_EMIT_DECL_REQ3) return "trusted_c_decl_header_ind_positivity_recursor_validation_arity3";
    return "none_pure_tcalc_premise_rule";
}

static inline uint64_t lean_tc_const_payload_u64(Term t) {
    if (t == T_ID_ZERO) return 0ull;
    return tc_name_id(t);
}

static inline void lean_tc_emit_pat_json(FILE *out, Pat pat, int indent_spaces) {
    uint8_t kind = 0;
    uint32_t head = 0;
    int arity = 0;
    const Term *args = NULL;
    tc_node(pat.t, &kind, &head, &arity, &args);
    uint32_t wtag = (head << 9) | ((uint32_t)pat.space << 1) | 1u;
    const char *ind = (indent_spaces == 8) ? "        " : "      ";
    fprintf(out, "{\n");
    fprintf(out, "%s  \"space_id\": %u,\n", ind, (unsigned)pat.space);
    fprintf(out, "%s  \"space_name\": \"%s\",\n", ind, lean_tc_space_name(pat.space));
    fprintf(out, "%s  \"term_id\": %u,\n", ind, (unsigned)pat.t);
    fprintf(out, "%s  \"con_id\": %u,\n", ind, (unsigned)head);
    fprintf(out, "%s  \"con_name\": \"%s\",\n", ind, lean_tc_con_name((int)head));
    fprintf(out, "%s  \"sort_id\": %u,\n", ind, (unsigned)tc_sort_of(pat.t));
    fprintf(out, "%s  \"sort_name\": \"%s\",\n", ind, lean_tc_sort_name((int)tc_sort_of(pat.t)));
    fprintf(out, "%s  \"arity\": %d,\n", ind, arity);
    fprintf(out, "%s  \"wtag\": %u,\n", ind, wtag);
    fprintf(out, "%s  \"wtag_hex\": \"0x%08x\",\n", ind, wtag);
    fprintf(out, "%s  \"args\": [", ind);
    for (int j = 0; j < arity; j++) {
        Term a = args[j];
        uint8_t ak = 0;
        uint32_t ahead = 0;
        int aar = 0;
        const Term *aargs = NULL;
        tc_node(a, &ak, &ahead, &aar, &aargs);
        if (j > 0) fprintf(out, ", ");
        if (ak == K_VAR) {
            fprintf(out, "{\"arg_idx\": %d, \"kind\": \"var\", \"var\": %u, \"term_id\": %u, \"const_name\": null, \"u64_payload_hex\": null}",
                    j, (unsigned)ahead, (unsigned)a);
        } else {
            uint64_t pay = lean_tc_const_payload_u64(a);
            const char *cname = (a == T_ID_ZERO) ? "T_ID_ZERO" : "K_NAME";
            fprintf(out, "{\"arg_idx\": %d, \"kind\": \"const\", \"var\": -1, \"term_id\": %u, \"const_name\": \"%s\", \"u64_payload_hex\": \"0x%016" PRIx64 "\"}",
                    j, (unsigned)a, cname, pay);
        }
    }
    fprintf(out, "]\n%s}", ind);
}

/* =========================================================================
 * 3. Deterministic JSON Serializer for the Live 23-Rule Theory & Layouts
 * ========================================================================= */

static inline bool lean_tc_write_theory_json_stream(FILE *out) {
    if (!out) return false;
    lean_tc_init_kernel();

    char sha_theory[65], sha_tcalc_h[65], sha_tcalc_c[65], sha_tisa_h[65];
    tc_sha256_file_hex("lean_tcalc_theory.h", sha_theory);
    tc_sha256_file_hex("tcalc.h", sha_tcalc_h);
    tc_sha256_file_hex("tcalc.c", sha_tcalc_c);
    tc_sha256_file_hex("tisa/tisa.h", sha_tisa_h);

    fprintf(out, "{\n");
    fprintf(out, "  \"schema\": \"tcalc.lean_theory_and_tisa_contract.v1\",\n");
    fprintf(out, "  \"schema_version\": %u,\n", TISA_SCHEMA_VERSION);
    fprintf(out, "  \"profile_version\": \"%s\",\n", TISA_PROFILE_VERSION);
    fprintf(out, "  \"semantic_authority\": \"tcalc_resource_interaction_take_keep_make\",\n");
    fprintf(out, "  \"source_hashes\": {\n");
    fprintf(out, "    \"lean_tcalc_theory_h_sha256\": \"%s\",\n", sha_theory);
    fprintf(out, "    \"tcalc_h_sha256\": \"%s\",\n", sha_tcalc_h);
    fprintf(out, "    \"tcalc_c_sha256\": \"%s\",\n", sha_tcalc_c);
    fprintf(out, "    \"tisa_h_sha256\": \"%s\"\n", sha_tisa_h);
    fprintf(out, "  },\n");

    /* Tuple layout */
    fprintf(out, "  \"tuple_layout\": {\n");
    fprintf(out, "    \"struct_name\": \"TcLeanGenTuple\",\n");
    fprintf(out, "    \"sizeof_bytes\": %zu,\n", sizeof(TcLeanGenTuple));
    fprintf(out, "    \"alignof_bytes\": %zu,\n", _Alignof(TcLeanGenTuple));
    fprintf(out, "    \"max_arity\": 3,\n");
    fprintf(out, "    \"tag_offset\": %zu,\n", offsetof(TcLeanGenTuple, tag));
    fprintf(out, "    \"tag_width_bytes\": 4,\n");
    fprintf(out, "    \"pad_offset\": %zu,\n", offsetof(TcLeanGenTuple, pad));
    fprintf(out, "    \"pad_width_bytes\": 4,\n");
    fprintf(out, "    \"args_offset\": %zu,\n", offsetof(TcLeanGenTuple, a));
    fprintf(out, "    \"arg_stride_bytes\": 8,\n");
    fprintf(out, "    \"arg_count\": 3,\n");
    fprintf(out, "    \"wtag_formula\": \"(con_id << 9) | (space_id << 1) | 1\"\n");
    fprintf(out, "  },\n");

    /* Runtime frame layout */
    fprintf(out, "  \"runtime_frame_layout\": {\n");
    fprintf(out, "    \"struct_name\": \"TcIsaRuntimeFrame\",\n");
    fprintf(out, "    \"sizeof_bytes\": %zu,\n", sizeof(TcIsaRuntimeFrame));
    fprintf(out, "    \"alignof_bytes\": %zu,\n", _Alignof(TcIsaRuntimeFrame));
    fprintf(out, "    \"fields\": [\n");
    fprintf(out, "      {\"name\": \"s0_ptr\", \"offset\": %zu, \"size\": 8, \"type\": \"Ptr(0)\"},\n", offsetof(TcIsaRuntimeFrame, s0_ptr));
    fprintf(out, "      {\"name\": \"s0_len\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, s0_len));
    fprintf(out, "      {\"name\": \"s1_ptr\", \"offset\": %zu, \"size\": 8, \"type\": \"Ptr(1)\"},\n", offsetof(TcIsaRuntimeFrame, s1_ptr));
    fprintf(out, "      {\"name\": \"s1_len\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, s1_len));
    fprintf(out, "      {\"name\": \"s2_ptr\", \"offset\": %zu, \"size\": 8, \"type\": \"Ptr(2)\"},\n", offsetof(TcIsaRuntimeFrame, s2_ptr));
    fprintf(out, "      {\"name\": \"s2_len\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, s2_len));
    fprintf(out, "      {\"name\": \"s3_ptr\", \"offset\": %zu, \"size\": 8, \"type\": \"Ptr(3)\"},\n", offsetof(TcIsaRuntimeFrame, s3_ptr));
    fprintf(out, "      {\"name\": \"s3_len\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, s3_len));
    fprintf(out, "      {\"name\": \"s4_ptr\", \"offset\": %zu, \"size\": 8, \"type\": \"Ptr(4)\"},\n", offsetof(TcIsaRuntimeFrame, s4_ptr));
    fprintf(out, "      {\"name\": \"s4_len\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, s4_len));
    fprintf(out, "      {\"name\": \"s5_ptr\", \"offset\": %zu, \"size\": 8, \"type\": \"Ptr(5)\"},\n", offsetof(TcIsaRuntimeFrame, s5_ptr));
    fprintf(out, "      {\"name\": \"s5_len\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, s5_len));
    fprintf(out, "      {\"name\": \"s6_ptr\", \"offset\": %zu, \"size\": 8, \"type\": \"Ptr(6)\"},\n", offsetof(TcIsaRuntimeFrame, s6_ptr));
    fprintf(out, "      {\"name\": \"s6_len\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, s6_len));
    fprintf(out, "      {\"name\": \"s7_ptr\", \"offset\": %zu, \"size\": 8, \"type\": \"Ptr(7)\"},\n", offsetof(TcIsaRuntimeFrame, s7_ptr));
    fprintf(out, "      {\"name\": \"s7_len\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, s7_len));
    fprintf(out, "      {\"name\": \"s8_ptr\", \"offset\": %zu, \"size\": 8, \"type\": \"Ptr(8)\"},\n", offsetof(TcIsaRuntimeFrame, s8_ptr));
    fprintf(out, "      {\"name\": \"s8_len\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, s8_len));
    fprintf(out, "      {\"name\": \"status\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, status));
    fprintf(out, "      {\"name\": \"ret_val\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, ret_val));
    fprintf(out, "      {\"name\": \"fuel_budget\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, fuel_budget));
    fprintf(out, "      {\"name\": \"fuel_used\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, fuel_used));
    fprintf(out, "      {\"name\": \"arg0\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"},\n", offsetof(TcIsaRuntimeFrame, arg0));
    fprintf(out, "      {\"name\": \"arg1\", \"offset\": %zu, \"size\": 8, \"type\": \"U64\"}\n", offsetof(TcIsaRuntimeFrame, arg1));
    fprintf(out, "    ]\n");
    fprintf(out, "  },\n");

    /* ABI contract */
    fprintf(out, "  \"abi_contract\": {\n");
    fprintf(out, "    \"num_logical_registers\": 12,\n");
    fprintf(out, "    \"logical_registers\": [\"v0\",\"v1\",\"v2\",\"v3\",\"v4\",\"v5\",\"v6\",\"v7\",\"v8\",\"v9\",\"v10\",\"v11\"],\n");
    fprintf(out, "    \"x86_64_registers\": [\"rdi\",\"rsi\",\"rdx\",\"rcx\",\"r8\",\"r9\",\"r10\",\"r11\",\"rax\",\"rbx\",\"r12\",\"r13\"],\n");
    fprintf(out, "    \"aarch64_registers\": [\"x0\",\"x1\",\"x2\",\"x3\",\"x4\",\"x5\",\"x6\",\"x7\",\"x8\",\"x9\",\"x10\",\"x11\"],\n");
    fprintf(out, "    \"rv64_registers\": [\"a0\",\"a1\",\"a2\",\"a3\",\"a4\",\"a5\",\"a6\",\"a7\",\"t0\",\"t1\",\"t2\",\"t3\"],\n");
    fprintf(out, "    \"scratch_registers\": {\n");
    fprintf(out, "      \"logical\": [\"s0\",\"s1\"],\n");
    fprintf(out, "      \"x86_64\": [\"r14\",\"r15\"],\n");
    fprintf(out, "      \"aarch64\": [\"x12\",\"x13\"],\n");
    fprintf(out, "      \"rv64\": [\"t4\",\"t5\"]\n");
    fprintf(out, "    },\n");
    fprintf(out, "    \"x86_64_wrapper_saved\": [\"rbx\",\"r12\",\"r13\",\"r14\",\"r15\"],\n");
    fprintf(out, "    \"x86_64_wrapper_prologue_bytes_hex\": \"534154415541564157\",\n");
    fprintf(out, "    \"x86_64_wrapper_epilogue_bytes_hex\": \"415f415e415d415c5bc3\"\n");
    fprintf(out, "  },\n");

    /* Outcome codes directly from live TcIsaOutcome enum */
    fprintf(out, "  \"outcome_codes\": {\n");
    fprintf(out, "    \"refused\": %u,\n", (unsigned)TISA_OUTCOME_REFUSED);
    fprintf(out, "    \"fired\": %u,\n", (unsigned)TISA_OUTCOME_FIRED);
    fprintf(out, "    \"unsupported\": %u,\n", (unsigned)TISA_OUTCOME_UNSUPPORTED);
    fprintf(out, "    \"invalid_input\": %u,\n", (unsigned)TISA_OUTCOME_INVALID_INPUT);
    fprintf(out, "    \"exhausted\": %u\n", (unsigned)TISA_OUTCOME_EXHAUSTED);
    fprintf(out, "  },\n");

    /* Spaces */
    fprintf(out, "  \"spaces\": [\n");
    for (int sp = 0; sp < 3; sp++) {
        fprintf(out, "    {\"space_id\": %d, \"name\": \"%s\", \"protected\": %s}%s\n",
                sp, lean_tc_space_name((uint8_t)sp),
                tc_inspect_space_protected((uint8_t)sp) ? "true" : "false",
                (sp < 2) ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* Sorts */
    int nsort = tc_inspect_sort_count();
    fprintf(out, "  \"sorts\": [\n");
    for (int s = 1; s < nsort; s++) {
        uint8_t fl = tc_inspect_sort_flags(s);
        fprintf(out, "    {\"sort_id\": %d, \"name\": \"%s\", \"flags\": %u, \"strict\": %s, \"provable\": %s}%s\n",
                s, lean_tc_sort_name(s), (unsigned)fl,
                (fl & SORT_STRICT) ? "true" : "false",
                (fl & SORT_PROVABLE) ? "true" : "false",
                (s + 1 < nsort) ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* Constructors */
    int ncon = tc_inspect_con_count();
    fprintf(out, "  \"constructors\": [\n");
    for (int c = 0; c < ncon; c++) {
        TcInspectCon ic = {0};
        if (!tc_inspect_con(c, &ic)) return false;
        uint32_t wtag0 = ((uint32_t)c << 9) | (0u << 1) | 1u;
        uint32_t wtag1 = ((uint32_t)c << 9) | (1u << 1) | 1u;
        uint32_t wtag2 = ((uint32_t)c << 9) | (2u << 1) | 1u;
        fprintf(out, "    {\n");
        fprintf(out, "      \"con_id\": %d,\n", c);
        fprintf(out, "      \"name\": \"%s\",\n", lean_tc_con_name(c));
        fprintf(out, "      \"sort_id\": %u,\n", (unsigned)ic.sort);
        fprintf(out, "      \"sort_name\": \"%s\",\n", lean_tc_sort_name((int)ic.sort));
        fprintf(out, "      \"arity\": %u,\n", (unsigned)ic.arity);
        fprintf(out, "      \"arg_sorts\": [");
        for (int j = 0; j < ic.arity; j++) {
            fprintf(out, "%s%u", (j > 0 ? ", " : ""), (unsigned)ic.asort[j]);
        }
        fprintf(out, "],\n");
        fprintf(out, "      \"is_def\": %s,\n", ic.isdef ? "true" : "false");
        fprintf(out, "      \"abound\": %u,\n", ic.abound);
        fprintf(out, "      \"retdeps_hex\": \"0x%016" PRIx64 "\",\n", ic.retdeps);
        fprintf(out, "      \"wtag_by_space\": [%u, %u, %u],\n", wtag0, wtag1, wtag2);
        fprintf(out, "      \"wtag_by_space_hex\": [\"0x%08x\", \"0x%08x\", \"0x%08x\"]\n", wtag0, wtag1, wtag2);
        fprintf(out, "    }%s\n", (c + 1 < ncon) ? "," : "");
    }
    fprintf(out, "  ],\n");

    /* Constants */
    fprintf(out, "  \"constants\": [\n");
    fprintf(out, "    {\n");
    fprintf(out, "      \"name\": \"T_ID_ZERO\",\n");
    fprintf(out, "      \"term_id\": %u,\n", (unsigned)T_ID_ZERO);
    fprintf(out, "      \"kind\": \"K_APP\",\n");
    fprintf(out, "      \"con_id\": %d,\n", C_ID_ZERO);
    fprintf(out, "      \"con_name\": \"C_ID_ZERO\",\n");
    fprintf(out, "      \"sort_id\": %d,\n", S_ID);
    fprintf(out, "      \"sort_name\": \"S_ID\",\n");
    fprintf(out, "      \"arity\": 0,\n");
    fprintf(out, "      \"u64_payload_hex\": \"0x0000000000000000\"\n");
    fprintf(out, "    }\n");
    fprintf(out, "  ],\n");

    /* Rules (all 23 rules: 12 privileged leaf publishers + 11 premise-bearing flat rules) */
    int nrules = tc_rule_count();
    fprintf(out, "  \"rule_counts\": {\n");
    fprintf(out, "    \"total\": %d,\n", nrules);
    fprintf(out, "    \"privileged_leaf_publishers\": 12,\n");
    fprintf(out, "    \"premise_bearing_flat_rules\": 11\n");
    fprintf(out, "  },\n");
    fprintf(out, "  \"rules\": [\n");
    for (int rid = 0; rid < nrules; rid++) {
        TcInspectRule ir = {0};
        TcInspectFlat ifl = {0};
        if (!tc_inspect_rule(rid, &ir, &ifl)) return false;
        const Rule *r = &ir.decl_rule;
        bool is_leaf = (ir.mflat && !ir.flat && ir.nkeep == 0);

        fprintf(out, "    {\n");
        fprintf(out, "      \"rule_id\": %d,\n", rid);
        fprintf(out, "      \"name\": \"%s\",\n", lean_tc_rule_name(rid));
        fprintf(out, "      \"role\": \"%s\",\n",
                is_leaf ? "privileged_leaf_publisher" : "premise_bearing_flat_rule");
        fprintf(out, "      \"checking_role\": \"%s\",\n",
                is_leaf ? "trustedCHelperLeafPublisher" : "pureTcalcPremiseRule");
        fprintf(out, "      \"trusted_c_obligation\": \"%s\",\n", lean_tc_rule_obligation(rid));
        fprintf(out, "      \"fibre_flags\": %u,\n", (unsigned)ir.fibre);
        fprintf(out, "      \"pure\": %s,\n", ir.pure ? "true" : "false");
        fprintf(out, "      \"flat\": %s,\n", ir.flat ? "true" : "false");
        fprintf(out, "      \"mflat\": %s,\n", ir.mflat ? "true" : "false");
        fprintf(out, "      \"implied_mask_hex\": \"0x%016" PRIx64 "\",\n", ir.implied);
        fprintf(out, "      \"mimplied_mask_hex\": \"0x%016" PRIx64 "\",\n", ir.mimplied);
        fprintf(out, "      \"bounds_mask_hex\": \"0x%016" PRIx64 "\",\n", ir.bounds);
        fprintf(out, "      \"nvars\": %d,\n", ir.nvars);
        fprintf(out, "      \"context_vars\": [");
        for (int h = 0; h < ir.nvars; h++) {
            if (h > 0) fprintf(out, ", ");
            fprintf(out, "{\"var\": %d, \"sort_id\": %u, \"sort_name\": \"%s\", \"bound\": %u, \"avoid_mask_hex\": \"0x%016" PRIx64 "\"}",
                    h, (unsigned)ir.holes[h].sort, lean_tc_sort_name((int)ir.holes[h].sort),
                    (unsigned)ir.holes[h].bound, ir.holes[h].avoid);
        }
        fprintf(out, "],\n");
        fprintf(out, "      \"ntake\": %u,\n", (unsigned)r->ntake);
        fprintf(out, "      \"nkeep\": %u,\n", (unsigned)r->nkeep);
        fprintf(out, "      \"nabsent\": %u,\n", (unsigned)r->nabsent);
        fprintf(out, "      \"ncount\": %u,\n", (unsigned)r->ncount);
        fprintf(out, "      \"ntell\": %u,\n", (unsigned)r->ntell);
        fprintf(out, "      \"nneq\": %u,\n", (unsigned)r->nneq);
        fprintf(out, "      \"fresh\": %d,\n", (int)r->fresh);

        /* Declarative keep premises */
        fprintf(out, "      \"keep_premises\": [");
        if (r->nkeep > 0) fprintf(out, "\n");
        for (int i = 0; i < r->nkeep; i++) {
            fprintf(out, "        ");
            lean_tc_emit_pat_json(out, r->keep[i], 8);
            fprintf(out, "%s\n", (i + 1 < r->nkeep) ? "," : "");
        }
        if (r->nkeep > 0) fprintf(out, "      ],\n");
        else fprintf(out, "],\n");

        /* Declarative make conclusion */
        fprintf(out, "      \"make_conclusion\": ");
        lean_tc_emit_pat_json(out, r->make, 6);
        fprintf(out, ",\n");

        /* Compiled FL coordinates */
        if (!ir.flat) {
            fprintf(out, "      \"compiled_flat\": null,\n");
            fprintf(out, "      \"rule_table_plan\": null\n");
        } else {
            uint32_t mtag = (ifl.mhead << 9) | ((uint32_t)r->make.space << 1) | 1u;
            fprintf(out, "      \"compiled_flat\": {\n");
            fprintf(out, "        \"fl_index\": %u,\n", ifl.fl_index);
            fprintf(out, "        \"ordinary\": %s,\n", ifl.ordinary ? "true" : "false");
            fprintf(out, "        \"ktag\": [");
            for (int i = 0; i < r->nkeep; i++) {
                fprintf(out, "%s%u", (i > 0 ? ", " : ""), ifl.ktag[i]);
            }
            fprintf(out, "],\n");
            fprintf(out, "        \"ktag_hex\": [");
            for (int i = 0; i < r->nkeep; i++) {
                fprintf(out, "%s\"0x%08x\"", (i > 0 ? ", " : ""), ifl.ktag[i]);
            }
            fprintf(out, "],\n");
            fprintf(out, "        \"mhead\": %u,\n", ifl.mhead);
            fprintf(out, "        \"mhead_name\": \"%s\",\n", lean_tc_con_name((int)ifl.mhead));
            fprintf(out, "        \"msort\": %u,\n", (unsigned)ifl.msort);
            fprintf(out, "        \"msort_name\": \"%s\",\n", lean_tc_sort_name((int)ifl.msort));
            fprintf(out, "        \"mspace\": %u,\n", (unsigned)r->make.space);
            fprintf(out, "        \"mspace_name\": \"%s\",\n", lean_tc_space_name(r->make.space));
            fprintf(out, "        \"mtag\": %u,\n", mtag);
            fprintf(out, "        \"mtag_hex\": \"0x%08x\",\n", mtag);
            fprintf(out, "        \"mar\": %u,\n", (unsigned)ifl.mar);
            fprintf(out, "        \"nconst\": %u,\n", (unsigned)ifl.nconst);
            fprintf(out, "        \"constants\": [");
            for (int c = 0; c < ifl.nconst; c++) {
                Term ct = ifl.mc[c];
                uint64_t pay = lean_tc_const_payload_u64(ct);
                const char *cname = (ct == T_ID_ZERO) ? "T_ID_ZERO" : "K_NAME";
                fprintf(out, "%s{\"const_idx\": %d, \"term_id\": %u, \"name\": \"%s\", \"u64_payload_hex\": \"0x%016" PRIx64 "\"}",
                        (c > 0 ? ", " : ""), c, (unsigned)ct, cname, pay);
            }
            fprintf(out, "],\n");
            fprintf(out, "        \"nguard\": %u,\n", (unsigned)ifl.nguard);
            fprintf(out, "        \"guards\": [\n");
            for (int g = 0; g < ifl.nguard; g++) {
                TcInspectGuard q = ifl.guard[g];
                bool is_c = (q.i0 == r->nkeep);
                uint64_t cpay = is_c ? lean_tc_const_payload_u64(ifl.mc[q.j0]) : 0ull;
                fprintf(out, "          {\"guard_idx\": %d, \"i\": %u, \"j\": %u, \"i0\": %u, \"j0\": %u, \"packed_guard\": %u, \"packed_guard_hex\": \"0x%08x\", \"rhs_is_const\": %s, \"rhs_const_u64_hex\": %s",
                        g, (unsigned)q.i, (unsigned)q.j, (unsigned)q.i0, (unsigned)q.j0,
                        ifl.packed_guard[g], ifl.packed_guard[g],
                        is_c ? "true" : "false",
                        is_c ? "\"" : "null");
                if (is_c) fprintf(out, "0x%016" PRIx64 "\"", cpay);
                fprintf(out, "}%s\n", (g + 1 < ifl.nguard) ? "," : "");
            }
            fprintf(out, "        ],\n");
            fprintf(out, "        \"make_coords\": [\n");
            for (int j = 0; j < ifl.mar; j++) {
                bool from_c = (ifl.mi[j] == r->nkeep);
                uint64_t cpay = from_c ? lean_tc_const_payload_u64(ifl.mc[ifl.mj[j]]) : 0ull;
                fprintf(out, "          {\"out_arg\": %d, \"mi\": %u, \"mj\": %u, \"packed_make\": %u, \"packed_make_hex\": \"0x%08x\", \"from_const\": %s, \"const_u64_hex\": %s",
                        j, (unsigned)ifl.mi[j], (unsigned)ifl.mj[j],
                        ifl.packed_make[j], ifl.packed_make[j],
                        from_c ? "true" : "false",
                        from_c ? "\"" : "null");
                if (from_c) fprintf(out, "0x%016" PRIx64 "\"", cpay);
                fprintf(out, "}%s\n", (j + 1 < ifl.mar) ? "," : "");
            }
            fprintf(out, "        ]\n");
            fprintf(out, "      },\n");
            /* Pure logical RuleTable.Plan view (matching Mettapedia.Machines.TIsa.RuleTable.Plan) */
            fprintf(out, "      \"rule_table_plan\": {\n");
            fprintf(out, "        \"inputs\": [");
            for (int i = 0; i < r->nkeep; i++) {
                uint8_t pk = 0; uint32_t ph = 0; int par = 0; const Term *pa = NULL;
                tc_node(r->keep[i].t, &pk, &ph, &par, &pa);
                fprintf(out, "%s{\"tag\": %u, \"arity\": %d}",
                        (i > 0 ? ", " : ""), ifl.ktag[i], par);
            }
            fprintf(out, "],\n");
            fprintf(out, "        \"guards\": [");
            for (int g = 0; g < ifl.nguard; g++) {
                TcInspectGuard q = ifl.guard[g];
                bool is_c = (q.i0 == r->nkeep);
                uint64_t cpay = is_c ? lean_tc_const_payload_u64(ifl.mc[q.j0]) : 0ull;
                fprintf(out, "%s{\"left\": {\"kind\": \"field\", \"premise\": %u, \"field\": %u}, \"right\": ",
                        (g > 0 ? ", " : ""), (unsigned)q.i, (unsigned)q.j);
                if (is_c) {
                    fprintf(out, "{\"kind\": \"literal\", \"u64_payload_hex\": \"0x%016" PRIx64 "\"}}", cpay);
                } else {
                    fprintf(out, "{\"kind\": \"field\", \"premise\": %u, \"field\": %u}}",
                            (unsigned)q.i0, (unsigned)q.j0);
                }
            }
            fprintf(out, "],\n");
            fprintf(out, "        \"output_tag\": %u,\n", mtag);
            fprintf(out, "        \"output_fields\": [");
            for (int j = 0; j < ifl.mar; j++) {
                bool from_c = (ifl.mi[j] == r->nkeep);
                uint64_t cpay = from_c ? lean_tc_const_payload_u64(ifl.mc[ifl.mj[j]]) : 0ull;
                if (from_c) {
                    fprintf(out, "%s{\"kind\": \"literal\", \"u64_payload_hex\": \"0x%016" PRIx64 "\"}",
                            (j > 0 ? ", " : ""), cpay);
                } else {
                    fprintf(out, "%s{\"kind\": \"field\", \"premise\": %u, \"field\": %u}",
                            (j > 0 ? ", " : ""), (unsigned)ifl.mi[j], (unsigned)ifl.mj[j]);
                }
            }
            fprintf(out, "]\n");
            fprintf(out, "      }\n");
        }
        fprintf(out, "    }%s\n", (rid + 1 < nrules) ? "," : "");
    }
    fprintf(out, "  ]\n");
    fprintf(out, "}\n");
    return true;
}

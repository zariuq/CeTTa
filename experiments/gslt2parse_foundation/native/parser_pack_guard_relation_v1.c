#include "parser_pack_guard_relation_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    PPGuardRelationV1Match match;
    uint8_t *value_canonical;
    size_t value_canonical_len;
    uint8_t *proof_canonical;
    size_t proof_canonical_len;
} PPGuardRelationV1CanonicalMatch;

static void ppguard_relation_v1_set_error(char *buf,
                                          size_t size,
                                          const char *format,
                                          ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppguard_relation_v1_digest_valid(const char *digest) {
    size_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        char value = digest[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool ppguard_relation_v1_array_fits(size_t count,
                                            size_t element_size) {
    return element_size == 0u || count <= SIZE_MAX / element_size;
}

static bool ppguard_relation_v1_owner_has_active_range(
    const Arena *const *owners,
    uint32_t owner_len,
    const void *ptr,
    size_t size) {
    uintptr_t begin;
    uintptr_t end;
    uint32_t owner_index;

    if (!ptr || size == 0u)
        return false;
    begin = (uintptr_t)ptr;
    if (size > UINTPTR_MAX - begin)
        return false;
    end = begin + size;
    for (owner_index = 0u; owner_index < owner_len; owner_index++) {
        const Arena *owner = owners[owner_index];
        const ArenaBlock *block;
        if (!owner)
            continue;
        for (block = owner->head; block; block = block->next) {
            uintptr_t block_begin = (uintptr_t)block->data;
            uintptr_t block_end = block_begin + block->used;
            if (begin >= block_begin && end <= block_end)
                return true;
        }
    }
    return false;
}

static bool ppguard_relation_v1_atom_storage_valid(
    const PPGuardRelationV1 *relation) {
    uint32_t index;

    if (relation->atom_storage ==
        PPGUARD_RELATION_V1_ATOMS_SELF_CONTAINED) {
        return !relation->borrowed_atom_owners &&
            relation->borrowed_atom_owner_len == 0u;
    }
    if (relation->atom_storage != PPGUARD_RELATION_V1_ATOMS_BORROWED ||
        !relation->borrowed_atom_owners ||
        relation->borrowed_atom_owner_len == 0u) {
        return false;
    }
    for (index = 0u; index < relation->borrowed_atom_owner_len; index++) {
        if (!relation->borrowed_atom_owners[index])
            return false;
    }
    return true;
}

static bool ppguard_relation_v1_atom_is_available(
    const PPGuardRelationV1 *relation,
    const Atom *atom) {
    return relation->atom_storage ==
               PPGUARD_RELATION_V1_ATOMS_SELF_CONTAINED ||
        ppguard_relation_v1_owner_has_active_range(
            relation->borrowed_atom_owners,
            relation->borrowed_atom_owner_len,
            atom, sizeof(*atom));
}

static int ppguard_relation_v1_u32_compare(const void *left,
                                            const void *right) {
    uint32_t lhs = *(const uint32_t *)left;
    uint32_t rhs = *(const uint32_t *)right;
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

static int ppguard_relation_v1_bytes_compare(const uint8_t *left,
                                              size_t left_len,
                                              const uint8_t *right,
                                              size_t right_len) {
    size_t shared = left_len < right_len ? left_len : right_len;
    int comparison = shared > 0u ? memcmp(left, right, shared) : 0;

    if (comparison != 0)
        return comparison;
    return left_len < right_len ? -1 : left_len > right_len ? 1 : 0;
}

static int ppguard_relation_v1_match_compare(const void *left,
                                              const void *right) {
    const PPGuardRelationV1CanonicalMatch *lhs = left;
    const PPGuardRelationV1CanonicalMatch *rhs = right;

#define PPGUARD_MATCH_COMPARE(field) \
    do { \
        if (lhs->match.field != rhs->match.field) \
            return lhs->match.field < rhs->match.field ? -1 : 1; \
    } while (0)
    PPGUARD_MATCH_COMPARE(guard_terminal_id);
    PPGUARD_MATCH_COMPARE(scalar_left);
    PPGUARD_MATCH_COMPARE(body_scalar_right);
    PPGUARD_MATCH_COMPARE(byte_left);
    PPGUARD_MATCH_COMPARE(body_byte_right);
#undef PPGUARD_MATCH_COMPARE
    {
        int comparison = ppguard_relation_v1_bytes_compare(
            lhs->value_canonical, lhs->value_canonical_len,
            rhs->value_canonical, rhs->value_canonical_len);
        if (comparison != 0)
            return comparison;
    }
    return ppguard_relation_v1_bytes_compare(
        lhs->proof_canonical, lhs->proof_canonical_len,
        rhs->proof_canonical, rhs->proof_canonical_len);
}

static int ppguard_relation_v1_edge_compare(const void *left,
                                             const void *right) {
    const CettaLpNativeUtf8LatticeEdge *lhs = left;
    const CettaLpNativeUtf8LatticeEdge *rhs = right;

#define PPGUARD_EDGE_COMPARE(field) \
    do { \
        if (lhs->field != rhs->field) \
            return lhs->field < rhs->field ? -1 : 1; \
    } while (0)
    PPGUARD_EDGE_COMPARE(scalar_left);
    PPGUARD_EDGE_COMPARE(terminal_id);
    PPGUARD_EDGE_COMPARE(scalar_right);
    PPGUARD_EDGE_COMPARE(value_kind);
    PPGUARD_EDGE_COMPARE(value);
    PPGUARD_EDGE_COMPARE(byte_left);
    PPGUARD_EDGE_COMPARE(byte_right);
#undef PPGUARD_EDGE_COMPARE
    return 0;
}

static bool ppguard_relation_v1_terminal_declared(
    const uint32_t *terminal_ids,
    uint32_t terminal_len,
    uint32_t terminal_id) {
    uint32_t low = 0u;
    uint32_t high = terminal_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (terminal_ids[middle] < terminal_id)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < terminal_len && terminal_ids[low] == terminal_id;
}

static void ppguard_relation_v1_canonical_matches_free(
    PPGuardRelationV1CanonicalMatch *matches,
    uint32_t match_len) {
    uint32_t index;

    if (!matches)
        return;
    for (index = 0u; index < match_len; index++) {
        free(matches[index].value_canonical);
        free(matches[index].proof_canonical);
    }
    free(matches);
}

static void ppguard_relation_v1_sha_u32(CettaNativeSha256 *sha,
                                         uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppguard_relation_v1_sha_u64(CettaNativeSha256 *sha,
                                         uint64_t value) {
    uint8_t bytes[8];
    uint32_t index;

    for (index = 0u; index < 8u; index++)
        bytes[7u - index] = (uint8_t)(value >> (index * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppguard_relation_v1_sha_bytes(CettaNativeSha256 *sha,
                                           const uint8_t *bytes,
                                           size_t len) {
    ppguard_relation_v1_sha_u64(sha, (uint64_t)len);
    cetta_native_sha256_update(sha, bytes, len);
}

static bool ppguard_relation_v1_sha_atom(CettaNativeSha256 *sha,
                                          const Atom *atom) {
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    bool ok;

    ok = fh_ground_term_v1_render(
        atom, &canonical, &canonical_len, NULL, 0u);
    if (ok)
        ppguard_relation_v1_sha_bytes(sha, canonical, canonical_len);
    free(canonical);
    return ok;
}

static bool ppguard_relation_v1_digest(PPGuardRelationV1 *relation) {
    static const uint8_t domain[] =
        "ParserPackPositiveGuardRelationV1";
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    ppguard_relation_v1_sha_bytes(&sha, domain, sizeof(domain) - 1u);
    ppguard_relation_v1_sha_bytes(
        &sha, (const uint8_t *)relation->source_digest, 64u);
    ppguard_relation_v1_sha_u32(&sha, relation->lattice.scalar_len);
    ppguard_relation_v1_sha_u32(&sha, relation->lattice.input_byte_len);
    ppguard_relation_v1_sha_u32(&sha, relation->lattice.decoded_byte_len);
    ppguard_relation_v1_sha_u32(&sha, relation->lattice.source_pass_count);
    for (index = 0u; index < relation->lattice.scalar_len; index++)
        ppguard_relation_v1_sha_u32(&sha, relation->codepoints[index]);
    for (index = 0u; index <= relation->lattice.scalar_len; index++)
        ppguard_relation_v1_sha_u32(&sha, relation->byte_offsets[index]);
    ppguard_relation_v1_sha_u32(&sha, relation->lattice.terminal_len);
    for (index = 0u; index < relation->lattice.terminal_len; index++)
        ppguard_relation_v1_sha_u32(&sha, relation->terminal_ids[index]);
    ppguard_relation_v1_sha_u32(&sha, relation->lattice.edge_len);
    for (index = 0u; index < relation->lattice.edge_len; index++) {
        const CettaLpNativeUtf8LatticeEdge *edge =
            &relation->edges[index];
        ppguard_relation_v1_sha_u32(&sha, edge->terminal_id);
        ppguard_relation_v1_sha_u32(&sha, edge->scalar_left);
        ppguard_relation_v1_sha_u32(&sha, edge->scalar_right);
        ppguard_relation_v1_sha_u32(&sha, edge->byte_left);
        ppguard_relation_v1_sha_u32(&sha, edge->byte_right);
        ppguard_relation_v1_sha_u32(&sha, (uint32_t)edge->value_kind);
        ppguard_relation_v1_sha_u32(&sha, edge->value);
    }
    ppguard_relation_v1_sha_u32(&sha, relation->base_witness_len);
    ppguard_relation_v1_sha_u32(&sha, relation->witness_len);
    for (index = 0u; index < relation->witness_len; index++) {
        if (!ppguard_relation_v1_sha_atom(
                &sha, relation->witness_values[index])) {
            return false;
        }
    }
    ppguard_relation_v1_sha_u32(&sha, relation->evidence_len);
    for (index = 0u; index < relation->evidence_len; index++) {
        const PPGuardRelationV1Evidence *evidence =
            &relation->evidence[index];
        ppguard_relation_v1_sha_u32(&sha, evidence->guard_terminal_id);
        ppguard_relation_v1_sha_u32(&sha, evidence->scalar_left);
        ppguard_relation_v1_sha_u32(&sha, evidence->body_scalar_right);
        ppguard_relation_v1_sha_u32(&sha, evidence->byte_left);
        ppguard_relation_v1_sha_u32(&sha, evidence->body_byte_right);
        ppguard_relation_v1_sha_u32(&sha, evidence->witness_id);
        if (!ppguard_relation_v1_sha_atom(&sha, evidence->value) ||
            !ppguard_relation_v1_sha_atom(&sha, evidence->proof)) {
            return false;
        }
    }
    cetta_native_sha256_finish_hex(&sha, relation->relation_digest);
    return true;
}

void ppguard_relation_v1_init(PPGuardRelationV1 *relation) {
    if (!relation)
        return;
    memset(relation, 0, sizeof(*relation));
    arena_init(&relation->arena);
}

void ppguard_relation_v1_free(PPGuardRelationV1 *relation) {
    if (!relation)
        return;
    free(relation->terminal_ids);
    free(relation->edges);
    free(relation->start_offsets);
    free(relation->codepoints);
    free(relation->byte_offsets);
    free(relation->witness_values);
    free(relation->evidence);
    free(relation->borrowed_atom_owners);
    arena_free(&relation->arena);
    memset(relation, 0, sizeof(*relation));
}

bool ppguard_relation_v1_validate(
    const PPGuardRelationV1 *relation,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardRelationV1CanonicalMatch *canonical = NULL;
    PPGuardRelationV1 digest_copy;
    uint32_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!relation ||
        relation->lattice.terminal_ids != relation->terminal_ids ||
        relation->lattice.edges != relation->edges ||
        relation->lattice.start_offsets != relation->start_offsets ||
        relation->lattice.codepoints != relation->codepoints ||
        relation->lattice.byte_offsets != relation->byte_offsets ||
        relation->base_witness_len > relation->witness_len ||
        relation->evidence_len !=
            relation->witness_len - relation->base_witness_len ||
        (relation->witness_len > 0u && !relation->witness_values) ||
        (relation->evidence_len > 0u && !relation->evidence) ||
        !ppguard_relation_v1_atom_storage_valid(relation) ||
        !ppguard_relation_v1_digest_valid(relation->source_digest) ||
        !ppguard_relation_v1_digest_valid(relation->relation_digest) ||
        !cetta_lp_native_utf8_lattice_validate(
            &relation->lattice, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "malformed positive guard relation");
        }
        goto done;
    }
    for (index = 0u; index < relation->witness_len; index++) {
        uint8_t *text = NULL;
        size_t text_len = 0u;
        if (!relation->witness_values[index] ||
            !ppguard_relation_v1_atom_is_available(
                relation, relation->witness_values[index]) ||
            !fh_ground_term_v1_render(
                relation->witness_values[index], &text, &text_len,
                NULL, 0u)) {
            free(text);
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                relation->atom_storage ==
                        PPGUARD_RELATION_V1_ATOMS_BORROWED
                    ? "positive guard relation witness owner expired or changed"
                    : "positive guard relation has a nonground witness");
            goto done;
        }
        free(text);
    }
    if (relation->evidence_len > 0u) {
        canonical = calloc(relation->evidence_len, sizeof(*canonical));
        if (!canonical)
            goto done;
    }
    for (index = 0u; index < relation->evidence_len; index++) {
        const PPGuardRelationV1Evidence *evidence =
            &relation->evidence[index];
        PPGuardRelationV1CanonicalMatch *entry = &canonical[index];
        uint32_t edge_index;
        uint32_t matching_edges = 0u;

        if (evidence->witness_id != relation->base_witness_len + index ||
            evidence->witness_id >= relation->witness_len ||
            evidence->scalar_left > evidence->body_scalar_right ||
            evidence->body_scalar_right > relation->lattice.scalar_len ||
            evidence->byte_left !=
                relation->byte_offsets[evidence->scalar_left] ||
            evidence->body_byte_right !=
                relation->byte_offsets[evidence->body_scalar_right] ||
            !evidence->value || !evidence->proof ||
            !ppguard_relation_v1_atom_is_available(
                relation, evidence->value) ||
            !ppguard_relation_v1_atom_is_available(
                relation, evidence->proof) ||
            !atom_eq(relation->witness_values[evidence->witness_id],
                     evidence->value) ||
            !ppguard_relation_v1_terminal_declared(
                relation->terminal_ids, relation->lattice.terminal_len,
                evidence->guard_terminal_id)) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard evidence is inconsistent");
            goto done;
        }
        entry->match = (PPGuardRelationV1Match){
            .guard_terminal_id = evidence->guard_terminal_id,
            .scalar_left = evidence->scalar_left,
            .body_scalar_right = evidence->body_scalar_right,
            .byte_left = evidence->byte_left,
            .body_byte_right = evidence->body_byte_right,
            .value = evidence->value,
            .proof = evidence->proof,
        };
        if (!fh_ground_term_v1_render(
                evidence->value, &entry->value_canonical,
                &entry->value_canonical_len, NULL, 0u) ||
            !fh_ground_term_v1_render(
                evidence->proof, &entry->proof_canonical,
                &entry->proof_canonical_len, NULL, 0u) ||
            (index > 0u &&
             ppguard_relation_v1_match_compare(
                 &canonical[index - 1u], entry) >= 0)) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard evidence is not canonical and unique");
            goto done;
        }
        for (edge_index = 0u;
             edge_index < relation->lattice.edge_len; edge_index++) {
            const CettaLpNativeUtf8LatticeEdge *edge =
                &relation->edges[edge_index];
            if (edge->terminal_id == evidence->guard_terminal_id &&
                edge->scalar_left == evidence->scalar_left &&
                edge->scalar_right == evidence->scalar_left &&
                edge->byte_left == evidence->byte_left &&
                edge->byte_right == evidence->byte_left &&
                edge->value_kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS &&
                edge->value == evidence->witness_id) {
                matching_edges++;
            }
        }
        if (matching_edges != 1u) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard evidence lacks one exact zero-width edge");
            goto done;
        }
    }
    for (index = 0u; index < relation->lattice.edge_len; index++) {
        const CettaLpNativeUtf8LatticeEdge *edge =
            &relation->edges[index];
        if (edge->value_kind ==
                CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS &&
            edge->value >= relation->witness_len) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard relation edge has an invalid witness");
            goto done;
        }
    }
    digest_copy = *relation;
    if (!ppguard_relation_v1_digest(&digest_copy) ||
        strcmp(digest_copy.relation_digest,
               relation->relation_digest) != 0) {
        ppguard_relation_v1_set_error(
            error_buf, error_buf_size,
            "positive guard relation digest changed");
        goto done;
    }
    ok = true;

done:
    ppguard_relation_v1_canonical_matches_free(
        canonical, relation ? relation->evidence_len : 0u);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguard_relation_v1_set_error(
            error_buf, error_buf_size,
            "failed to validate positive guard relation");
    }
    return ok;
}

static bool ppguard_relation_v1_build_impl(
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const uint32_t *guard_terminal_ids,
    uint32_t guard_terminal_len,
    const PPGuardRelationV1Match *matches,
    uint32_t match_len,
    PPGuardRelationV1AtomStorage atom_storage,
    const Arena *const *atom_owners,
    uint32_t atom_owner_len,
    const char *source_digest,
    PPGuardRelationV1 *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardRelationV1 result;
    PPGuardRelationV1CanonicalMatch *canonical_matches = NULL;
    uint32_t *sorted_guard_ids = NULL;
    uint32_t unique_match_len = 0u;
    uint32_t terminal_len;
    uint32_t edge_len;
    uint32_t witness_len;
    uint32_t index;
    bool ok = false;

    ppguard_relation_v1_init(&result);
    result.atom_storage = atom_storage;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!base_lattice || !out || guard_terminal_len == 0u ||
        !guard_terminal_ids || (match_len > 0u && !matches) ||
        (base_witness_len > 0u && !base_witness_values) ||
        (atom_storage != PPGUARD_RELATION_V1_ATOMS_SELF_CONTAINED &&
         atom_storage != PPGUARD_RELATION_V1_ATOMS_BORROWED) ||
        (atom_storage == PPGUARD_RELATION_V1_ATOMS_SELF_CONTAINED &&
         (atom_owners || atom_owner_len > 0u)) ||
        (atom_storage == PPGUARD_RELATION_V1_ATOMS_BORROWED &&
         (!atom_owners || atom_owner_len == 0u)) ||
        !ppguard_relation_v1_digest_valid(source_digest) ||
        !cetta_lp_native_utf8_lattice_validate(
            base_lattice, error_buf, error_buf_size)) {
        if (!error_buf || error_buf_size == 0u || error_buf[0] == '\0') {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "bad positive guard relation inputs");
        }
        goto done;
    }
    for (index = 0u; index < atom_owner_len; index++) {
        if (!atom_owners[index] || atom_owners[index] == &out->arena) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "invalid positive guard Atom owner");
            goto done;
        }
    }
    if (guard_terminal_len > UINT32_MAX - base_lattice->terminal_len) {
        ppguard_relation_v1_set_error(
            error_buf, error_buf_size,
            "positive guard terminal count overflow");
        goto done;
    }
    if (!ppguard_relation_v1_array_fits(
            guard_terminal_len, sizeof(*sorted_guard_ids))) {
        ppguard_relation_v1_set_error(
            error_buf, error_buf_size,
            "positive guard terminal allocation overflow");
        goto done;
    }
    sorted_guard_ids = malloc(
        sizeof(*sorted_guard_ids) * (size_t)guard_terminal_len);
    if (!sorted_guard_ids)
        goto done;
    memcpy(sorted_guard_ids, guard_terminal_ids,
           sizeof(*sorted_guard_ids) * (size_t)guard_terminal_len);
    qsort(sorted_guard_ids, guard_terminal_len,
          sizeof(*sorted_guard_ids), ppguard_relation_v1_u32_compare);
    for (index = 0u; index < guard_terminal_len; index++) {
        if ((index > 0u &&
             sorted_guard_ids[index - 1u] == sorted_guard_ids[index]) ||
            ppguard_relation_v1_terminal_declared(
                base_lattice->terminal_ids, base_lattice->terminal_len,
                sorted_guard_ids[index])) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard terminal IDs are not a disjoint set");
            goto done;
        }
    }
    for (index = 0u; index < base_lattice->edge_len; index++) {
        const CettaLpNativeUtf8LatticeEdge *edge =
            &base_lattice->edges[index];
        if (edge->value_kind ==
                CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS &&
            edge->value >= base_witness_len) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "base lattice witness ID is outside its value table");
            goto done;
        }
    }
    for (index = 0u; index < base_witness_len; index++) {
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        if (!base_witness_values[index] ||
            (atom_storage == PPGUARD_RELATION_V1_ATOMS_BORROWED &&
             !ppguard_relation_v1_owner_has_active_range(
                 atom_owners, atom_owner_len,
                 base_witness_values[index],
                 sizeof(*base_witness_values[index]))) ||
            !fh_ground_term_v1_render(
                base_witness_values[index], &canonical, &canonical_len,
                NULL, 0u)) {
            free(canonical);
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                atom_storage == PPGUARD_RELATION_V1_ATOMS_BORROWED
                    ? "base lattice witness is outside declared Atom owners"
                    : "base lattice witness value is not ground");
            goto done;
        }
        free(canonical);
    }
    if (match_len > 0u) {
        if (!ppguard_relation_v1_array_fits(
                match_len, sizeof(*canonical_matches))) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard match allocation overflow");
            goto done;
        }
        canonical_matches = calloc(
            match_len, sizeof(*canonical_matches));
        if (!canonical_matches)
            goto done;
    }
    for (index = 0u; index < match_len; index++) {
        const PPGuardRelationV1Match *match = &matches[index];
        PPGuardRelationV1CanonicalMatch *canonical =
            &canonical_matches[index];
        if (!ppguard_relation_v1_terminal_declared(
                sorted_guard_ids, guard_terminal_len,
                match->guard_terminal_id) ||
            match->scalar_left > match->body_scalar_right ||
            match->body_scalar_right > base_lattice->scalar_len ||
            match->byte_left !=
                base_lattice->byte_offsets[match->scalar_left] ||
            match->body_byte_right !=
                base_lattice->byte_offsets[match->body_scalar_right] ||
            !match->value || !match->proof) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard body match is inconsistent");
            goto done;
        }
        if (atom_storage == PPGUARD_RELATION_V1_ATOMS_BORROWED &&
            (!ppguard_relation_v1_owner_has_active_range(
                 atom_owners, atom_owner_len,
                 match->value, sizeof(*match->value)) ||
             !ppguard_relation_v1_owner_has_active_range(
                 atom_owners, atom_owner_len,
                 match->proof, sizeof(*match->proof)))) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard value or proof is outside declared Atom owners");
            goto done;
        }
        canonical->match = *match;
        if (!fh_ground_term_v1_render(
                match->value, &canonical->value_canonical,
                &canonical->value_canonical_len, NULL, 0u) ||
            !fh_ground_term_v1_render(
                match->proof, &canonical->proof_canonical,
                &canonical->proof_canonical_len, NULL, 0u)) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard value or proof is not ground");
            goto done;
        }
    }
    if (match_len > 1u) {
        qsort(canonical_matches, match_len,
              sizeof(*canonical_matches),
              ppguard_relation_v1_match_compare);
    }
    for (index = 0u; index < match_len; index++) {
        if (unique_match_len > 0u &&
            ppguard_relation_v1_match_compare(
                &canonical_matches[unique_match_len - 1u],
                &canonical_matches[index]) == 0) {
            PPGuardRelationV1CanonicalMatch *previous =
                &canonical_matches[unique_match_len - 1u];
            if (!atom_eq((Atom *)previous->match.value,
                         (Atom *)canonical_matches[index].match.value) ||
                !atom_eq((Atom *)previous->match.proof,
                         (Atom *)canonical_matches[index].match.proof)) {
                ppguard_relation_v1_set_error(
                    error_buf, error_buf_size,
                    "canonical positive guard evidence collision");
                goto done;
            }
            free(canonical_matches[index].value_canonical);
            free(canonical_matches[index].proof_canonical);
            canonical_matches[index].value_canonical = NULL;
            canonical_matches[index].proof_canonical = NULL;
            continue;
        }
        if (unique_match_len != index) {
            canonical_matches[unique_match_len] = canonical_matches[index];
            memset(&canonical_matches[index], 0,
                   sizeof(canonical_matches[index]));
        }
        unique_match_len++;
    }
    if (unique_match_len > UINT32_MAX - base_witness_len ||
        unique_match_len > UINT32_MAX - base_lattice->edge_len) {
        ppguard_relation_v1_set_error(
            error_buf, error_buf_size,
            "positive guard relation size overflow");
        goto done;
    }
    terminal_len = base_lattice->terminal_len + guard_terminal_len;
    edge_len = base_lattice->edge_len + unique_match_len;
    witness_len = base_witness_len + unique_match_len;
    if (!ppguard_relation_v1_array_fits(
            terminal_len, sizeof(*result.terminal_ids)) ||
        !ppguard_relation_v1_array_fits(
            edge_len, sizeof(*result.edges)) ||
        !ppguard_relation_v1_array_fits(
            (size_t)base_lattice->scalar_len + 2u,
            sizeof(*result.start_offsets)) ||
        !ppguard_relation_v1_array_fits(
            base_lattice->scalar_len, sizeof(*result.codepoints)) ||
        !ppguard_relation_v1_array_fits(
            (size_t)base_lattice->scalar_len + 1u,
            sizeof(*result.byte_offsets)) ||
        !ppguard_relation_v1_array_fits(
            witness_len, sizeof(*result.witness_values)) ||
        !ppguard_relation_v1_array_fits(
            unique_match_len, sizeof(*result.evidence)) ||
        !ppguard_relation_v1_array_fits(
            atom_owner_len, sizeof(*result.borrowed_atom_owners))) {
        ppguard_relation_v1_set_error(
            error_buf, error_buf_size,
            "positive guard relation allocation overflow");
        goto done;
    }
    result.terminal_ids = malloc(
        sizeof(*result.terminal_ids) * (size_t)terminal_len);
    result.start_offsets = calloc(
        (size_t)base_lattice->scalar_len + 2u,
        sizeof(*result.start_offsets));
    result.byte_offsets = malloc(
        sizeof(*result.byte_offsets) *
        ((size_t)base_lattice->scalar_len + 1u));
    if ((edge_len > 0u &&
         !(result.edges = malloc(
               sizeof(*result.edges) * (size_t)edge_len))) ||
        (base_lattice->scalar_len > 0u &&
         !(result.codepoints = malloc(
               sizeof(*result.codepoints) *
               (size_t)base_lattice->scalar_len))) ||
        (witness_len > 0u &&
         !(result.witness_values = calloc(
               witness_len, sizeof(*result.witness_values)))) ||
        (unique_match_len > 0u &&
         !(result.evidence = calloc(
               unique_match_len, sizeof(*result.evidence)))) ||
        (atom_owner_len > 0u &&
         !(result.borrowed_atom_owners = malloc(
               sizeof(*result.borrowed_atom_owners) *
               (size_t)atom_owner_len))) ||
        !result.terminal_ids || !result.start_offsets ||
        !result.byte_offsets) {
        goto done;
    }
    if (atom_owner_len > 0u) {
        memcpy(result.borrowed_atom_owners, atom_owners,
               sizeof(*result.borrowed_atom_owners) *
               (size_t)atom_owner_len);
    }
    result.borrowed_atom_owner_len = atom_owner_len;
    if (base_lattice->terminal_len > 0u) {
        memcpy(result.terminal_ids, base_lattice->terminal_ids,
               sizeof(*result.terminal_ids) *
               (size_t)base_lattice->terminal_len);
    }
    memcpy(result.terminal_ids + base_lattice->terminal_len,
           sorted_guard_ids,
           sizeof(*result.terminal_ids) * (size_t)guard_terminal_len);
    qsort(result.terminal_ids, terminal_len,
          sizeof(*result.terminal_ids), ppguard_relation_v1_u32_compare);
    if (base_lattice->edge_len > 0u) {
        memcpy(result.edges, base_lattice->edges,
               sizeof(*result.edges) *
               (size_t)base_lattice->edge_len);
    }
    if (base_lattice->scalar_len > 0u) {
        memcpy(result.codepoints, base_lattice->codepoints,
               sizeof(*result.codepoints) *
               (size_t)base_lattice->scalar_len);
    }
    memcpy(result.byte_offsets, base_lattice->byte_offsets,
           sizeof(*result.byte_offsets) *
           ((size_t)base_lattice->scalar_len + 1u));
    for (index = 0u; index < base_witness_len; index++) {
        result.witness_values[index] =
            atom_storage == PPGUARD_RELATION_V1_ATOMS_BORROWED
                ? base_witness_values[index]
                : atom_deep_copy(
                      &result.arena, base_witness_values[index]);
        if (!result.witness_values[index])
            goto done;
    }
    for (index = 0u; index < unique_match_len; index++) {
        const PPGuardRelationV1Match *match =
            &canonical_matches[index].match;
        PPGuardRelationV1Evidence *evidence = &result.evidence[index];
        uint32_t witness_id = base_witness_len + index;

        evidence->guard_terminal_id = match->guard_terminal_id;
        evidence->scalar_left = match->scalar_left;
        evidence->body_scalar_right = match->body_scalar_right;
        evidence->byte_left = match->byte_left;
        evidence->body_byte_right = match->body_byte_right;
        evidence->witness_id = witness_id;
        evidence->value =
            atom_storage == PPGUARD_RELATION_V1_ATOMS_BORROWED
                ? (Atom *)match->value
                : atom_deep_copy(
                      &result.arena, (Atom *)match->value);
        evidence->proof =
            atom_storage == PPGUARD_RELATION_V1_ATOMS_BORROWED
                ? (Atom *)match->proof
                : atom_deep_copy(
                      &result.arena, (Atom *)match->proof);
        if (!evidence->value || !evidence->proof)
            goto done;
        result.witness_values[witness_id] = evidence->value;
        result.edges[base_lattice->edge_len + index] =
            (CettaLpNativeUtf8LatticeEdge){
                .terminal_id = match->guard_terminal_id,
                .scalar_left = match->scalar_left,
                .scalar_right = match->scalar_left,
                .byte_left = match->byte_left,
                .byte_right = match->byte_left,
                .value_kind =
                    CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS,
                .value = witness_id,
            };
    }
    if (edge_len > 1u) {
        qsort(result.edges, edge_len, sizeof(*result.edges),
              ppguard_relation_v1_edge_compare);
    }
    for (index = 0u; index < edge_len; index++) {
        uint32_t offset_index = result.edges[index].scalar_left + 1u;
        if (result.start_offsets[offset_index] == UINT32_MAX) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard position count overflow");
            goto done;
        }
        result.start_offsets[offset_index]++;
    }
    for (index = 1u; index <= base_lattice->scalar_len + 1u; index++) {
        if (result.start_offsets[index] >
            UINT32_MAX - result.start_offsets[index - 1u]) {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "positive guard position offset overflow");
            goto done;
        }
        result.start_offsets[index] += result.start_offsets[index - 1u];
    }
    result.lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = result.terminal_ids,
        .terminal_len = terminal_len,
        .edges = result.edges,
        .edge_len = edge_len,
        .start_offsets = result.start_offsets,
        .start_offset_len = base_lattice->scalar_len + 2u,
        .codepoints = result.codepoints,
        .byte_offsets = result.byte_offsets,
        .scalar_len = base_lattice->scalar_len,
        .input_byte_len = base_lattice->input_byte_len,
        .decoded_byte_len = base_lattice->decoded_byte_len,
        .source_pass_count = base_lattice->source_pass_count,
    };
    result.witness_len = witness_len;
    result.base_witness_len = base_witness_len;
    result.evidence_len = unique_match_len;
    memcpy(result.source_digest, source_digest, 65u);
    if (!cetta_lp_native_utf8_lattice_validate(
            &result.lattice, error_buf, error_buf_size) ||
        !ppguard_relation_v1_digest(&result) ||
        !ppguard_relation_v1_validate(
            &result, error_buf, error_buf_size)) {
        if (!error_buf || error_buf_size == 0u || error_buf[0] == '\0') {
            ppguard_relation_v1_set_error(
                error_buf, error_buf_size,
                "failed to seal positive guard relation");
        }
        goto done;
    }
    ppguard_relation_v1_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    ppguard_relation_v1_canonical_matches_free(
        canonical_matches, match_len);
    free(sorted_guard_ids);
    ppguard_relation_v1_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguard_relation_v1_set_error(
            error_buf, error_buf_size,
            "failed to build positive guard relation");
    }
    return ok;
}

bool ppguard_relation_v1_build(
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const uint32_t *guard_terminal_ids,
    uint32_t guard_terminal_len,
    const PPGuardRelationV1Match *matches,
    uint32_t match_len,
    const char *source_digest,
    PPGuardRelationV1 *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppguard_relation_v1_build_impl(
        base_lattice, base_witness_values, base_witness_len,
        guard_terminal_ids, guard_terminal_len, matches, match_len,
        PPGUARD_RELATION_V1_ATOMS_SELF_CONTAINED, NULL, 0u,
        source_digest, out, error_buf, error_buf_size);
}

bool ppguard_relation_v1_build_borrowed_atoms(
    const CettaLpNativeUtf8Lattice *base_lattice,
    Atom *const *base_witness_values,
    uint32_t base_witness_len,
    const uint32_t *guard_terminal_ids,
    uint32_t guard_terminal_len,
    const PPGuardRelationV1Match *matches,
    uint32_t match_len,
    const Arena *const *atom_owners,
    uint32_t atom_owner_len,
    const char *source_digest,
    PPGuardRelationV1 *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppguard_relation_v1_build_impl(
        base_lattice, base_witness_values, base_witness_len,
        guard_terminal_ids, guard_terminal_len, matches, match_len,
        PPGUARD_RELATION_V1_ATOMS_BORROWED,
        atom_owners, atom_owner_len, source_digest,
        out, error_buf, error_buf_size);
}

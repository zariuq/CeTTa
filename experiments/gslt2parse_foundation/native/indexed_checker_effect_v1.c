#include "indexed_checker_effect_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PPINDEXED_CHECKER_EFFECT_V1_MAX_RECORDS = 65536u,
    PPINDEXED_CHECKER_POLICY_V1_ALLOW_INNER_CONSTANTS = 0u,
    PPINDEXED_CHECKER_POLICY_V1_ALLOW_DUPLICATE_FLOATING = 1u,
    PPINDEXED_CHECKER_POLICY_V1_ALLOW_TOPLEVEL_ESSENTIAL = 2u,
    PPINDEXED_CHECKER_POLICY_V1_REJECT_UNKNOWN_STEPS = 3u,
    PPINDEXED_CHECKER_POLICY_V1_SKIP_COMPLETED_SOURCES = 4u,
    PPINDEXED_CHECKER_POLICY_V1_REJECT_ACTIVE_SOURCE_CYCLES = 5u,
    PPINDEXED_CHECKER_POLICY_V1_COUNT = 6u
};

typedef struct {
    char *operation;
    PPIndexedCheckerEffectV1Kind effect;
} PPIndexedCheckerEffectV1RawRow;

static const char *const ppice_v1_effect_names[
    PPINDEXED_CHECKER_EFFECT_V1_COUNT] = {
    "checker-open-scope",
    "checker-close-scope",
    "checker-declare-constants",
    "checker-declare-variables",
    "checker-declare-distinct",
    "checker-declare-floating",
    "checker-declare-essential",
    "checker-declare-rule",
    "checker-check-normal",
    "checker-check-compressed",
    "checker-resolve-source",
    "checker-complete-scope",
};

static const char *const ppice_v1_policy_names[
    PPINDEXED_CHECKER_POLICY_V1_COUNT] = {
    "checker-allow-inner-constants",
    "checker-allow-duplicate-floating",
    "checker-allow-toplevel-essential",
    "checker-reject-unknown-steps",
    "checker-skip-completed-sources",
    "checker-reject-active-source-cycles",
};

static void ppice_v1_set_error(char *buf,
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

static bool ppice_v1_digest_valid(const char *digest) {
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

static bool ppice_v1_expr_head(const Atom *atom,
                               const char *head,
                               CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static char *ppice_v1_render(const Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static int ppice_v1_text_compare(const void *left, const void *right) {
    const char *const *lhs = left;
    const char *const *rhs = right;
    return strcmp(*lhs, *rhs);
}

static int32_t ppice_v1_operation_find(
    const PPOccurrenceFoldV1Plan *plan,
    const char *name) {
    uint32_t low = 0u;
    uint32_t high = plan->operation_len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        int comparison = strcmp(plan->operations[middle], name);
        if (comparison < 0)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= plan->operation_len ||
        strcmp(plan->operations[low], name) != 0) {
        return -1;
    }
    return (int32_t)low;
}

static int32_t ppice_v1_effect_find(const char *name) {
    uint32_t index;

    for (index = 0u;
         index < PPINDEXED_CHECKER_EFFECT_V1_COUNT;
         index++) {
        if (strcmp(ppice_v1_effect_names[index], name) == 0)
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppice_v1_policy_find(const char *name) {
    uint32_t index;

    for (index = 0u;
         index < PPINDEXED_CHECKER_POLICY_V1_COUNT;
         index++) {
        if (strcmp(ppice_v1_policy_names[index], name) == 0)
            return (int32_t)index;
    }
    return -1;
}

static bool *ppice_v1_policy_field(
    PPIndexedCheckerPolicyV1 *policy,
    uint32_t policy_id) {
    if (!policy)
        return NULL;
    switch (policy_id) {
    case PPINDEXED_CHECKER_POLICY_V1_ALLOW_INNER_CONSTANTS:
        return &policy->allow_inner_constants;
    case PPINDEXED_CHECKER_POLICY_V1_ALLOW_DUPLICATE_FLOATING:
        return &policy->allow_duplicate_floating;
    case PPINDEXED_CHECKER_POLICY_V1_ALLOW_TOPLEVEL_ESSENTIAL:
        return &policy->allow_toplevel_essential;
    case PPINDEXED_CHECKER_POLICY_V1_REJECT_UNKNOWN_STEPS:
        return &policy->reject_unknown_steps;
    case PPINDEXED_CHECKER_POLICY_V1_SKIP_COMPLETED_SOURCES:
        return &policy->skip_completed_sources;
    case PPINDEXED_CHECKER_POLICY_V1_REJECT_ACTIVE_SOURCE_CYCLES:
        return &policy->reject_active_source_cycles;
    default:
        return NULL;
    }
}

static bool ppice_v1_policy_value(
    const Atom *value,
    bool *out) {
    if (!value || !out)
        return false;
    if (atom_is_symbol((Atom *)value, "policy-false")) {
        *out = false;
        return true;
    }
    if (atom_is_symbol((Atom *)value, "policy-true")) {
        *out = true;
        return true;
    }
    return false;
}

static bool ppice_v1_compiler_plan_digest(
    Atom *const *records,
    uint32_t record_len,
    char out[65]) {
    CettaNativeSha256 sha;
    char **canonical = NULL;
    uint32_t index;
    bool ok = false;

    canonical = calloc(
        record_len ? record_len : 1u, sizeof(*canonical));
    if (!canonical)
        goto done;
    for (index = 0u; index < record_len; index++) {
        canonical[index] = ppice_v1_render(records[index]);
        if (!canonical[index])
            goto done;
    }
    if (record_len > 1u) {
        qsort(
            canonical, record_len, sizeof(*canonical),
            ppice_v1_text_compare);
    }
    for (index = 1u; index < record_len; index++) {
        if (strcmp(canonical[index - 1u], canonical[index]) >= 0)
            goto done;
    }
    cetta_native_sha256_init(&sha);
    for (index = 0u; index < record_len; index++) {
        cetta_native_sha256_update(
            &sha, (const uint8_t *)canonical[index],
            strlen(canonical[index]));
        cetta_native_sha256_update(
            &sha, (const uint8_t *)"\n", 1u);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    ok = true;

done:
    if (canonical) {
        for (index = 0u; index < record_len; index++)
            free(canonical[index]);
    }
    free(canonical);
    return ok;
}

static void ppice_v1_sha_u32(CettaNativeSha256 *sha, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppice_v1_sha_text(
    CettaNativeSha256 *sha,
    const char *text) {
    uint64_t len = (uint64_t)strlen(text);
    uint8_t length[8];
    uint32_t index;

    for (index = 0u; index < 8u; index++)
        length[7u - index] = (uint8_t)(len >> (index * 8u));
    cetta_native_sha256_update(sha, length, sizeof(length));
    cetta_native_sha256_update(
        sha, (const uint8_t *)text, (size_t)len);
}

static bool ppice_v1_plan_digest(
    const PPIndexedCheckerEffectV1Plan *plan,
    char out[65]) {
    static const char domain[] = "IndexedCheckerEffectPlanV1";
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    ppice_v1_sha_text(&sha, domain);
    ppice_v1_sha_text(&sha, plan->occurrence_fold_plan_digest);
    ppice_v1_sha_text(&sha, plan->compiler_plan_digest);
    ppice_v1_sha_u32(&sha, plan->operation_len);
    for (index = 0u; index < plan->operation_len; index++) {
        ppice_v1_sha_u32(
            &sha, (uint32_t)plan->effect_by_operation[index]);
    }
    ppice_v1_sha_u32(
        &sha, plan->policy.allow_inner_constants ? 1u : 0u);
    ppice_v1_sha_u32(
        &sha, plan->policy.allow_duplicate_floating ? 1u : 0u);
    ppice_v1_sha_u32(
        &sha, plan->policy.allow_toplevel_essential ? 1u : 0u);
    ppice_v1_sha_u32(
        &sha, plan->policy.reject_unknown_steps ? 1u : 0u);
    ppice_v1_sha_u32(
        &sha, plan->policy.skip_completed_sources ? 1u : 0u);
    ppice_v1_sha_u32(
        &sha, plan->policy.reject_active_source_cycles ? 1u : 0u);
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

void ppindexed_checker_effect_v1_plan_init(
    PPIndexedCheckerEffectV1Plan *plan) {
    if (plan)
        memset(plan, 0, sizeof(*plan));
}

void ppindexed_checker_effect_v1_plan_free(
    PPIndexedCheckerEffectV1Plan *plan) {
    if (!plan)
        return;
    free(plan->effect_by_operation);
    memset(plan, 0, sizeof(*plan));
}

const char *ppindexed_checker_effect_v1_kind_name(
    PPIndexedCheckerEffectV1Kind kind) {
    return (uint32_t)kind < PPINDEXED_CHECKER_EFFECT_V1_COUNT
        ? ppice_v1_effect_names[(uint32_t)kind] : NULL;
}

bool ppindexed_checker_effect_v1_lookup(
    const PPIndexedCheckerEffectV1Plan *plan,
    uint32_t operation_id,
    PPIndexedCheckerEffectV1Kind *out) {
    if (!plan || !out || operation_id >= plan->operation_len ||
        !plan->effect_by_operation ||
        (uint32_t)plan->effect_by_operation[operation_id] >=
            PPINDEXED_CHECKER_EFFECT_V1_COUNT) {
        return false;
    }
    *out = plan->effect_by_operation[operation_id];
    return true;
}

bool ppindexed_checker_effect_v1_plan_validate(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPIndexedCheckerEffectV1Plan *plan,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    char digest[65];

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || !occurrence_plan || !plan ||
        !ppoccurrence_fold_v1_plan_validate_program(
            program, occurrence_plan, error_buf, error_buf_size) ||
        !ppice_v1_digest_valid(plan->occurrence_fold_plan_digest) ||
        !ppice_v1_digest_valid(plan->compiler_plan_digest) ||
        !ppice_v1_digest_valid(plan->plan_digest) ||
        strcmp(
            plan->occurrence_fold_plan_digest,
            occurrence_plan->plan_digest) != 0 ||
        plan->operation_len != occurrence_plan->operation_len ||
        plan->operation_len == 0u ||
        !plan->effect_by_operation) {
        if (error_buf && error_buf_size > 0u &&
            error_buf[0] == '\0') {
            ppice_v1_set_error(
                error_buf, error_buf_size,
                "invalid indexed-checker effect-plan identity");
        }
        return false;
    }
    for (index = 0u; index < plan->operation_len; index++) {
        if ((uint32_t)plan->effect_by_operation[index] >=
                PPINDEXED_CHECKER_EFFECT_V1_COUNT) {
            ppice_v1_set_error(
                error_buf, error_buf_size,
                "indexed-checker effect plan contains an unknown effect");
            return false;
        }
    }
    if (!ppice_v1_plan_digest(plan, digest) ||
        strcmp(digest, plan->plan_digest) != 0) {
        ppice_v1_set_error(
            error_buf, error_buf_size,
            "indexed-checker effect-plan digest changed");
        return false;
    }
    return true;
}

bool ppindexed_checker_effect_v1_plan_build(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    Atom *const *records,
    size_t record_len,
    const char *compiler_plan_digest,
    PPIndexedCheckerEffectV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedCheckerEffectV1Plan result;
    PPIndexedCheckerEffectV1RawRow *rows = NULL;
    char computed_compiler_digest[65];
    uint32_t index;
    bool policy_seen[PPINDEXED_CHECKER_POLICY_V1_COUNT] = {false};
    bool ok = false;

    ppindexed_checker_effect_v1_plan_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || !occurrence_plan || !out ||
        (!records && record_len > 0u) ||
        record_len == 0u ||
        record_len > PPINDEXED_CHECKER_EFFECT_V1_MAX_RECORDS ||
        record_len > UINT32_MAX ||
        record_len !=
            (size_t)occurrence_plan->operation_len +
                PPINDEXED_CHECKER_POLICY_V1_COUNT ||
        !ppice_v1_digest_valid(compiler_plan_digest) ||
        !ppoccurrence_fold_v1_plan_validate_program(
            program, occurrence_plan, error_buf, error_buf_size) ||
        !ppice_v1_compiler_plan_digest(
            records, (uint32_t)record_len,
            computed_compiler_digest) ||
        strcmp(
            computed_compiler_digest,
            compiler_plan_digest) != 0) {
        if (error_buf && error_buf_size > 0u &&
            error_buf[0] == '\0') {
            ppice_v1_set_error(
                error_buf, error_buf_size,
                "bad or unauthenticated indexed-checker effect plan");
        }
        goto done;
    }
    rows = calloc(record_len, sizeof(*rows));
    result.effect_by_operation = malloc(
        (size_t)occurrence_plan->operation_len *
            sizeof(*result.effect_by_operation));
    if (!rows || !result.effect_by_operation)
        goto done;
    result.operation_len = occurrence_plan->operation_len;
    for (index = 0u; index < result.operation_len; index++) {
        result.effect_by_operation[index] =
            (PPIndexedCheckerEffectV1Kind)
                PPINDEXED_CHECKER_EFFECT_V1_COUNT;
    }
    for (index = 0u; index < (uint32_t)record_len; index++) {
        Atom *record = records[index];
        if (ppice_v1_expr_head(
                record, "indexed-checker-effect-v1", 2u)) {
            char *effect_name = NULL;
            int32_t operation_id;
            int32_t effect_id;

            rows[index].operation =
                ppice_v1_render(record->expr.elems[1]);
            effect_name = ppice_v1_render(record->expr.elems[2]);
            if (!rows[index].operation || !effect_name) {
                free(effect_name);
                goto done;
            }
            operation_id = ppice_v1_operation_find(
                occurrence_plan, rows[index].operation);
            effect_id = ppice_v1_effect_find(effect_name);
            free(effect_name);
            if (operation_id < 0 || effect_id < 0) {
                ppice_v1_set_error(
                    error_buf, error_buf_size,
                    "indexed-checker effect record escapes its source "
                    "operation or effect algebra");
                goto done;
            }
            if ((uint32_t)result.effect_by_operation[operation_id] !=
                    PPINDEXED_CHECKER_EFFECT_V1_COUNT) {
                ppice_v1_set_error(
                    error_buf, error_buf_size,
                    "source operation selects multiple checker effects");
                goto done;
            }
            rows[index].effect =
                (PPIndexedCheckerEffectV1Kind)effect_id;
            result.effect_by_operation[operation_id] =
                rows[index].effect;
        } else if (ppice_v1_expr_head(
                       record, "indexed-checker-policy-v1", 2u)) {
            char *policy_name =
                ppice_v1_render(record->expr.elems[1]);
            int32_t policy_id =
                policy_name
                    ? ppice_v1_policy_find(policy_name)
                    : -1;
            bool value;
            bool *field;

            free(policy_name);
            if (policy_id < 0 ||
                !ppice_v1_policy_value(
                    record->expr.elems[2], &value) ||
                policy_seen[(uint32_t)policy_id] ||
                !(field = ppice_v1_policy_field(
                    &result.policy, (uint32_t)policy_id))) {
                ppice_v1_set_error(
                    error_buf, error_buf_size,
                    "indexed-checker policy record escapes its source "
                    "policy algebra or repeats a key");
                goto done;
            }
            *field = value;
            policy_seen[(uint32_t)policy_id] = true;
        } else {
            ppice_v1_set_error(
                error_buf, error_buf_size,
                "indexed-checker plan record %u has an unknown shape",
                index);
            goto done;
        }
    }
    for (index = 0u; index < result.operation_len; index++) {
        if ((uint32_t)result.effect_by_operation[index] ==
                PPINDEXED_CHECKER_EFFECT_V1_COUNT) {
            ppice_v1_set_error(
                error_buf, error_buf_size,
                "source operation has no indexed-checker effect");
            goto done;
        }
    }
    for (index = 0u;
         index < PPINDEXED_CHECKER_POLICY_V1_COUNT;
         index++) {
        if (!policy_seen[index]) {
            ppice_v1_set_error(
                error_buf, error_buf_size,
                "source checker has no value for policy %s",
                ppice_v1_policy_names[index]);
            goto done;
        }
    }
    memcpy(
        result.occurrence_fold_plan_digest,
        occurrence_plan->plan_digest,
        sizeof(result.occurrence_fold_plan_digest));
    memcpy(
        result.compiler_plan_digest,
        compiler_plan_digest,
        sizeof(result.compiler_plan_digest));
    if (!ppice_v1_plan_digest(&result, result.plan_digest) ||
        !ppindexed_checker_effect_v1_plan_validate(
            program, occurrence_plan, &result,
            error_buf, error_buf_size)) {
        goto done;
    }
    ppindexed_checker_effect_v1_plan_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    if (!ok && error_buf && error_buf_size > 0u &&
        error_buf[0] == '\0') {
        ppice_v1_set_error(
            error_buf, error_buf_size,
            "failed to build indexed-checker effect plan");
    }
    if (rows) {
        for (index = 0u;
             index < (uint32_t)record_len;
             index++) {
            free(rows[index].operation);
        }
    }
    free(rows);
    ppindexed_checker_effect_v1_plan_free(&result);
    return ok;
}

typedef struct {
    FILE *output;
    const char *prefix;
    char *error_buf;
    size_t error_buf_size;
    bool failed;
} PPIndexedCheckerEffectV1CEmitter;

static void ppice_v1_c_error(
    PPIndexedCheckerEffectV1CEmitter *emitter,
    const char *format,
    ...) {
    va_list arguments;

    if (!emitter || emitter->failed)
        return;
    emitter->failed = true;
    if (!emitter->error_buf || emitter->error_buf_size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(
        emitter->error_buf, emitter->error_buf_size,
        format, arguments);
    va_end(arguments);
}

static void ppice_v1_c_write(
    PPIndexedCheckerEffectV1CEmitter *emitter,
    const char *format,
    ...) {
    va_list arguments;

    if (!emitter || emitter->failed)
        return;
    va_start(arguments, format);
    if (vfprintf(emitter->output, format, arguments) < 0) {
        ppice_v1_c_error(
            emitter, "failed to write generated checker-effect C");
    }
    va_end(arguments);
}

static bool ppice_v1_c_identifier_valid(const char *identifier) {
    size_t index;

    if (!identifier || !identifier[0] ||
        !(identifier[0] == '_' ||
          isalpha((unsigned char)identifier[0]))) {
        return false;
    }
    for (index = 1u; identifier[index]; index++) {
        if (!(identifier[index] == '_' ||
              isalnum((unsigned char)identifier[index]))) {
            return false;
        }
    }
    return true;
}

static void ppice_v1_c_string(
    PPIndexedCheckerEffectV1CEmitter *emitter,
    const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;

    ppice_v1_c_write(emitter, "\"");
    while (cursor && *cursor) {
        unsigned char ch = *cursor++;
        if (ch == '\\' || ch == '"') {
            ppice_v1_c_write(emitter, "\\%c", (int)ch);
        } else if (ch >= 0x20u && ch <= 0x7eu) {
            ppice_v1_c_write(emitter, "%c", (int)ch);
        } else {
            ppice_v1_c_write(
                emitter, "\\%03o", (unsigned int)ch);
        }
    }
    ppice_v1_c_write(emitter, "\"");
}

bool ppindexed_checker_effect_v1_emit_c(
    const PPGuardedLexCursorV1Program *program,
    const PPOccurrenceFoldV1Plan *occurrence_plan,
    const PPIndexedCheckerEffectV1Plan *plan,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size) {
    PPIndexedCheckerEffectV1CEmitter emitter;
    char validation_error[256] = {0};
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    memset(&emitter, 0, sizeof(emitter));
    emitter.output = output;
    emitter.prefix = identifier_prefix;
    emitter.error_buf = error_buf;
    emitter.error_buf_size = error_buf_size;
    if (!program || !occurrence_plan || !plan || !output ||
        !ppice_v1_c_identifier_valid(identifier_prefix)) {
        ppice_v1_c_error(
            &emitter, "bad indexed-checker C-emitter arguments");
        return false;
    }
    if (!ppindexed_checker_effect_v1_plan_validate(
            program, occurrence_plan, plan,
            validation_error, sizeof(validation_error))) {
        ppice_v1_c_error(
            &emitter,
            "cannot emit invalid indexed-checker effect plan: %s",
            validation_error[0]
                ? validation_error : "validation failed");
        return false;
    }
    ppice_v1_c_write(
        &emitter,
        "\n/* Generated from IndexedCheckerEffectPlanV1. */\n"
        "#include \"indexed_checker_effect_v1.h\"\n\n"
        "static const PPIndexedCheckerEffectV1Kind "
        "%s_indexed_checker_effects[%u] = {\n",
        identifier_prefix, plan->operation_len);
    for (index = 0u; index < plan->operation_len; index++) {
        ppice_v1_c_write(
            &emitter,
            "    (PPIndexedCheckerEffectV1Kind)%u%s\n",
            (unsigned int)plan->effect_by_operation[index],
            index + 1u == plan->operation_len ? "" : ",");
    }
    ppice_v1_c_write(
        &emitter,
        "};\n\n"
        "const char *%s_indexed_checker_effect_plan_digest(void) {\n"
        "    return ",
        identifier_prefix);
    ppice_v1_c_string(&emitter, plan->plan_digest);
    ppice_v1_c_write(
        &emitter,
        ";\n}\n\n"
        "bool %s_indexed_checker_effect_plan_init(\n"
        "    const PPGuardedLexCursorV1Program *program,\n"
        "    const PPOccurrenceFoldV1Plan *occurrence_plan,\n"
        "    PPIndexedCheckerEffectV1Plan *out,\n"
        "    char *error_buf, size_t error_buf_size) {\n"
        "    PPIndexedCheckerEffectV1Plan result;\n"
        "    if (error_buf && error_buf_size > 0u)\n"
        "        error_buf[0] = '\\0';\n"
        "    if (!program || !occurrence_plan || !out) return false;\n"
        "    ppindexed_checker_effect_v1_plan_init(&result);\n"
        "    result.effect_by_operation = malloc(\n"
        "        sizeof(%s_indexed_checker_effects));\n"
        "    if (!result.effect_by_operation) return false;\n"
        "    memcpy(\n"
        "        result.effect_by_operation,\n"
        "        %s_indexed_checker_effects,\n"
        "        sizeof(%s_indexed_checker_effects));\n"
        "    result.operation_len = UINT32_C(%u);\n"
        "    result.policy.allow_inner_constants = %s;\n"
        "    result.policy.allow_duplicate_floating = %s;\n"
        "    result.policy.allow_toplevel_essential = %s;\n"
        "    result.policy.reject_unknown_steps = %s;\n"
        "    result.policy.skip_completed_sources = %s;\n"
        "    result.policy.reject_active_source_cycles = %s;\n"
        "    memcpy(result.occurrence_fold_plan_digest, ",
        identifier_prefix, identifier_prefix,
        identifier_prefix, identifier_prefix,
        plan->operation_len,
        plan->policy.allow_inner_constants ? "true" : "false",
        plan->policy.allow_duplicate_floating ? "true" : "false",
        plan->policy.allow_toplevel_essential ? "true" : "false",
        plan->policy.reject_unknown_steps ? "true" : "false",
        plan->policy.skip_completed_sources ? "true" : "false",
        plan->policy.reject_active_source_cycles ? "true" : "false");
    ppice_v1_c_string(
        &emitter, plan->occurrence_fold_plan_digest);
    ppice_v1_c_write(
        &emitter,
        ", sizeof(result.occurrence_fold_plan_digest));\n"
        "    memcpy(result.compiler_plan_digest, ");
    ppice_v1_c_string(&emitter, plan->compiler_plan_digest);
    ppice_v1_c_write(
        &emitter,
        ", sizeof(result.compiler_plan_digest));\n"
        "    memcpy(result.plan_digest, ");
    ppice_v1_c_string(&emitter, plan->plan_digest);
    ppice_v1_c_write(
        &emitter,
        ", sizeof(result.plan_digest));\n"
        "    if (!ppindexed_checker_effect_v1_plan_validate(\n"
        "            program, occurrence_plan, &result,\n"
        "            error_buf, error_buf_size)) {\n"
        "        ppindexed_checker_effect_v1_plan_free(&result);\n"
        "        return false;\n"
        "    }\n"
        "    ppindexed_checker_effect_v1_plan_free(out);\n"
        "    *out = result;\n"
        "    return true;\n"
        "}\n");
    if (!emitter.failed && ferror(output)) {
        ppice_v1_c_error(
            &emitter, "generated checker-effect C stream failed");
    }
    return !emitter.failed;
}

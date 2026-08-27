#include "native/json_nik_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned passed;
static unsigned failed;

#define CHECK(condition) do { \
    if (condition) { \
        ++passed; \
    } else { \
        ++failed; \
        fprintf(stderr, "FAIL %s:%d: %s\n", \
            __FILE__, __LINE__, #condition); \
    } \
} while (0)

typedef struct {
    uint8_t *bytes;
    size_t length;
} FileBytes;

static FileBytes read_file(const char *path) {
    FileBytes result = {0};
    FILE *stream = fopen(path, "rb");
    long length;
    if (!stream)
        return result;
    if (fseek(stream, 0, SEEK_END) != 0 ||
        (length = ftell(stream)) <= 0 ||
        fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return result;
    }
    result.bytes = (uint8_t *)malloc((size_t)length);
    if (!result.bytes ||
        fread(result.bytes, 1u, (size_t)length, stream) != (size_t)length) {
        free(result.bytes);
        result.bytes = NULL;
        fclose(stream);
        return result;
    }
    result.length = (size_t)length;
    fclose(stream);
    return result;
}

static bool duplicate_value_has_exact_occurrences(Atom *value) {
    Atom *members;
    Atom *first;
    Atom *second;
    if (!value || value->kind != ATOM_EXPR || value->expr.len != 2u ||
        !atom_is_symbol(value->expr.elems[0], "JsonObjectV1")) {
        return false;
    }
    members = value->expr.elems[1];
    if (!members || members->kind != ATOM_EXPR || members->expr.len != 2u)
        return false;
    first = members->expr.elems[0];
    second = members->expr.elems[1];
    return first && second &&
        first->kind == ATOM_EXPR && second->kind == ATOM_EXPR &&
        first->expr.len == 5u && second->expr.len == 5u &&
        atom_is_symbol(first->expr.elems[0], "JsonMemberV1") &&
        atom_is_symbol(second->expr.elems[0], "JsonMemberV1") &&
        first->expr.elems[1]->kind == ATOM_GROUNDED &&
        second->expr.elems[1]->kind == ATOM_GROUNDED &&
        first->expr.elems[1]->ground.gkind == GV_INT &&
        second->expr.elems[1]->ground.gkind == GV_INT &&
        first->expr.elems[1]->ground.ival == 0 &&
        second->expr.elems[1]->ground.ival == 1 &&
        first->expr.elems[4]->kind == ATOM_EXPR &&
        second->expr.elems[4]->kind == ATOM_EXPR &&
        first->expr.elems[4]->expr.len == 3u &&
        second->expr.elems[4]->expr.len == 3u &&
        atom_is_symbol(first->expr.elems[4]->expr.elems[0],
                       "JsonSourceSpanV1") &&
        atom_is_symbol(second->expr.elems[4]->expr.elems[0],
                       "JsonSourceSpanV1") &&
        first->expr.elems[4]->expr.elems[1]->kind == ATOM_GROUNDED &&
        first->expr.elems[4]->expr.elems[2]->kind == ATOM_GROUNDED &&
        second->expr.elems[4]->expr.elems[1]->kind == ATOM_GROUNDED &&
        second->expr.elems[4]->expr.elems[2]->kind == ATOM_GROUNDED &&
        first->expr.elems[4]->expr.elems[1]->ground.gkind == GV_INT &&
        first->expr.elems[4]->expr.elems[2]->ground.gkind == GV_INT &&
        second->expr.elems[4]->expr.elems[1]->ground.gkind == GV_INT &&
        second->expr.elems[4]->expr.elems[2]->ground.gkind == GV_INT &&
        first->expr.elems[4]->expr.elems[1]->ground.ival == 1 &&
        first->expr.elems[4]->expr.elems[2]->ground.ival == 6 &&
        second->expr.elems[4]->expr.elems[1]->ground.ival == 7 &&
        second->expr.elems[4]->expr.elems[2]->ground.ival == 12;
}

int main(void) {
    FileBytes language = read_file("langdef/json/rfc8259_syntax_v1.metta");
    FileBytes profile = read_file(
        "langdef/json/rfc8259_parser_profile_v1.metta");
    FileBytes target = read_file(
        "langdef/json/occurrence_preserving_value_v1.metta");
    CettaJsonNikV1Admission admission = {0};
    CettaJsonNikV1LanguageReceipt language_receipt;
    CettaNikHostedNativeReceiptV1 hosted_receipt;
    CettaJsonNikV1Request request;
    CettaJsonRuntimeV1Limits limits;
    CettaJsonRuntimeV1Status prepared_status;
    CettaNikHostedNativeCallKindV1 call;
    Arena arena;
    Arena prepared_arena;
    SymbolTable symbols;
    char error[512] = {0};
    static const uint8_t duplicate[] = "{\"x\":1,\"x\":2}";
    static const uint8_t invalid[] = "[1,]";

    symbol_table_init(&symbols);
    g_symbols = &symbols;
    CHECK(language.bytes && profile.bytes && target.bytes);
    CHECK(cetta_nik_native_calculus_v1_is_valid(
        cetta_json_nik_v1_calculus()));
    if (!language.bytes || !profile.bytes || !target.bytes)
        goto done;

    admission = cetta_json_nik_v1_admit(
        language.bytes, language.length,
        profile.bytes, profile.length,
        target.bytes, target.length,
        error, sizeof(error));
    if (admission.kind != CETTA_NIK_HOST_ADMISSION_ADMITTED_V1)
        fprintf(stderr, "JSON NIK admission error: %s\n",
                error[0] ? error : "unspecified");
    CHECK(admission.kind == CETTA_NIK_HOST_ADMISSION_ADMITTED_V1);
    CHECK(admission.host && cetta_json_nik_v1_is_current(admission.host));
    if (!admission.host)
        goto done;
    CHECK(cetta_json_runtime_v1_table_build_count(
              cetta_json_nik_v1_borrow_selected_runtime(admission.host)) ==
          1u);
    CHECK(cetta_json_nik_v1_production_kernel(admission.host) ==
          CETTA_JSON_KERNEL_V1_PACKED_GLL);

    arena_init(&arena);
    arena_init(&prepared_arena);
    cetta_json_runtime_v1_default_limits(&limits);
    limits.kernel = CETTA_JSON_KERNEL_V1_PACKED_GLL;
    request = (CettaJsonNikV1Request){
        .arena = &arena,
        .json_bytes = duplicate,
        .json_byte_len = sizeof(duplicate) - 1u,
        .limits = &limits,
    };
    cetta_json_nik_v1_language_receipt_init(&language_receipt);
    call = cetta_json_nik_v1_run(
        admission.host, CETTA_JSON_NIK_V1_OPERATION_PARSE,
        &request, &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1 &&
        hosted_receipt.result.kind == CETTA_NIK_RESULT_OUTCOME &&
        hosted_receipt.result.value.outcome == CETTA_NIK_OUTCOME_ESTABLISHED &&
        hosted_receipt.operation_identity ==
            CETTA_JSON_NIK_V1_OPERATION_PARSE &&
        language_receipt.status == CETTA_JSON_RUNTIME_V1_OK &&
        language_receipt.kernel ==
            CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL &&
        duplicate_value_has_exact_occurrences(language_receipt.value));

    {
        Atom *prepared_value = NULL;
        CHECK(cetta_json_nik_v1_borrow_selected_runtime(admission.host) !=
            NULL);
        CHECK(cetta_json_nik_v1_parse_prepared(
                admission.host, &prepared_arena,
                duplicate, sizeof(duplicate) - 1u, NULL,
                &prepared_value, &prepared_status,
                error, sizeof(error)) &&
            prepared_status == CETTA_JSON_RUNTIME_V1_OK &&
            atom_eq(prepared_value, language_receipt.value));
        CHECK(cetta_json_runtime_v1_table_build_count(
                  cetta_json_nik_v1_borrow_selected_runtime(
                      admission.host)) == 1u);
    }

    request.json_bytes = invalid;
    request.json_byte_len = sizeof(invalid) - 1u;
    call = cetta_json_nik_v1_run(
        admission.host, CETTA_JSON_NIK_V1_OPERATION_PARSE,
        &request, &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1 &&
        hosted_receipt.result.value.outcome == CETTA_NIK_OUTCOME_REFUTED &&
        language_receipt.status == CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED &&
        language_receipt.value == NULL);

    request.json_bytes = duplicate;
    request.json_byte_len = sizeof(duplicate) - 1u;
    limits.recognizer_work_limit = 1u;
    call = cetta_json_nik_v1_run(
        admission.host, CETTA_JSON_NIK_V1_OPERATION_PARSE,
        &request, &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_RESULT_V1 &&
        hosted_receipt.result.value.outcome == CETTA_NIK_OUTCOME_INCOMPLETE &&
        language_receipt.status == CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT);

    CettaJsonRuntimeV1Status prior_status = language_receipt.status;
    call = cetta_json_nik_v1_run(
        admission.host, UINT64_C(9999),
        &request, &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_OUTSIDE_CALCULUS_V1 &&
        language_receipt.status == prior_status);

    uint8_t language_first = language.bytes[0];
    language.bytes[0] ^= UINT8_C(1);
    CHECK(!cetta_json_nik_v1_is_current(admission.host));
    call = cetta_json_nik_v1_run(
        admission.host, CETTA_JSON_NIK_V1_OPERATION_PARSE,
        &request, &language_receipt, &hosted_receipt);
    CHECK(call == CETTA_NIK_HOSTED_NATIVE_CALL_STALE_V1 &&
        language_receipt.status == prior_status);
    language.bytes[0] = language_first;
    CHECK(cetta_json_nik_v1_is_current(admission.host));

    uint8_t profile_first = profile.bytes[0];
    profile.bytes[0] ^= UINT8_C(1);
    CHECK(!cetta_json_nik_v1_is_current(admission.host));
    profile.bytes[0] = profile_first;
    CHECK(cetta_json_nik_v1_is_current(admission.host));

    uint8_t target_first = target.bytes[0];
    target.bytes[0] ^= UINT8_C(1);
    CHECK(!cetta_json_nik_v1_is_current(admission.host));
    target.bytes[0] = target_first;
    CHECK(cetta_json_nik_v1_is_current(admission.host));

    arena_free(&arena);
    arena_free(&prepared_arena);

done:
    cetta_json_nik_v1_destroy(admission.host);
    free(target.bytes);
    free(profile.bytes);
    free(language.bytes);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("JSON NIK v1: %u passed, %u failed\n", passed, failed);
    return failed == 0u ? 0 : 1;
}

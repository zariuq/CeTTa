#include "finite_horn_gslt_v1.h"
#include "native_sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR_CAP 1024u

static const char CORE[] =
    "; layout is not authority\n"
    "(gslt-presentation-v1 Core\n"
    " (rewrites (rule r (head (p ?x)) (body)))\n"
    " (equations)\n"
    " (signature (operator q 2) (operator dormant 3) (operator p 01)))";

static const char LEAF[] =
    "(gslt-presentation-v1 Leaf "
    "(signature) (equations) "
    "(rewrites (rule s (head (q left right)) (body (p \"λ😀\")))))";

static const char CORE_CANONICAL[] =
    "(gslt-presentation-v1 Core (signature (operator dormant 3) "
    "(operator p 1) (operator q 2)) "
    "(equations) (rewrites (rule r (head (p ?x)) (body))))\n";

static const char CORE_QUOTED[] =
    "(q-cons (q-rule (q-sym r) "
    "(q-app (q-sym p) (q-cons (q-var q-zero) q-nil)) q-nil) q-nil)";

static const char FRAMED[] =
    "(gslt-presentation-v1 FramedV1 "
    "(nik-authority-frame-v1 "
    "(mode direct-decision) (certificate-policy none) "
    "(fiber sample-language) "
    "(outcome-algebra (Accepted Rejected) (exclusive) (default Rejected)) "
    "(native-projection pending) (status AUTHORED_FRAGMENT) "
    "(commitments direct-computation certificate-free)) "
    "(signature (operator probe 1)) (equations) "
    "(rewrites (rule probe-a "
    "(nik-rule-frame-v1 (native-projection native-probe) (role calculus)) "
    "(head (probe a)) (body))))";

static bool load_strings(const char *const *texts,
                         const char *const *sources,
                         size_t count,
                         FHGSLTPackage **out,
                         char error[ERROR_CAP]) {
    FHGSLTInput *inputs = (FHGSLTInput *)calloc(count, sizeof(*inputs));
    if (inputs == NULL) {
        (void)snprintf(error, ERROR_CAP, "test allocation failed");
        return false;
    }
    for (size_t index = 0u; index < count; index++) {
        inputs[index].bytes = (const uint8_t *)texts[index];
        inputs[index].len = strlen(texts[index]);
        inputs[index].source = sources[index];
    }
    bool ok = fhgslt_package_from_inputs(inputs,
                                         count,
                                         out,
                                         error,
                                         ERROR_CAP);
    free(inputs);
    return ok;
}

static bool expect_rejected(const char *text, const char *label) {
    const char *texts[] = {text};
    const char *sources[] = {label};
    FHGSLTPackage *package = NULL;
    char error[ERROR_CAP] = {0};
    if (load_strings(texts, sources, 1u, &package, error)) {
        fhgslt_package_free(package);
        fprintf(stderr, "%s: unexpectedly admitted\n", label);
        return false;
    }
    if (error[0] == '\0') {
        fprintf(stderr, "%s: rejected without a diagnostic\n", label);
        return false;
    }
    return true;
}

static bool expect_inputs_rejected(const char *const *texts,
                                   const char *const *sources,
                                   size_t count,
                                   const char *label) {
    FHGSLTPackage *package = NULL;
    char error[ERROR_CAP] = {0};
    if (load_strings(texts, sources, count, &package, error)) {
        fhgslt_package_free(package);
        fprintf(stderr, "%s: unexpectedly admitted\n", label);
        return false;
    }
    if (error[0] == '\0') {
        fprintf(stderr, "%s: rejected without a diagnostic\n", label);
        return false;
    }
    return true;
}

static bool digest_for_strings(const char *const *texts,
                               const char *const *sources,
                               size_t count,
                               char digest[65]) {
    FHGSLTPackage *package = NULL;
    char error[ERROR_CAP] = {0};
    if (!load_strings(texts, sources, count, &package, error)) {
        fprintf(stderr, "admission failed: %s\n", error);
        return false;
    }
    bool ok = fhgslt_package_digest(package, digest, error, sizeof(error));
    if (!ok)
        fprintf(stderr, "digest failed: %s\n", error);
    fhgslt_package_free(package);
    return ok;
}

static size_t count_bytes(const uint8_t *bytes,
                          size_t len,
                          const char *needle) {
    size_t needle_len = strlen(needle);
    size_t count = 0u;
    if (needle_len == 0u || needle_len > len)
        return 0u;
    for (size_t offset = 0u; offset + needle_len <= len; offset++) {
        if (memcmp(bytes + offset, needle, needle_len) == 0)
            count++;
    }
    return count;
}

static bool local_canaries(size_t *passed) {
    char error[ERROR_CAP] = {0};
    const char *texts[] = {CORE, LEAF};
    const char *sources[] = {"core", "leaf"};
    FHGSLTPackage *package = NULL;
    if (!load_strings(texts, sources, 2u, &package, error)) {
        fprintf(stderr, "positive package failed: %s\n", error);
        return false;
    }
    if (fhgslt_package_presentation_count(package) != 2u ||
        fhgslt_package_operator_count(package) != 3u ||
        fhgslt_package_rule_count(package) != 2u) {
        fprintf(stderr, "positive package counts changed\n");
        fhgslt_package_free(package);
        return false;
    }
    (*passed)++;

    uint8_t *quoted = NULL;
    size_t quoted_len = 0u;
    if (!fhgslt_package_quoted_rules(package,
                                     0u,
                                     &quoted,
                                     &quoted_len,
                                     error,
                                     sizeof(error)) ||
        quoted_len != strlen(CORE_QUOTED) ||
        memcmp(quoted, CORE_QUOTED, quoted_len) != 0) {
        fprintf(stderr, "ground rule quotation changed: %s\n", error);
        free(quoted);
        fhgslt_package_free(package);
        return false;
    }
    (*passed)++;

    const char alpha_core[] =
        "(gslt-presentation-v1 Core "
        "(signature (operator p 1) (operator q 2)) "
        "(equations) (rewrites (rule r (head (p ?renamed)) (body))))";
    const char *alpha_texts[] = {alpha_core};
    const char *alpha_sources[] = {"alpha-core"};
    FHGSLTPackage *alpha_package = NULL;
    uint8_t *alpha_quoted = NULL;
    size_t alpha_quoted_len = 0u;
    if (!load_strings(alpha_texts,
                      alpha_sources,
                      1u,
                      &alpha_package,
                      error) ||
        !fhgslt_package_quoted_rules(alpha_package,
                                     0u,
                                     &alpha_quoted,
                                     &alpha_quoted_len,
                                     error,
                                     sizeof(error)) ||
        alpha_quoted_len != quoted_len ||
        memcmp(alpha_quoted, quoted, quoted_len) != 0) {
        fprintf(stderr, "ground rule quotation is not alpha-stable: %s\n", error);
        free(alpha_quoted);
        free(quoted);
        fhgslt_package_free(alpha_package);
        fhgslt_package_free(package);
        return false;
    }
    free(alpha_quoted);
    free(quoted);
    fhgslt_package_free(alpha_package);
    (*passed)++;

    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    if (!fhgslt_package_canonical_presentation(package,
                                               0u,
                                               &canonical,
                                               &canonical_len,
                                               error,
                                               sizeof(error)) ||
        canonical_len != strlen(CORE_CANONICAL) ||
        memcmp(canonical, CORE_CANONICAL, canonical_len) != 0) {
        fprintf(stderr, "canonical bytes changed: %s\n", error);
        free(canonical);
        fhgslt_package_free(package);
        return false;
    }
    free(canonical);
    (*passed)++;

    uint8_t *roundtrip_bytes[2] = {NULL, NULL};
    size_t roundtrip_lens[2] = {0u, 0u};
    FHGSLTInput roundtrip_inputs[2];
    memset(roundtrip_inputs, 0, sizeof(roundtrip_inputs));
    for (size_t index = 0u; index < 2u; index++) {
        if (!fhgslt_package_canonical_presentation(package,
                                                   index,
                                                   &roundtrip_bytes[index],
                                                   &roundtrip_lens[index],
                                                   error,
                                                   sizeof(error))) {
            fprintf(stderr, "roundtrip export failed: %s\n", error);
            for (size_t free_index = 0u; free_index < 2u; free_index++)
                free(roundtrip_bytes[free_index]);
            fhgslt_package_free(package);
            return false;
        }
        roundtrip_inputs[index].bytes = roundtrip_bytes[index];
        roundtrip_inputs[index].len = roundtrip_lens[index];
        roundtrip_inputs[index].source = "roundtrip";
    }
    char original_digest[65];
    if (!fhgslt_package_digest(package,
                               original_digest,
                               error,
                               sizeof(error))) {
        fprintf(stderr, "original digest failed: %s\n", error);
        for (size_t index = 0u; index < 2u; index++)
            free(roundtrip_bytes[index]);
        fhgslt_package_free(package);
        return false;
    }
    FHGSLTPackage *roundtrip = NULL;
    if (!fhgslt_package_from_inputs(roundtrip_inputs,
                                    2u,
                                    &roundtrip,
                                    error,
                                    sizeof(error))) {
        fprintf(stderr, "roundtrip admission failed: %s\n", error);
        for (size_t index = 0u; index < 2u; index++)
            free(roundtrip_bytes[index]);
        fhgslt_package_free(package);
        return false;
    }
    char roundtrip_digest[65];
    if (!fhgslt_package_digest(roundtrip,
                               roundtrip_digest,
                               error,
                               sizeof(error)) ||
        strcmp(original_digest, roundtrip_digest) != 0) {
        fprintf(stderr, "roundtrip digest changed: %s\n", error);
        for (size_t index = 0u; index < 2u; index++)
            free(roundtrip_bytes[index]);
        fhgslt_package_free(roundtrip);
        fhgslt_package_free(package);
        return false;
    }
    for (size_t index = 0u; index < 2u; index++)
        free(roundtrip_bytes[index]);
    fhgslt_package_free(roundtrip);
    (*passed)++;

    const char *reversed_texts[] = {LEAF, CORE};
    const char *reversed_sources[] = {"leaf", "core"};
    char reversed_digest[65];
    if (!digest_for_strings(reversed_texts,
                            reversed_sources,
                            2u,
                            reversed_digest) ||
        strcmp(original_digest, reversed_digest) != 0) {
        fprintf(stderr, "component order changed package digest\n");
        fhgslt_package_free(package);
        return false;
    }
    (*passed)++;

    uint8_t *reflected = NULL;
    size_t reflected_len = 0u;
    static const char dormant_fact[] =
        "(source-operator (q-sym Core) (q-sym dormant) "
        "(q-succ (q-succ (q-succ q-zero))))";
    if (!fhgslt_package_reflected_presentation(
            package, &reflected, &reflected_len, error, sizeof(error)) ||
        count_bytes(reflected, reflected_len,
                    "(head (source-operator ") != 3u ||
        count_bytes(reflected, reflected_len,
                    "(head (source-rule ") != 2u ||
        count_bytes(reflected, reflected_len, dormant_fact) != 1u) {
        fprintf(stderr, "declaration-complete reflection changed: %s\n", error);
        free(reflected);
        fhgslt_package_free(package);
        return false;
    }
    free(reflected);
    (*passed)++;

    const char core_without_rule[] =
        "(gslt-presentation-v1 Core "
        "(signature (operator p 1) (operator q 2)) "
        "(equations) (rewrites))";
    const char *deleted_texts[] = {core_without_rule, LEAF};
    const char *deleted_sources[] = {"deleted", "leaf"};
    char deleted_digest[65];
    if (!digest_for_strings(deleted_texts,
                            deleted_sources,
                            2u,
                            deleted_digest) ||
        strcmp(original_digest, deleted_digest) == 0) {
        fprintf(stderr, "rule deletion did not change package digest\n");
        fhgslt_package_free(package);
        return false;
    }
    (*passed)++;
    fhgslt_package_free(package);

    const char raw_unicode[] =
        "(gslt-presentation-v1 Unicode (signature (operator p 1)) "
        "(equations) (rewrites (rule r (head (p \"λ😀\")) (body))))";
    const char escaped_unicode[] =
        "(gslt-presentation-v1 Unicode (signature (operator p 1)) "
        "(equations) (rewrites "
        "(rule r (head (p \"\\u03bb\\ud83d\\ude00\")) (body))))";
    const char *raw_texts[] = {raw_unicode};
    const char *raw_sources[] = {"raw-unicode"};
    const char *escaped_texts[] = {escaped_unicode};
    const char *escaped_sources[] = {"escaped-unicode"};
    char raw_digest[65];
    char escaped_digest[65];
    if (!digest_for_strings(raw_texts, raw_sources, 1u, raw_digest) ||
        !digest_for_strings(escaped_texts,
                            escaped_sources,
                            1u,
                            escaped_digest) ||
        strcmp(raw_digest, escaped_digest) != 0) {
        fprintf(stderr, "Unicode spellings did not canonicalize identically\n");
        return false;
    }
    (*passed)++;

    const char *framed_texts[] = {FRAMED};
    const char *framed_sources[] = {"framed"};
    FHGSLTPackage *framed_package = NULL;
    error[0] = '\0';
    if (!load_strings(framed_texts,
                      framed_sources,
                      1u,
                      &framed_package,
                      error) ||
        fhgslt_package_presentation_count(framed_package) != 1u ||
        fhgslt_package_rule_count(framed_package) != 1u) {
        fprintf(stderr, "framed authority failed admission: %s\n", error);
        fhgslt_package_free(framed_package);
        return false;
    }
    uint8_t *framed_canonical = NULL;
    size_t framed_canonical_len = 0u;
    if (!fhgslt_package_canonical_presentation(framed_package,
                                               0u,
                                               &framed_canonical,
                                               &framed_canonical_len,
                                               error,
                                               sizeof(error)) ||
        count_bytes(framed_canonical,
                    framed_canonical_len,
                    "(fiber sample-language)") != 1u ||
        count_bytes(framed_canonical,
                    framed_canonical_len,
                    "(role calculus)") != 1u) {
        fprintf(stderr, "framed authority metadata was not canonical: %s\n", error);
        free(framed_canonical);
        fhgslt_package_free(framed_package);
        return false;
    }
    free(framed_canonical);
    (*passed)++;

    uint8_t *framed_quoted = NULL;
    size_t framed_quoted_len = 0u;
    if (!fhgslt_package_quoted_rules(framed_package,
                                     0u,
                                     &framed_quoted,
                                     &framed_quoted_len,
                                     error,
                                     sizeof(error)) ||
        count_bytes(framed_quoted, framed_quoted_len, "nik-") != 0u) {
        fprintf(stderr, "authority metadata leaked into executable rules: %s\n", error);
        free(framed_quoted);
        fhgslt_package_free(framed_package);
        return false;
    }
    free(framed_quoted);
    fhgslt_package_free(framed_package);
    (*passed)++;

    struct Rejection {
        const char *label;
        const char *text;
    } rejections[] = {
        {"wrong-arity",
         "(gslt-presentation-v1 Bad (signature (operator p 2)) "
         "(equations) (rewrites (rule r (head (p x)) (body))))"},
        {"unknown-field",
         "(gslt-presentation-v1 Bad (signature) (equations) (rewrites) "
         "(parser-options))"},
        {"authored-equation",
         "(gslt-presentation-v1 Bad (signature) "
         "(equations (equation e a b)) (rewrites))"},
        {"duplicate-operator",
         "(gslt-presentation-v1 Bad "
         "(signature (operator p 1) (operator p 1)) "
         "(equations) (rewrites))"},
        {"duplicate-rule",
         "(gslt-presentation-v1 Bad (signature (operator p 1)) "
         "(equations) (rewrites "
         "(rule r (head (p a)) (body)) "
         "(rule r (head (p b)) (body))))"},
        {"anonymous-variable",
         "(gslt-presentation-v1 Bad (signature (operator p 1)) "
         "(equations) (rewrites (rule r (head (p ?_)) (body))))"},
        {"isolated-surrogate",
         "(gslt-presentation-v1 Bad (signature (operator p 1)) "
         "(equations) (rewrites "
         "(rule r (head (p \"\\ud800\")) (body))))"},
        {"missing-field",
         "(gslt-presentation-v1 Bad (signature) (equations))"},
        {"primitive-head",
         "(gslt-presentation-v1 Bad (signature) (equations) "
         "(rewrites (rule r (head atom) (body))))"},
        {"quoted-symbol-fragment",
         "(gslt-presentation-v1 Bad (signature (operator p 1)) "
         "(equations) (rewrites "
         "(rule r (head (p abc\"def\")) (body))))"},
        {"framed-direct-certificate",
         "(gslt-presentation-v1 Bad "
         "(nik-authority-frame-v1 (mode direct-decision) "
         "(certificate-policy trace) (fiber sample-language) "
         "(outcome-algebra (Accepted Rejected) (exclusive) "
         "(default Rejected)) (native-projection pending) "
         "(status AUTHORED_FRAGMENT) (commitments)) "
         "(signature) (equations) (rewrites))"},
        {"framed-missing-fiber",
         "(gslt-presentation-v1 Bad "
         "(nik-authority-frame-v1 (mode direct-decision) "
         "(certificate-policy none) "
         "(outcome-algebra (Accepted Rejected) (exclusive) "
         "(default Rejected)) (native-projection pending) "
         "(status AUTHORED_FRAGMENT) (commitments)) "
         "(signature) (equations) (rewrites))"},
        {"framed-default-outside-algebra",
         "(gslt-presentation-v1 Bad "
         "(nik-authority-frame-v1 (mode direct-decision) "
         "(certificate-policy none) (fiber sample-language) "
         "(outcome-algebra (Accepted Rejected) (exclusive) "
         "(default Missing)) (native-projection pending) "
         "(status AUTHORED_FRAGMENT) (commitments)) "
         "(signature) (equations) (rewrites))"},
        {"framed-missing-rule-frame",
         "(gslt-presentation-v1 Bad "
         "(nik-authority-frame-v1 (mode direct-decision) "
         "(certificate-policy none) (fiber sample-language) "
         "(outcome-algebra (Accepted Rejected) (exclusive) "
         "(default Rejected)) (native-projection pending) "
         "(status AUTHORED_FRAGMENT) (commitments)) "
         "(signature (operator probe 1)) (equations) "
         "(rewrites (rule probe-a (head (probe a)) (body))))"},
        {"framed-unsupported-rule-role",
         "(gslt-presentation-v1 Bad "
         "(nik-authority-frame-v1 (mode direct-decision) "
         "(certificate-policy none) (fiber sample-language) "
         "(outcome-algebra (Accepted Rejected) (exclusive) "
         "(default Rejected)) (native-projection pending) "
         "(status AUTHORED_FRAGMENT) (commitments)) "
         "(signature (operator probe 1)) (equations) "
         "(rewrites (rule probe-a (nik-rule-frame-v1 "
         "(native-projection native-probe) (role observation)) "
         "(head (probe a)) (body))))"},
    };
    for (size_t index = 0u;
         index < sizeof(rejections) / sizeof(rejections[0]);
         index++) {
        if (!expect_rejected(rejections[index].text, rejections[index].label))
            return false;
        (*passed)++;
    }

    static const uint8_t malformed_utf8[] = {
        '(', 'g', 's', 'l', 't', '-', 'p', 'r', 'e', 's', 'e', 'n', 't',
        'a', 't', 'i', 'o', 'n', '-', 'v', '1', ' ', 'B', 'a', 'd', ' ',
        '(', 's', 'i', 'g', 'n', 'a', 't', 'u', 'r', 'e', ')', ' ',
        '(', 'e', 'q', 'u', 'a', 't', 'i', 'o', 'n', 's', ')', ' ',
        '(', 'r', 'e', 'w', 'r', 'i', 't', 'e', 's', ')', ' ',
        0xc0u, 0xafu, ')'
    };
    FHGSLTInput malformed = {
        .bytes = malformed_utf8,
        .len = sizeof(malformed_utf8),
        .source = "malformed-utf8",
    };
    FHGSLTPackage *bad_package = NULL;
    error[0] = '\0';
    if (fhgslt_package_from_inputs(&malformed,
                                   1u,
                                   &bad_package,
                                   error,
                                   sizeof(error)) ||
        error[0] == '\0') {
        fhgslt_package_free(bad_package);
        fprintf(stderr, "malformed UTF-8 was not rejected with a diagnostic\n");
        return false;
    }
    (*passed)++;

    const char duplicate_name_a[] =
        "(gslt-presentation-v1 Same (signature) (equations) (rewrites))";
    const char duplicate_name_b[] =
        "(gslt-presentation-v1 Same (signature) (equations) (rewrites))";
    const char *duplicate_names[] = {duplicate_name_a, duplicate_name_b};
    const char *duplicate_sources[] = {"duplicate-a", "duplicate-b"};
    if (!expect_inputs_rejected(duplicate_names,
                                duplicate_sources,
                                2u,
                                "duplicate-presentation"))
        return false;
    (*passed)++;

    const char duplicate_rule_a[] =
        "(gslt-presentation-v1 A (signature (operator p 1)) "
        "(equations) (rewrites (rule same (head (p a)) (body))))";
    const char duplicate_rule_b[] =
        "(gslt-presentation-v1 B (signature) "
        "(equations) (rewrites (rule same (head (p b)) (body))))";
    const char *duplicate_rules[] = {duplicate_rule_a, duplicate_rule_b};
    if (!expect_inputs_rejected(duplicate_rules,
                                duplicate_sources,
                                2u,
                                "duplicate-composed-rule"))
        return false;
    (*passed)++;

    bad_package = NULL;
    error[0] = '\0';
    if (fhgslt_package_from_inputs(NULL,
                                   0u,
                                   &bad_package,
                                   error,
                                   sizeof(error)) ||
        error[0] == '\0') {
        fprintf(stderr, "empty package was not rejected with a diagnostic\n");
        return false;
    }
    (*passed)++;
    return true;
}

static bool external_package(int argc, char **argv, size_t *passed) {
    if (argc == 1)
        return true;
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s [EXPECTED-DIGEST "
                "[--quote-index INDEX EXPECTED-QUOTE-SHA256] "
                "PRESENTATION...]\n",
                argv[0]);
        return false;
    }
    const char *expected = argv[1];
    size_t path_index = 2u;
    size_t quote_index = SIZE_MAX;
    const char *expected_quote = NULL;
    if (argc > 2 && strcmp(argv[2], "--quote-index") == 0) {
        if (argc < 6) {
            fprintf(stderr, "quote-index mode requires an index, hash, and paths\n");
            return false;
        }
        char *end = NULL;
        unsigned long long parsed = strtoull(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || parsed > SIZE_MAX) {
            fprintf(stderr, "invalid quote presentation index: %s\n", argv[3]);
            return false;
        }
        quote_index = (size_t)parsed;
        expected_quote = argv[4];
        path_index = 5u;
    }
    FHGSLTPackage *package = NULL;
    char error[ERROR_CAP] = {0};
    if (!fhgslt_package_from_paths((const char *const *)(argv + path_index),
                                   (size_t)argc - path_index,
                                   &package,
                                   error,
                                   sizeof(error))) {
        fprintf(stderr, "external package admission failed: %s\n", error);
        return false;
    }
    char digest[65];
    if (!fhgslt_package_digest(package, digest, error, sizeof(error)) ||
        strcmp(digest, expected) != 0) {
        fprintf(stderr,
                "external package digest mismatch: expected %s, got %s (%s)\n",
                expected,
                digest,
                error);
        fhgslt_package_free(package);
        return false;
    }

    if (expected_quote != NULL) {
        uint8_t *quoted = NULL;
        size_t quoted_len = 0u;
        char quoted_digest[65];
        if (!fhgslt_package_quoted_rules(package,
                                         quote_index,
                                         &quoted,
                                         &quoted_len,
                                         error,
                                         sizeof(error))) {
            fprintf(stderr, "external rule quotation failed: %s\n", error);
            fhgslt_package_free(package);
            return false;
        }
        cetta_native_sha256_hex(quoted, quoted_len, quoted_digest);
        free(quoted);
        if (strcmp(quoted_digest, expected_quote) != 0) {
            fprintf(stderr,
                    "external quotation digest mismatch: expected %s, got %s\n",
                    expected_quote,
                    quoted_digest);
            fhgslt_package_free(package);
            return false;
        }
    }

    size_t count = fhgslt_package_presentation_count(package);
    uint8_t **canonical = (uint8_t **)calloc(count, sizeof(*canonical));
    FHGSLTInput *inputs = (FHGSLTInput *)calloc(count, sizeof(*inputs));
    if (canonical == NULL || inputs == NULL) {
        fprintf(stderr, "external roundtrip allocation failed\n");
        free(canonical);
        free(inputs);
        fhgslt_package_free(package);
        return false;
    }
    bool ok = true;
    for (size_t index = 0u; index < count; index++) {
        if (!fhgslt_package_canonical_presentation(package,
                                                   index,
                                                   &canonical[index],
                                                   &inputs[index].len,
                                                   error,
                                                   sizeof(error))) {
            fprintf(stderr, "external canonicalization failed: %s\n", error);
            ok = false;
            break;
        }
        inputs[index].bytes = canonical[index];
        inputs[index].source = "external-roundtrip";
    }
    FHGSLTPackage *roundtrip = NULL;
    if (ok && !fhgslt_package_from_inputs(inputs,
                                          count,
                                          &roundtrip,
                                          error,
                                          sizeof(error))) {
        fprintf(stderr, "external roundtrip admission failed: %s\n", error);
        ok = false;
    }
    char roundtrip_digest[65] = {0};
    if (ok &&
        (!fhgslt_package_digest(roundtrip,
                                roundtrip_digest,
                                error,
                                sizeof(error)) ||
         strcmp(roundtrip_digest, expected) != 0)) {
        fprintf(stderr, "external roundtrip digest changed: %s\n", error);
        ok = false;
    }
    for (size_t index = 0u; index < count; index++)
        free(canonical[index]);
    free(canonical);
    free(inputs);
    fhgslt_package_free(roundtrip);
    fhgslt_package_free(package);
    if (ok)
        (*passed)++;
    return ok;
}

int main(int argc, char **argv) {
    size_t passed = 0u;
    if (!local_canaries(&passed) || !external_package(argc, argv, &passed))
        return 1;
    size_t expected = argc == 1 ? 30u : 31u;
    if (passed != expected) {
        fprintf(stderr,
                "canary accounting mismatch: expected %zu, got %zu\n",
                expected,
                passed);
        return 1;
    }
    printf("(FiniteHornGSLTV1NativeCanarySummary %zu %zu 0)\n",
           expected,
           passed);
    return 0;
}

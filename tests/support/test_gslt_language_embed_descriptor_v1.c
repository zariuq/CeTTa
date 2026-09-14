#include "gslt_language_runtime.h"
#include "native_sha256.h"
#include <stdio.h>
#include <string.h>

extern const CettaGsltEmbeddedLanguageV1 CANDIDATE_DESCRIPTOR;
extern const CettaGsltEmbeddedLanguageV1 REFERENCE_DESCRIPTOR;

static bool text(const char *a, const char *b) {
    return a && b ? !strcmp(a, b) : a == b;
}

static bool source(const CettaGsltEmbeddedSourceV1 *a,
                   const CettaGsltEmbeddedSourceV1 *b) {
    char sha[65];
    cetta_native_sha256_hex(a->input.bytes, a->input.length, sha);
    return text(a->input.source, b->input.source) && text(a->sha256, b->sha256) &&
        text(sha, a->sha256) && a->input.length == b->input.length &&
        !memcmp(a->input.bytes, b->input.bytes, a->input.length);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const CettaGsltEmbeddedLanguageV1 *a = &CANDIDATE_DESCRIPTOR;
    const CettaGsltEmbeddedLanguageV1 *b = &REFERENCE_DESCRIPTOR;
    bool ok = true;
#define TEXT(field) do { if (!text(a->field, b->field)) { \
    fprintf(stderr, "mismatch: " #field "\n"); ok = false; } } while (0)
    TEXT(name); TEXT(profile_name); TEXT(syntax_backend); TEXT(term_abi);
    TEXT(program_nil); TEXT(program_cons); TEXT(entry_relation);
    TEXT(query_relation); TEXT(observation); TEXT(manifest_sha256);
#undef TEXT
    ok = ok && text(a->compiler_sha256, argv[1]) && source(&a->manifest, &b->manifest) &&
        a->semantic_source_count == b->semantic_source_count &&
        a->entry_arity == b->entry_arity && a->program_position == b->program_position &&
        a->result_position == b->result_position && a->query_arity == b->query_arity;
    for (size_t i = 0; ok && i < a->semantic_source_count; i++)
        ok = source(&a->semantic_sources[i], &b->semantic_sources[i]);
    const CettaGsltRequestPipelineV1 *pa = a->request_pipeline, *pb = b->request_pipeline;
    if (pa && pb) {
        ok = ok && text(pa->classify_relation, pb->classify_relation) &&
            text(pa->produce_relation, pb->produce_relation) &&
            text(pa->observe_relation, pb->observe_relation) &&
            text(pa->produced_nil, pb->produced_nil) && text(pa->produced_cons, pb->produced_cons);
    } else ok = ok && pa == pb;
    char sha[65];
    cetta_native_sha256_hex(a->compiled_plan.bytes, a->compiled_plan.length, sha);
    ok = ok && text(sha, a->compiled_plan.sha256) &&
        text(a->compiled_plan.sha256, b->compiled_plan.sha256) &&
        a->compiled_plan.length == b->compiled_plan.length &&
        !memcmp(a->compiled_plan.bytes, b->compiled_plan.bytes, a->compiled_plan.length);
    if (!ok) { fputs("embedded language comparison failed\n", stderr); return 1; }
    printf("(GsltLanguageNativeDescriptorExactV1 %s %zu)\n", a->name, a->compiled_plan.length);
    return 0;
}

#include "native/json_runtime_v1.h"
#include "symbol.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} NameList;

typedef struct {
    uint32_t valid_total;
    uint32_t valid_passed;
    uint32_t invalid_total;
    uint32_t invalid_passed;
    uint32_t policy_total;
    uint32_t policy_accepted;
    uint32_t policy_rejected;
    uint32_t failed;
} CorpusCounts;

static char *copy_text(const char *text) {
    size_t len;
    char *copy;
    if (!text) return NULL;
    len = strlen(text);
    copy = (char *)malloc(len + 1u);
    if (!copy) return NULL;
    (void)memcpy(copy, text, len + 1u);
    return copy;
}

static void name_list_free(NameList *names) {
    size_t index;
    if (!names) return;
    for (index = 0u; index < names->len; index++) free(names->items[index]);
    free(names->items);
    names->items = NULL;
    names->len = 0u;
    names->cap = 0u;
}

static bool name_list_append(NameList *names, const char *name) {
    char **grown;
    char *copy;
    size_t cap;
    if (!names || !name) return false;
    if (names->len == names->cap) {
        cap = names->cap == 0u ? 64u : names->cap * 2u;
        grown = (char **)realloc(names->items, cap * sizeof(*grown));
        if (!grown) return false;
        names->items = grown;
        names->cap = cap;
    }
    copy = copy_text(name);
    if (!copy) return false;
    names->items[names->len++] = copy;
    return true;
}

static int compare_names(const void *left, const void *right) {
    const char *const *left_name = (const char *const *)left;
    const char *const *right_name = (const char *const *)right;
    return strcmp(*left_name, *right_name);
}

static bool is_json_case_name(const char *name) {
    size_t len;
    if (!name || name[1] != '_' ||
        (name[0] != 'y' && name[0] != 'n' && name[0] != 'i')) {
        return false;
    }
    len = strlen(name);
    return len > 7u && strcmp(name + len - 5u, ".json") == 0;
}

static bool collect_names(const char *directory, NameList *names) {
    DIR *stream;
    struct dirent *entry;
    if (!directory || !names || !(stream = opendir(directory))) return false;
    while ((entry = readdir(stream)) != NULL) {
        if (is_json_case_name(entry->d_name) &&
            !name_list_append(names, entry->d_name)) {
            (void)closedir(stream);
            return false;
        }
    }
    if (closedir(stream) != 0) return false;
    qsort(names->items, names->len, sizeof(*names->items), compare_names);
    return true;
}

static uint8_t *read_file(const char *path, size_t *len_out) {
    FILE *file;
    long length;
    uint8_t *bytes;
    if (!path || !len_out || !(file = fopen(path, "rb"))) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc(length > 0 ? (size_t)length : 1u);
    if (!bytes ||
        fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        (void)fclose(file);
        return NULL;
    }
    if (fclose(file) != 0) {
        free(bytes);
        return NULL;
    }
    *len_out = (size_t)length;
    return bytes;
}

static char *join_path(const char *directory, const char *name) {
    size_t directory_len;
    size_t name_len;
    bool slash;
    char *path;
    if (!directory || !name) return NULL;
    directory_len = strlen(directory);
    name_len = strlen(name);
    slash = directory_len > 0u && directory[directory_len - 1u] != '/';
    path = (char *)malloc(directory_len + (slash ? 1u : 0u) + name_len + 1u);
    if (!path) return NULL;
    (void)memcpy(path, directory, directory_len);
    if (slash) path[directory_len++] = '/';
    (void)memcpy(path + directory_len, name, name_len + 1u);
    return path;
}

static bool is_declared_rejection(CettaJsonRuntimeV1Status status) {
    return status == CETTA_JSON_RUNTIME_V1_INVALID_UTF8 ||
        status == CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED ||
        status == CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT ||
        status == CETTA_JSON_RUNTIME_V1_INVALID_UNICODE_ESCAPE;
}

static bool load_runtime(CettaJsonRuntimeV1 **runtime_out,
                         char *error, size_t error_size) {
    uint8_t *language = NULL;
    uint8_t *profile = NULL;
    uint8_t *target = NULL;
    size_t language_len = 0u;
    size_t profile_len = 0u;
    size_t target_len = 0u;
    CettaJsonRuntimeV1 *runtime = NULL;

    language = read_file("langdef/json/rfc8259_syntax_v1.metta",
                         &language_len);
    profile = read_file("langdef/json/rfc8259_parser_profile_v1.metta",
                        &profile_len);
    target = read_file("langdef/json/occurrence_preserving_value_v1.metta",
                       &target_len);
    if (!language || !profile || !target) {
        (void)snprintf(error, error_size,
                       "cannot read authored JSON language sources");
        goto done;
    }
    runtime = cetta_json_runtime_v1_new(
        language, language_len, profile, profile_len,
        target, target_len, error, error_size);

done:
    free(target);
    free(profile);
    free(language);
    if (!runtime) return false;
    *runtime_out = runtime;
    return true;
}

static bool qualify_case(const CettaJsonRuntimeV1 *runtime,
                         Arena *arena,
                         const char *directory,
                         const char *name,
                         CorpusCounts *counts) {
    char *path = join_path(directory, name);
    uint8_t *bytes = NULL;
    size_t len = 0u;
    CettaJsonRuntimeV1Limits limits;
    CettaJsonRuntimeV1Status status = CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT;
    Atom *value = NULL;
    ArenaMark mark = arena_mark(arena);
    char error[512] = {0};
    bool parsed;
    bool passed = false;

    if (!path || !(bytes = read_file(path, &len))) {
        fprintf(stderr, "FAIL: cannot read corpus case %s\n", name);
        counts->failed++;
        free(path);
        return false;
    }
    cetta_json_runtime_v1_default_limits(&limits);
    limits.kernel = CETTA_JSON_KERNEL_V1_PACKED_GLL_GLR_DUAL;
    parsed = cetta_json_runtime_v1_parse(
        runtime, arena, bytes, len, &limits, &value, &status,
        error, sizeof(error));

    switch (name[0]) {
    case 'y':
        counts->valid_total++;
        passed = parsed && status == CETTA_JSON_RUNTIME_V1_OK && value;
        if (passed) counts->valid_passed++;
        break;
    case 'n':
        counts->invalid_total++;
        passed = !parsed && is_declared_rejection(status);
        if (passed) counts->invalid_passed++;
        break;
    case 'i':
        counts->policy_total++;
        passed = (parsed && status == CETTA_JSON_RUNTIME_V1_OK && value) ||
            (!parsed && is_declared_rejection(status));
        if (passed && parsed)
            counts->policy_accepted++;
        else if (passed)
            counts->policy_rejected++;
        break;
    default:
        break;
    }
    if (!passed) {
        fprintf(stderr, "FAIL: %s status=%s detail=%s\n", name,
                cetta_json_runtime_v1_status_name(status),
                error[0] ? error : "<none>");
        counts->failed++;
    }
    arena_reset(arena, mark);
    free(bytes);
    free(path);
    return passed;
}

int main(int argc, char **argv) {
    NameList names = {0};
    CorpusCounts counts = {0};
    CettaJsonRuntimeV1 *runtime = NULL;
    SymbolTable symbols;
    Arena arena;
    char error[512] = {0};
    size_t index;

    if (argc != 2) {
        fprintf(stderr, "usage: %s JSON_TEST_SUITE_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (!collect_names(argv[1], &names) || names.len == 0u) {
        fprintf(stderr, "cannot enumerate JSON corpus directory: %s\n",
                argv[1]);
        name_list_free(&names);
        return 2;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    if (!load_runtime(&runtime, error, sizeof(error))) {
        fprintf(stderr, "cannot prepare authored JSON runtime: %s\n",
                error[0] ? error : "unknown failure");
        name_list_free(&names);
        symbol_table_free(&symbols);
        g_symbols = NULL;
        return 2;
    }

    arena_init(&arena);
    for (index = 0u; index < names.len; index++) {
        (void)qualify_case(runtime, &arena, argv[1], names.items[index],
                           &counts);
    }
    printf("(JsonGsltCorpusSummary %u %u %u %u %u %u %u %u)\n",
           counts.valid_total, counts.valid_passed,
           counts.invalid_total, counts.invalid_passed,
           counts.policy_total, counts.policy_accepted,
           counts.policy_rejected, counts.failed);

    arena_free(&arena);
    cetta_json_runtime_v1_free(runtime);
    name_list_free(&names);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}

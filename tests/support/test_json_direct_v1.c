#define _POSIX_C_SOURCE 200809L
#include "native/json_direct_v1.h"
#include "native/json_value_v1.h"
#include "symbol.h"
#include "library_json.h"
#include "lang.h"
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static unsigned passed, failed;
static unsigned reference_limits;
static unsigned number_validation_calls;
static bool number_validation_bytes;

static bool reject_number_lexeme(const uint8_t *bytes, size_t length) {
    number_validation_calls++;
    number_validation_bytes = length == 2u && bytes[0] == '1' && bytes[1] == '2';
    return false;
}
static CettaLanguageId test_language = CETTA_LANGUAGE_HE;
CettaLanguageId eval_current_language_id(void) { return test_language; }
#define CHECK(c) do { if (c) passed++; else { failed++; fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); } } while (0)

static bool ordinary_bool(Atom *value, bool petta, bool truth) {
    return petta
        ? atom_is_symbol(value, truth ? "true" : "false")
        : value && value->kind == ATOM_GROUNDED &&
          value->ground.gkind == GV_BOOL &&
          value->ground.bval == truth;
}

static uint8_t *read_bytes(const char *path, size_t *len) {
    FILE *file=fopen(path,"rb");
    if (!file) return NULL;
    if (fseek(file,0,SEEK_END)) { fclose(file); return NULL; }
    long n=ftell(file);
    if (n<0 || fseek(file,0,SEEK_SET)) { fclose(file); return NULL; }
    uint8_t *bytes=malloc((size_t)n+1);
    if (!bytes || fread(bytes,1,(size_t)n,file)!=(size_t)n) { free(bytes); fclose(file); return NULL; }
    fclose(file); *len=(size_t)n; bytes[n]=0; return bytes;
}

static CettaJsonRuntimeV1 *reference(void) {
    size_t a=0,b=0,c=0;
    uint8_t *syntax=read_bytes("langdef/json/rfc8259_syntax_v1.metta",&a);
    uint8_t *profile=read_bytes("langdef/json/rfc8259_parser_profile_v1.metta",&b);
    uint8_t *target=read_bytes("langdef/json/occurrence_preserving_value_v1.metta",&c);
    char error[512]={0};
    CettaJsonRuntimeV1 *runtime=cetta_json_runtime_v1_new(syntax,a,profile,b,target,c,error,sizeof(error));
    free(syntax); free(profile); free(target);
    if (!runtime) fprintf(stderr,"reference preparation: %s\n",error);
    return runtime;
}

static bool fusion_agrees(Atom *ordinary, uint32_t work, uint32_t depth, size_t limit) {
    Arena temporary; arena_init_detached(&temporary);
    Atom *canonical = NULL, *normalized = NULL, *projected = NULL;
    CettaJsonValueV1Status old_status, new_status, validation_status;
    char error[256];
    uint8_t *old_bytes = NULL, *new_bytes = NULL;
    size_t old_len = 17u, new_len = 17u;
    bool converted = cetta_json_value_v1_from_ordinary(&temporary, ordinary, work, depth,
        &canonical, &old_status, error, sizeof(error));
    CettaJsonValueV1Status conversion_status = old_status;
    bool validated = cetta_json_value_v1_validate_ordinary(ordinary, work, depth,
        &validation_status, error, sizeof(error));
    bool ok = converted == validated && old_status == validation_status;
    bool old_ok = converted && cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, canonical, false, work, depth,
        limit, &old_bytes, &old_len, &old_status, error, sizeof(error));
    bool new_ok = cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, ordinary, true, work, depth,
        limit, &new_bytes, &new_len, &new_status, error, sizeof(error));
    ok = ok && old_ok == new_ok && old_status == new_status && old_len == new_len &&
        (old_ok ? !memcmp(old_bytes, new_bytes, old_len) : !old_bytes && !new_bytes);
    if (converted && !work && !depth) {
        ok = ok && cetta_json_value_v1_to_ordinary(&temporary, canonical, 0u, 0u,
            &projected, &old_status, error, sizeof(error)) &&
            cetta_json_value_v1_normalize_ordinary(&temporary, ordinary, &normalized,
                &new_status, error, sizeof(error)) && atom_eq(projected, normalized);
    }
    if (!ok) fprintf(stderr, "fusion mismatch work=%u depth=%u bytes=%zu: conversion=%d/%u validation=%d/%u emission=%d/%u vs %d/%u\n",
        work, depth, limit, converted, conversion_status, validated, validation_status,
        old_ok, old_status, new_ok, new_status);
    free(old_bytes); free(new_bytes); arena_free(&temporary);
    return ok;
}

static bool agree(const CettaJsonRuntimeV1 *runtime, const uint8_t *bytes,size_t n) {
    Arena arena; arena_init(&arena);
    Atom *fast=NULL,*slow=NULL,*ordinary=NULL,*converted=NULL;
    CettaJsonRuntimeV1Limits limits;
    CettaJsonRuntimeV1Status a,b;
    CettaJsonValueV1Status codec;
    char error[512]={0};
    cetta_json_runtime_v1_default_limits(&limits);
    bool x=cetta_json_direct_v1_parse(&arena,bytes,n,false,false,0,&fast,&a,error,sizeof(error));
    CettaJsonRuntimeV1Status direct_status = a;
    Atom *probe_parts[] = {
        atom_string(&arena, "__cetta_projection_absent_probe_v1__"),
    };
    Atom *probe_path = atom_expr(&arena, probe_parts, 1u);
    Atom *probe_value = NULL;
    bool probe_found = false;
    CettaJsonRuntimeV1Status probe_status;
    bool probe = cetta_json_direct_v1_project(
        &arena, bytes, n, probe_path, false, false, 0u,
        &probe_value, &probe_found, &probe_status, error, sizeof(error));
    bool y=cetta_json_runtime_v1_parse(runtime,&arena,bytes,n,&limits,&slow,&b,error,sizeof(error));
    bool ok=x==y && probe==x &&
        (x || probe_status == direct_status) &&
        (!x || atom_eq(fast,slow));
    if (x && y) {
        ok=ok && cetta_json_direct_v1_parse(&arena,bytes,n,true,false,0,&ordinary,&a,error,sizeof(error)) &&
            cetta_json_value_v1_to_ordinary(&arena,slow,4000000,1024,&converted,&codec,error,sizeof(error)) &&
            atom_eq(ordinary,converted) && fusion_agrees(ordinary, 0u, 0u, 0u);
    }
    if (!ok && x && !y && b==CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT) {
        /* A reference resource cap does not refute a grammar-derived value.
         * Report it separately; ledger qualification still requires equality. */
        reference_limits++;
    }
    if (!ok) fprintf(stderr,"agreement failed: bytes=%zu direct=%d reference=%d reference-status=%u %s\n",n,x,y,b,error);
    arena_free(&arena); return ok;
}

typedef struct { Arena kept; Atom *copy; unsigned rows; size_t last; } Consumer;
static bool consume(void *context, Atom *value,size_t line) {
    Consumer *c=context;
    c->rows++; c->last=line; c->copy=atom_deep_copy(&c->kept,value); return c->copy!=NULL;
}

static void *concurrent(void *context) {
    unsigned *errors=context;
    const char *input="{\"x\":[1,true,\"\\u0000λ\"],\"x\":2}";
    for (unsigned i=0;i<100;i++) {
        Arena a; arena_init_detached(&a); Atom *value=NULL;
        if (!cetta_json_direct_v1_parse(&a,(const uint8_t *)input,strlen(input),true,true,32,&value,NULL,NULL,0)) (*errors)++;
        arena_free(&a);
    }
    return NULL;
}

static void deep_values(void) {
    const size_t depth = 100000u;
    for (unsigned object = 0; object < 2; object++) {
        size_t prefix = object ? 5u : 1u;
        size_t size = (prefix + 1u) * depth + 4u;
        uint8_t *text = malloc(size);
        CHECK(text != NULL);
        if (!text) return;
        for (size_t i = 0; i < depth; i++) memcpy(text + prefix*i, object ? "{\"x\":" : "[", prefix);
        memcpy(text + prefix*depth, "true", 4u);
        memset(text + prefix*depth + 4u, object ? '}' : ']', depth);
        for (unsigned ordinary = 0; ordinary < 2; ordinary++) {
            for (unsigned petta = 0; petta < 2; petta++) {
                Arena arena; arena_init(&arena);
                Atom *value = NULL;
                CettaJsonRuntimeV1Status status;
                char error[256];
                bool ok = cetta_json_direct_v1_parse(&arena, text, size, ordinary, petta,
                                                    0u, &value, &status, error, sizeof(error));
                CHECK(ok);
                Atom *path_step = object
                    ? atom_string(&arena, "x") : atom_int(&arena, 0);
                Atom **path_items = arena_alloc(
                    &arena, sizeof(*path_items) * depth);
                for (size_t i = 0u; path_items && i < depth; i++)
                    path_items[i] = path_step;
                Atom *path = path_items
                    ? atom_expr(&arena, path_items, depth) : NULL;
                Atom *selected = NULL;
                bool found = false;
                CHECK(path && cetta_json_direct_v1_project(
                    &arena, text, size, path, ordinary, petta, 0u,
                    &selected, &found, &status, error, sizeof(error)) &&
                    found && selected &&
                    (ordinary
                        ? ordinary_bool(selected, petta, true)
                        : selected->kind == ATOM_EXPR &&
                          selected->expr.len == 2u &&
                          ordinary_bool(
                              selected->expr.elems[1], petta, true)));
                Atom *leaf = value;
                for (size_t i = 0; ok && i < depth; i++) {
                    ok = leaf && leaf->kind == ATOM_EXPR && leaf->expr.len == 2u;
                    Atom *items = ok ? leaf->expr.elems[1] : NULL;
                    ok = ok && items && items->kind == ATOM_EXPR && items->expr.len == 1u;
                    if (ok) leaf = object ? items->expr.elems[0]->expr.elems[ordinary ? 2 : 3] : items->expr.elems[0];
                }
                CHECK(ok && leaf &&
                    (ordinary
                        ? ordinary_bool(leaf, petta, true)
                        : leaf->kind == ATOM_EXPR && leaf->expr.len == 2u &&
                          ordinary_bool(leaf->expr.elems[1], petta, true)));
                uint8_t *encoded = NULL; size_t encoded_size = 0;
                CettaJsonValueV1Status codec;
                CHECK(ok && cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, value, ordinary, 0u, 0u, 0u, &encoded,
                    &encoded_size, &codec, error, sizeof(error)) && encoded_size == size && !memcmp(encoded, text, size));
                free(encoded);
                CettaJsonLibraryRuntimeV1 *library = cetta_json_library_runtime_v1_new(error, sizeof(error));
                CHECK(library != NULL);
                test_language = petta ? CETTA_LANGUAGE_PETTA : CETTA_LANGUAGE_HE;
                Atom *source[] = {atom_string_n(&arena, (const char *)text, size)};
                Atom *parsed = library ? cetta_json_library_dispatch_v1(library, &arena,
                    atom_symbol(&arena, ordinary ? "__cetta_lib_json_parse" : "__cetta_lib_json_parse_v1"), source, 1u) : NULL;
                CHECK(parsed && parsed->kind == ATOM_EXPR && atom_is_symbol(parsed->expr.elems[0],
                    object ? (ordinary ? "JsonObject" : "JsonObjectV1") : (ordinary ? "JsonArray" : "JsonArrayV1")));
                if (parsed) value = parsed;
                Atom *argument[] = {value};
                Atom *written = library ? cetta_json_library_dispatch_v1(library, &arena,
                    atom_symbol(&arena, ordinary ? "__cetta_lib_json_stringify" : "__cetta_lib_json_stringify_v1"),
                    argument, 1u) : NULL;
                CHECK(written && written->kind == ATOM_GROUNDED && written->ground.gkind == GV_STRING &&
                    atom_string_len(written) == size && !memcmp(written->ground.sval, text, size));
                if (!ordinary && library) {
                    Atom *projected = cetta_json_library_dispatch_v1(library, &arena,
                        atom_symbol(&arena, "__cetta_lib_json_from_inspection_v1"), argument, 1u);
                    argument[0] = projected;
                    written = cetta_json_library_dispatch_v1(library, &arena,
                        atom_symbol(&arena, "__cetta_lib_json_stringify"), argument, 1u);
                    CHECK(written && written->kind == ATOM_GROUNDED && written->ground.gkind == GV_STRING &&
                    atom_string_len(written) == size && !memcmp(written->ground.sval, text, size));
                    Atom *last = projected;
                    bool shape = true;
                    for (size_t i = 0; shape && i < depth; i++) {
                        shape = last && last->kind == ATOM_EXPR && last->expr.len == 2u &&
                            last->expr.elems[1] && last->expr.elems[1]->kind == ATOM_EXPR &&
                            last->expr.elems[1]->expr.len == 1u;
                        if (shape) last = object ? last->expr.elems[1]->expr.elems[0]->expr.elems[2] : last->expr.elems[1]->expr.elems[0];
                    }
                    CHECK(shape && ordinary_bool(last, petta, true));
                }
                cetta_json_library_runtime_v1_free(library);
                Atom *converted = NULL;
                bool converted_ok = ordinary
                    ? cetta_json_value_v1_from_ordinary(&arena, value, 0u, 0u, &converted, &codec, error, sizeof(error))
                    : cetta_json_value_v1_to_ordinary(&arena, value, 0u, 0u, &converted, &codec, error, sizeof(error));
                CHECK(ok && converted_ok);
                encoded = NULL; encoded_size = 0;
                CHECK(converted_ok && cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, converted, !ordinary, 0u, 0u, 0u, &encoded,
                    &encoded_size, &codec, error, sizeof(error)) && encoded_size == size && !memcmp(encoded, text, size));
                free(encoded);
                ArenaMark mark = arena_mark(&arena);
                Atom *sentinel = value;
                CHECK(!cetta_json_direct_v1_parse(&arena, text, size-1u, ordinary, petta, 0u,
                    &value, &status, error, sizeof(error)) && value == sentinel && arena.live_bytes == mark.live_bytes);
                arena_free(&arena);
            }
        }
        free(text);
    }
}

static void lookup_controls(void) {
    Arena arena; arena_init_detached(&arena);
    CettaJsonLibraryRuntimeV1 *library = cetta_json_library_runtime_v1_new(NULL, 0);
    CHECK(library != NULL);
    if (!library) { arena_free(&arena); return; }
    for (unsigned lane = 0; lane < 2u; lane++) {
        test_language = lane ? CETTA_LANGUAGE_PETTA : CETTA_LANGUAGE_HE;
        for (unsigned truth = 0; truth < 2u; truth++) {
            Atom *boolean = lane
                ? atom_symbol(&arena, truth ? "true" : "false")
                : atom_bool(&arena, truth != 0u);
            Atom *pair[] = {atom_symbol(&arena, "JsonPair"), atom_string(&arena, "key"), boolean};
            Atom *pairs[] = {atom_expr(&arena, pair, 3u)};
            Atom *object[] = {atom_symbol(&arena, "JsonObject"), atom_expr(&arena, pairs, 1u)};
            Atom *args[] = {atom_expr(&arena, object, 2u), atom_string(&arena, "key")};
            Atom *result = cetta_json_library_dispatch_v1(library, &arena,
                atom_symbol(&arena, "__cetta_lib_json_object_get"), args, 2u);
            CHECK(ordinary_bool(result, lane != 0u, truth != 0u));
        }
    }
    test_language = CETTA_LANGUAGE_HE;
    Atom *invalid_number[] = {
        atom_symbol(&arena, "InvalidJsonNumberV1"),
        atom_string(&arena, "01")};
    Atom *first[] = {atom_symbol(&arena, "JsonPair"), atom_string(&arena, "key"), atom_expr(&arena, invalid_number, 2u)};
    const char invalid[] = {(char)0xc0, (char)0x80};
    Atom *last[] = {atom_symbol(&arena, "JsonPair"), atom_string(&arena, "later"), atom_string_n(&arena, invalid, sizeof(invalid))};
    Atom *pairs[] = {atom_expr(&arena, first, 3u), atom_expr(&arena, last, 3u)};
    Atom *object[] = {atom_symbol(&arena, "JsonObject"), atom_expr(&arena, pairs, 2u)};
    Atom *args[] = {atom_expr(&arena, object, 2u), atom_string(&arena, "key")};
    CHECK(fusion_agrees(args[0], 0u, 0u, 0u));
    Atom *local = cetta_json_library_dispatch_v1(library, &arena,
        atom_symbol(&arena, "__cetta_lib_json_object_get"), args, 2u);
    /* Projection is not a whole-document validation pass. Invalid unrelated
     * payloads still fail emission; a small selected field stays local. */
    CHECK(local && local->kind == ATOM_EXPR &&
          atom_is_symbol(local->expr.elems[0], "InvalidJsonNumberV1"));
    object[1] = atom_expr(&arena, pairs, 1u); args[0] = atom_expr(&arena, object, 2u);
    /* Lookup validates value structure, not the number's JSON spelling. */
    Atom *selected = cetta_json_library_dispatch_v1(library, &arena,
        atom_symbol(&arena, "__cetta_lib_json_object_get"), args, 2u);
    CHECK(selected && selected->kind == ATOM_EXPR && atom_is_symbol(selected->expr.elems[0], "InvalidJsonNumberV1") &&
          atom_string_equals_cstr(selected->expr.elems[1], "01"));
    Atom *copies[] = {selected};
    Atom *rejected_number = cetta_json_library_dispatch_v1(library, &arena,
        atom_symbol(&arena, "__cetta_lib_json_stringify"), copies, 1u);
    CHECK(rejected_number && rejected_number->kind == ATOM_EXPR && atom_is_symbol(rejected_number->expr.elems[0], "JsonFailureV1"));
    cetta_json_library_runtime_v1_free(library); arena_free(&arena);
}

static Atom *ordinary_first_path(Atom *value, Atom *path) {
    if (!value || !path || path->kind != ATOM_EXPR)
        return NULL;
    for (CettaExprIndex step = 0u; step < path->expr.len; step++) {
        Atom *component = path->expr.elems[step];
        if (component->kind == ATOM_GROUNDED &&
            component->ground.gkind == GV_STRING) {
            if (!value || value->kind != ATOM_EXPR ||
                value->expr.len != 2u ||
                !atom_is_symbol(value->expr.elems[0], "JsonObject") ||
                value->expr.elems[1]->kind != ATOM_EXPR) {
                return NULL;
            }
            Atom *selected = NULL;
            Atom *pairs = value->expr.elems[1];
            for (CettaExprIndex at = 0u; at < pairs->expr.len; at++) {
                Atom *pair = pairs->expr.elems[at];
                if (!pair || pair->kind != ATOM_EXPR ||
                    pair->expr.len != 3u ||
                    !atom_is_symbol(pair->expr.elems[0], "JsonPair")) {
                    return NULL;
                }
                Atom *key = pair->expr.elems[1];
                if (key->kind == ATOM_GROUNDED &&
                    key->ground.gkind == GV_STRING &&
                    atom_string_len(key) == atom_string_len(component) &&
                    memcmp(key->ground.sval, component->ground.sval,
                           atom_string_len(key)) == 0) {
                    selected = pair->expr.elems[2];
                    break;
                }
            }
            value = selected;
        } else if (component->kind == ATOM_GROUNDED &&
                   component->ground.gkind == GV_INT &&
                   component->ground.ival >= 0) {
            if (!value || value->kind != ATOM_EXPR ||
                value->expr.len != 2u ||
                !atom_is_symbol(value->expr.elems[0], "JsonArray") ||
                value->expr.elems[1]->kind != ATOM_EXPR ||
                (uint64_t)component->ground.ival >=
                    value->expr.elems[1]->expr.len) {
                return NULL;
            }
            value = value->expr.elems[1]
                ->expr.elems[(CettaExprIndex)component->ground.ival];
        } else {
            return NULL;
        }
        if (!value)
            return NULL;
    }
    return value;
}

static bool projection_agrees(const char *text, Atom *path, bool petta) {
    Arena arena;
    arena_init_detached(&arena);
    Atom *whole = NULL, *projected = NULL;
    bool found = false;
    CettaJsonRuntimeV1Status whole_status, projected_status;
    char error[256] = {0};
    bool parsed = cetta_json_direct_v1_parse(
        &arena, (const uint8_t *)text, strlen(text), true, petta, 0u,
        &whole, &whole_status, error, sizeof(error));
    bool selected = cetta_json_direct_v1_project(
        &arena, (const uint8_t *)text, strlen(text), path, true, petta, 0u,
        &projected, &found, &projected_status, error, sizeof(error));
    Atom *expected = parsed ? ordinary_first_path(whole, path) : NULL;
    bool ok = parsed == selected &&
        (!parsed
            ? whole_status == projected_status
            : found == (expected != NULL) &&
              (!found || atom_eq(projected, expected)));
    if (!ok) {
        fprintf(stderr,
            "projection disagreement petta=%d parsed=%d selected=%d found=%d whole-status=%u projected-status=%u text=%s\n",
            petta, parsed, selected, found, whole_status, projected_status,
            text);
    }
    arena_free(&arena);
    return ok;
}

static void projection_controls(void) {
    Arena paths;
    arena_init_detached(&paths);
    Atom *empty = atom_expr(&paths, NULL, 0u);
    Atom *nested_parts[] = {
        atom_string(&paths, "choices"), atom_int(&paths, 1),
        atom_string(&paths, "message"), atom_string(&paths, "content"),
    };
    Atom *nested = atom_expr(&paths, nested_parts, 4u);
    Atom *duplicate_parts[] = {atom_string(&paths, "x")};
    Atom *duplicate = atom_expr(&paths, duplicate_parts, 1u);
    const char nul_key[] = {'a', 0, 'b'};
    Atom *nul_parts[] = {atom_string_n(&paths, nul_key, sizeof(nul_key))};
    Atom *nul = atom_expr(&paths, nul_parts, 1u);
    Atom *missing_parts[] = {atom_string(&paths, "missing"), atom_int(&paths, 9)};
    Atom *missing = atom_expr(&paths, missing_parts, 2u);
    Atom *paths_to_check[] = {empty, nested, duplicate, nul, missing};
    const char *documents[] = {
        "{\"choices\":[{\"message\":{\"content\":\"first\"}},{\"message\":{\"content\":\"λ\\u0000z\"}}],\"x\":1,\"x\":2,\"a\\u0000b\":true}",
        "{\"choices\":[{\"message\":{\"content\":\"first\"}},{\"message\":{\"content\":\"λ\\u0000z\"}}],\"x\":1,\"x\":2,\"a\\u0000b\":true}",
        "{\"x\":1,\"x\":2}",
        "{\"a\\u0000b\":false,\"later\":[1,2,3]}",
        "{\"present\":null}",
    };
    for (unsigned lane = 0u; lane < 2u; lane++)
        for (unsigned at = 0u; at < 5u; at++)
            CHECK(projection_agrees(
                documents[at], paths_to_check[at], lane != 0u));

    Atom *bad_parts[] = {atom_bool(&paths, true)};
    Atom *bad_path = atom_expr(&paths, bad_parts, 1u);
    Arena output;
    arena_init_detached(&output);
    Atom *sentinel = atom_symbol(&output, "unchanged");
    Atom *result = sentinel;
    bool found = true;
    CettaJsonRuntimeV1Status status;
    char error[256] = {0};
    ArenaMark mark = arena_mark(&output);
    CHECK(!cetta_json_direct_v1_project(
        &output, (const uint8_t *)"{\"x\":1}", 7u, bad_path,
        true, false, 0u, &result, &found, &status, error, sizeof(error)) &&
        result == NULL && !found &&
        status == CETTA_JSON_RUNTIME_V1_BAD_ARGUMENT &&
        output.live_bytes == mark.live_bytes);
    result = sentinel;
    found = true;
    mark = arena_mark(&output);
    const char *malformed = "{\"x\":1,\"bad\":[1,]}";
    CHECK(!cetta_json_direct_v1_project(
        &output, (const uint8_t *)malformed, strlen(malformed), duplicate,
        true, false, 0u, &result, &found, &status, error, sizeof(error)) &&
        result == NULL && !found &&
        status == CETTA_JSON_RUNTIME_V1_SYNTAX_REJECTED &&
        output.live_bytes == mark.live_bytes);
    arena_free(&output);
    arena_free(&paths);
}

static void pretty_controls(void) {
    const char *inputs[] = {"null", "[]", "{}", "[1,2]", "{\"a\":[1,{},[]],\"a\":\"x\\u0000y\"}"};
    const char *expected[] = {"null", "[]", "{}", "[\n  1,\n  2\n]",
        "{\n  \"a\": [\n    1,\n    {},\n    []\n  ],\n  \"a\": \"x\\u0000y\"\n}"};
    for (unsigned mode = 0u; mode < 2u; mode++) {
        for (unsigned at = 0u; at < sizeof(inputs)/sizeof(*inputs); at++) {
            Arena arena; arena_init_detached(&arena);
            Atom *value = NULL, *parsed = NULL;
            CettaJsonRuntimeV1Status parse_status;
            CettaJsonValueV1Status status;
            char error[256]; uint8_t *text = NULL; size_t size = 0u;
            CHECK(cetta_json_direct_v1_parse(&arena, (const uint8_t *)inputs[at], strlen(inputs[at]),
                mode != 0u, false, 0u, &value, &parse_status, error, sizeof(error)));
            CHECK(cetta_json_value_v1_emit_pretty(cetta_json_direct_v1_number_valid, value, mode != 0u, 0u, 0u, 0u,
                &text, &size, &status, error, sizeof(error)) && size == strlen(expected[at]) &&
                !memcmp(text, expected[at], size));
            CHECK(cetta_json_direct_v1_parse(&arena, text, size, true, false, 0u,
                &parsed, &parse_status, error, sizeof(error)));
            free(text); text = NULL; size = 0u;
            CHECK(cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, parsed, true, 0u, 0u, 0u, &text, &size, &status,
                error, sizeof(error)) && size == strlen(inputs[at]) && !memcmp(text, inputs[at], size));
            free(text); text = NULL; size = 17u;
            CHECK(!cetta_json_value_v1_emit_pretty(cetta_json_direct_v1_number_valid, value, mode != 0u, 0u, 0u, 1u,
                &text, &size, &status, error, sizeof(error)) && !text && size == 17u &&
                status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT);
            arena_free(&arena);
        }
    }
}

static void unit(const CettaJsonRuntimeV1 *runtime) {
    lookup_controls();
    projection_controls();
    pretty_controls();
    const char *cases[]={"null","false","true","0","-0","-12.50e+2"," \t\r\n[  ] ","{\"x\":1,\"x\":2}",
        "{\"λ\": \"λ\\u0000z\", \"x\":[{ },[ ]]}","\"\\uD834\\uDD1E\"","\"a\\tb\\nc\\rd\\fe\\bf\\/g\\\\h\\\"\"",
        "{ }","{  }","[  ]","[1,]","01","-","1e","[","{\"x\":}","\"\\uD800\"","\"\\uDC00\"","\"\\x\"",
        "true false","\"\n\"","\"unterminated","", " ","[0.01,1E-10,12345678901234567890]"};
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);i++) CHECK(agree(runtime,(const uint8_t *)cases[i],strlen(cases[i])));
    const uint8_t invalid[][6]={{34,0,34},{34,0xc0,0x80,34},{34,0xed,0xa0,0x80,34},{34,0xf4,0x90,0x80,0x80,34}};
    const size_t lengths[]={3,4,5,6};
    for(unsigned i=0;i<4;i++) CHECK(agree(runtime,invalid[i],lengths[i]));
    Arena arena; arena_init(&arena);
    Atom *sentinel=atom_string(&arena,"keep"), *value=sentinel;
    ArenaMark mark=arena_mark(&arena);
    char error[256]; CettaJsonRuntimeV1Status status;
    const char *bad="{\"x\":[1,2,3,]}";
    CHECK(!cetta_json_direct_v1_parse(&arena,(const uint8_t *)bad,strlen(bad),true,false,32,&value,&status,error,sizeof(error)));
    CHECK(value==sentinel && arena.live_bytes==mark.live_bytes && atom_string_equals_cstr(sentinel,"keep"));
    CHECK(strstr(error,"JSON byte ") && strstr(error,"expected "));
    const char *nested="[[[[0]]]]";
    CHECK(!cetta_json_direct_v1_parse(&arena,(const uint8_t *)nested,strlen(nested),true,false,4,&value,&status,error,sizeof(error)) && status==CETTA_JSON_RUNTIME_V1_RESOURCE_LIMIT);
    CHECK(cetta_json_direct_v1_parse(&arena,(const uint8_t *)nested,strlen(nested),true,false,5,&value,&status,error,sizeof(error)));
    for (unsigned work = 0; work < 8u; work++)
        for (unsigned depth = 0; depth < 7u; depth++)
            for (unsigned bytes = 0; bytes < 12u; bytes++)
                CHECK(fusion_agrees(value, work, depth, bytes));
    Atom *symbolic = atom_symbol(&arena, "text");
    CHECK(fusion_agrees(symbolic, 0u, 0u, 0u));
    for (unsigned i = 0; i < 4u; i++) {
        CHECK(fusion_agrees(atom_string_n(
            &arena, (const char *)invalid[i] + 1u, lengths[i] - 2u),
            0u, 0u, 0u));
    }
    const uint8_t scalars[] = {0, 1, 8, 9, 10, 12, 13, 31, 34, 92, 127,
        0xc2, 0x80, 0xdf, 0xbf, 0xe0, 0xa0, 0x80, 0xed, 0x9f, 0xbf,
        0xee, 0x80, 0x80, 0xef, 0xbf, 0xbf, 0xf0, 0x90, 0x80, 0x80,
        0xf4, 0x8f, 0xbf, 0xbf};
    Atom *boundary = atom_string_n(
        &arena, (const char *)scalars, sizeof(scalars));
    for (unsigned work = 0; work < 24u; work++)
        for (unsigned bytes = 0; bytes < 80u; bytes++)
            CHECK(fusion_agrees(boundary, work, 0u, bytes));
    Atom *canonical = NULL, *limited = sentinel;
    CettaJsonValueV1Status value_status;
    CHECK(cetta_json_direct_v1_parse(&arena,(const uint8_t *)nested,strlen(nested),false,false,0,&canonical,&status,error,sizeof(error)));
    ArenaMark before_conversion = arena_mark(&arena);
    CHECK(!cetta_json_value_v1_to_ordinary(&arena, canonical, 2u, 0u, &limited, &value_status,
        error, sizeof(error)) && value_status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT && limited == sentinel &&
        arena.live_bytes == before_conversion.live_bytes);
    CHECK(!cetta_json_value_v1_to_ordinary(&arena, canonical, 0u, 4u, &limited, &value_status,
        error, sizeof(error)) && value_status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT && limited == sentinel &&
        arena.live_bytes == before_conversion.live_bytes);
    uint8_t *limited_text = NULL; size_t limited_size = 17u;
    CHECK(!cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, canonical, false, 0u, 4u, 0u, &limited_text, &limited_size,
        &value_status, error, sizeof(error)) && value_status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT &&
        !limited_text && limited_size == 17u);
    CHECK(!cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, canonical, false, 0u, 0u, 2u, &limited_text, &limited_size,
        &value_status, error, sizeof(error)) && value_status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT &&
        !limited_text && limited_size == 17u);
    Atom *number_items[] = {atom_symbol(&arena, "JsonNumberV1"), atom_string(&arena, "12")};
    Atom *number_value = atom_expr(&arena, number_items, 2u);
    CHECK(!cetta_json_value_v1_emit(NULL, number_value, false, 0u, 0u, 0u,
        &limited_text, &limited_size, &value_status, error, sizeof(error)) &&
        value_status == CETTA_JSON_VALUE_V1_BAD_ARGUMENT && !limited_text && limited_size == 17u);
    CHECK(!cetta_json_value_v1_emit(reject_number_lexeme, number_value, false, 0u, 0u, 0u,
        &limited_text, &limited_size, &value_status, error, sizeof(error)) &&
        value_status == CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT &&
        number_validation_calls == 1u && number_validation_bytes && !limited_text && limited_size == 17u);
    number_items[1] = atom_string(&arena, "01");
    number_value = atom_expr(&arena, number_items, 2u);
    CHECK(!cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, number_value, false, 0u, 0u, 0u,
        &limited_text, &limited_size, &value_status, error, sizeof(error)) &&
        value_status == CETTA_JSON_VALUE_V1_ROUNDTRIP_DISAGREEMENT && !limited_text && limited_size == 17u);
    const char *numbers[]={"0","-1","0.1","1e-1","01","1.","NaN","1e+",""," 0"};
    for(unsigned i=0;i<10;i++) CHECK(cetta_json_direct_v1_number_valid((const uint8_t *)numbers[i],strlen(numbers[i]))==(i<4));
    char *wide=malloc(24010); size_t n=0;
    wide[n++]='[';
    for(unsigned i=0;i<6000;i++) { if(i) wide[n++]=','; wide[n++]='0'; }
    wide[n++]=']';
    CHECK(cetta_json_direct_v1_parse(&arena,(const uint8_t *)wide,n,true,false,2,&value,&status,error,sizeof(error)) && value->expr.elems[1]->expr.len==6000);
    free(wide); arena_free(&arena);
    arena_init(&arena);
    CHECK(cetta_json_direct_v1_parse(&arena, (const uint8_t *)cases[7], strlen(cases[7]),
                                    false, false, 0u, &value, &status, error, sizeof(error)));
    uint8_t *reference_text = NULL; size_t reference_size = 0;
    CettaJsonValueV1Status reference_status;
    CHECK(cetta_json_value_v1_stringify(runtime, value, false, 0u, 0u, 0u,
        &reference_text, &reference_size, &reference_status, error, sizeof(error)) &&
        reference_size == strlen(cases[7]) && !memcmp(reference_text, cases[7], reference_size));
    free(reference_text); arena_free(&arena);
    Consumer c={0}; arena_init(&c.kept);
    FILE *stream=tmpfile(); size_t failed_line=0;
    CHECK(stream!=NULL);
    if(stream) {
        fputs("1\n\n{\"x\":\"\\u0000\"}\n[1,]\n2\n",stream); rewind(stream);
        CHECK(!cetta_json_direct_v1_lines(stream,true,false,32,consume,&c,&failed_line,error,sizeof(error)));
        CHECK(c.rows==2 && c.last==3 && failed_line==4 && c.copy!=NULL);
        uint8_t *encoded=NULL; size_t len=0; CettaJsonValueV1Status s;
        CHECK(cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, c.copy,true,1000,32,1000,&encoded,&len,&s,error,sizeof(error)) &&
              len==strlen("{\"x\":\"\\u0000\"}") && !memcmp(encoded,"{\"x\":\"\\u0000\"}",len));
        free(encoded); fclose(stream);
    }
    arena_free(&c.kept);
    pthread_t threads[4]; unsigned errors[4]={0};
    for(unsigned i=0;i<4;i++) CHECK(!pthread_create(threads+i,NULL,concurrent,errors+i));
    for(unsigned i=0;i<4;i++) { CHECK(!pthread_join(threads[i],NULL)); CHECK(errors[i]==0); }
    /* Deterministic short input mutations, including every truncation. */
    const char *sample="{\"λ\":[0,\"\\uD834\\uDD1E\",false],\"x\":null}";
    n=strlen(sample);
    for(size_t i=0;i<n;i++) CHECK(agree(runtime,(const uint8_t *)sample,i));
    uint32_t random=17419; uint8_t mutated[80];
    for(unsigned i=0;i<200;i++) {
        memcpy(mutated,sample,n); random=random*1664525u+1013904223u;
        mutated[random%n]=(uint8_t)(random>>24);
        CHECK(agree(runtime,mutated,n));
    }
    deep_values();
}

static bool corpus(const CettaJsonRuntimeV1 *runtime,const char *directory,bool use_reference) {
    DIR *dir=opendir(directory); if(!dir) return false;
    unsigned yes=0,no=0,policy=0,bad=0;
    struct dirent *entry;
    while((entry=readdir(dir))) {
        const char *name=entry->d_name;
        if(strlen(name)<3 || name[1]!='_' || (name[0]!='y' && name[0]!='n' && name[0]!='i')) continue;
        char path[4096]; snprintf(path,sizeof(path),"%s/%s",directory,name);
        size_t n=0; uint8_t *bytes=read_bytes(path,&n);
        if(!bytes) { bad++; continue; }
        Arena arena; arena_init(&arena); Atom *value=NULL;
        bool ok=cetta_json_direct_v1_parse(&arena,bytes,n,false,false,0,&value,NULL,NULL,0);
        if(name[0]=='y') { yes++; if(!ok) { bad++; fprintf(stderr,"valid rejected: %s\n",name); } }
        else if(name[0]=='n') { no++; if(ok) { bad++; fprintf(stderr,"invalid accepted: %s\n",name); } }
        else policy++;
        unsigned before=reference_limits;
        if(use_reference && !agree(runtime,bytes,n) && reference_limits==before) { bad++; fprintf(stderr,"corpus disagreement: %s\n",name); }
        if(reference_limits!=before) fprintf(stderr,"reference limit: %s\n",name);
        arena_free(&arena); free(bytes);
    }
    closedir(dir); printf("{\"valid\":%u,\"invalid\":%u,\"policy\":%u,\"failed\":%u,\"reference_limits\":%u}\n",yes,no,policy,bad,reference_limits); return !bad;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
static bool lines(const CettaJsonRuntimeV1 *runtime,const char *path,bool compare) {
    FILE *stream=fopen(path,"rb"); if(!stream) return false;
    char *line=NULL; size_t capacity=0,rows=0,bytes=0,peak=0; ssize_t n;
    double start=now(); bool ok=true;
    while((n=getline(&line,&capacity,stream))>=0) {
        bytes+=(size_t)n; rows++;
        if(compare) {
            if(!agree(runtime,(const uint8_t *)line,(size_t)n)) { fprintf(stderr,"row %zu\n",rows); ok=false; break; }
            if(rows%1000==0) { fprintf(stderr,"checked %zu rows\n",rows); fflush(stderr); }
        } else {
            Arena arena; arena_init(&arena); Atom *value=NULL; char error[256];
            bool accepted=cetta_json_direct_v1_parse(&arena,(const uint8_t *)line,(size_t)n,true,false,0,&value,NULL,error,sizeof(error));
            if(arena.live_bytes>peak) peak=arena.live_bytes;
            if(!accepted) { fprintf(stderr,"row %zu: %s\n",rows,error); ok=false; }
            arena_free(&arena); if(!accepted) break;
        }
    }
    if(ferror(stream)) ok=false;
    struct rusage usage; getrusage(RUSAGE_SELF,&usage);
    long peak_rss=usage.ru_maxrss;
    /* Linux getrusage may include the spawning process's pre-exec peak.
     * VmHWM counts this executable; other systems keep getrusage. */
    FILE *status=fopen("/proc/self/status","r");
    if(status) {
        char entry[256];
        while(fgets(entry,sizeof(entry),status))
            if(sscanf(entry,"VmHWM: %ld kB",&peak_rss)==1) break;
        fclose(status);
    }
    printf("{\"rows\":%zu,\"bytes\":%zu,\"seconds\":%.6f,\"peak_rss_kib\":%ld,\"peak_value_bytes\":%zu,\"agreement\":%s}\n",rows,bytes,now()-start,peak_rss,peak,compare?"true":"false");
    free(line); fclose(stream); return ok;
}

/* Separate representation, emission and lookup costs; no timing assertions. */
static bool value_cost(size_t size) {
    const char prefix[] = "{\"payload\":\"";
    const char suffix[] = "\",\"needle\":1}";
    if (size > SIZE_MAX - sizeof(prefix) - sizeof(suffix)) return false;
    size_t length = sizeof(prefix) - 1u + size + sizeof(suffix) - 1u;
    char *text = malloc(length + 1u);
    if (!text) return false;
    memcpy(text, prefix, sizeof(prefix)-1u);
    memset(text + sizeof(prefix)-1u, 'x', size);
    memcpy(text + sizeof(prefix)-1u + size, suffix, sizeof(suffix));
    CettaJsonLibraryRuntimeV1 *library = cetta_json_library_runtime_v1_new(NULL, 0);
    bool ok = library != NULL;
    for (unsigned ordinary = 0; ok && ordinary < 2u; ordinary++) {
        Arena input; arena_init_detached(&input);
        Atom *source[] = {atom_string_n(&input, text, length)};
        double start = now();
        Atom *value = cetta_json_library_dispatch_v1(library, &input,
            atom_symbol(&input, ordinary ? "__cetta_lib_json_parse" : "__cetta_lib_json_parse_v1"), source, 1u);
        double elapsed = now() - start;
        ok = value && value->kind == ATOM_EXPR && atom_is_symbol(value->expr.elems[0], ordinary ? "JsonObject" : "JsonObjectV1");
        printf("{\"operation\":\"parse\",\"native_strings\":%s,\"payload_bytes\":%zu,\"seconds\":%.9f,\"arena_bytes\":%zu}\n",
            ordinary ? "true" : "false", size, elapsed, arena_accounted_live_bytes(&input));
        if (ok && ordinary) {
            const char *operations[] = {"__cetta_lib_json_stringify",
                "__cetta_lib_json_object_get", "__cetta_lib_json_project_v1"};
            for (unsigned operation = 0; ok && operation < 3u; operation++) {
                Arena output; arena_init_detached(&output);
                Atom *key = atom_string(&output, "needle");
                Atom *path_items[] = {key};
                Atom *args[] = {
                    operation == 2u ? source[0] : value,
                    operation == 2u
                        ? atom_expr(&output, path_items, 1u) : key,
                };
                start = now();
                Atom *result = cetta_json_library_dispatch_v1(library, &output,
                    atom_symbol(&output, operations[operation]), args,
                    operation ? 2u : 1u);
                elapsed = now() - start;
                ok = operation ? result && result->kind == ATOM_GROUNDED &&
                      result->ground.gkind == GV_INT && result->ground.ival == 1
                    : result && result->kind == ATOM_GROUNDED && result->ground.gkind == GV_STRING &&
                      atom_string_len(result) == length && !memcmp(result->ground.sval, text, length);
                printf("{\"operation\":\"%s\",\"native_strings\":true,\"payload_bytes\":%zu,\"seconds\":%.9f,\"arena_bytes\":%zu}\n",
                    operation == 0u ? "emit" : operation == 1u ? "lookup" : "project",
                    size, elapsed, arena_accounted_live_bytes(&output));
                arena_free(&output);
            }
            /* The unfused composition remains executable in the same binary,
             * so timing comparisons need no historical executable or parser. */
            for (unsigned operation = 0; ok && operation < 2u; operation++) {
                Arena temporary, output;
                arena_init_detached(&temporary); arena_init_detached(&output);
                Atom *canonical = NULL, *result = NULL;
                CettaJsonValueV1Status status;
                char error[256];
                start = now();
                ok = cetta_json_value_v1_from_ordinary(&temporary, value, 0u, 0u,
                    &canonical, &status, error, sizeof(error));
                if (ok && !operation) {
                    uint8_t *bytes = NULL; size_t size_out = 0u;
                    ok = cetta_json_value_v1_emit(cetta_json_direct_v1_number_valid, canonical, false, 0u, 0u, 0u,
                        &bytes, &size_out, &status, error, sizeof(error));
                    if (ok) result = atom_string_n(&output, (const char *)bytes, size_out);
                    free(bytes);
                } else if (ok) {
                    Atom *key = NULL;
                    ok = cetta_json_value_v1_from_ordinary(&temporary, atom_string(&temporary, "needle"),
                        0u, 0u, &key, &status, error, sizeof(error));
                    Atom *members = canonical->expr.elems[1];
                    for (CettaExprIndex at = 0u; ok && at < members->expr.len; at++) {
                        Atom *member = members->expr.elems[at];
                        if (atom_eq(member->expr.elems[2], key)) {
                            ok = cetta_json_value_v1_to_ordinary(&output, member->expr.elems[3],
                                0u, 0u, &result, &status, error, sizeof(error));
                            break;
                        }
                    }
                }
                elapsed = now() - start;
                ok = ok && (operation ? result && result->kind == ATOM_GROUNDED &&
                      result->ground.gkind == GV_INT && result->ground.ival == 1
                    : result && result->kind == ATOM_GROUNDED && result->ground.gkind == GV_STRING &&
                      atom_string_len(result) == length && !memcmp(result->ground.sval, text, length));
                printf("{\"operation\":\"%s-conversion-reference\",\"native_strings\":true,\"payload_bytes\":%zu,\"seconds\":%.9f,\"arena_bytes\":%zu}\n",
                    operation ? "lookup" : "emit", size, elapsed,
                    arena_accounted_live_bytes(&temporary) + arena_accounted_live_bytes(&output));
                arena_free(&output); arena_free(&temporary);
            }
        }
        arena_free(&input);
    }
    cetta_json_library_runtime_v1_free(library);
    free(text);
    return ok;
}

typedef struct { size_t count,refuse; double total; } NumericSinkTest;
static bool test_numeric_consume(void *raw,const uint8_t *bytes,size_t len,size_t ordinal) {
    NumericSinkTest *sink=raw; char text[64];
    CHECK(ordinal==sink->count); sink->count++;
    if(ordinal==sink->refuse || len>=sizeof(text)) return false;
    memcpy(text,bytes,len); text[len]='\0'; sink->total+=strtod(text,NULL); return true;
}
static void numeric_observer(void) {
    Arena arena; arena_init_detached(&arena);
    Atom *path=atom_expr(&arena,(Atom *[]){atom_string(&arena,"v")},1),*root=atom_expr(&arena,NULL,0);
    const char *sources[]={"{\"v\":[[1,2],[-3e0,4.0]],\"ignored\":\"λ\"}",
        "{\"v\":[[1,2],[3,4]],\"bad\":[}", "{\"v\":[[1],[2,3]]}", "{\"v\":[[1,2],[3,\"x\"]]}",
        "{\"missing\":[]}", "{\"v\":[[1,2],[3,4]],\"v\":\"ignored duplicate\"}"};
    for(size_t i=0;i<sizeof(sources)/sizeof(*sources);i++) {
        bool found=false; CettaJsonRuntimeV1Status status; char error[128]={0}; NumericSinkTest sink={.refuse=SIZE_MAX};
        bool ok=cetta_json_direct_v1_scalar_array((const uint8_t *)sources[i],strlen(sources[i]),path,
            (size_t[]){2,2},2,CETTA_JSON_SCALAR_NUMBER_V1,0,test_numeric_consume,&sink,&found,&status,error,sizeof(error));
        bool valid=i==0 || i>=4;
        CHECK(ok==valid); CHECK(found==(i==0 || i==5));
        CHECK(sink.count==(i==0 || i==5 ? 4u : 0u));
        if(i==0) CHECK(sink.total==4);
    }
    NumericSinkTest sink={.refuse=SIZE_MAX}; bool found; CettaJsonRuntimeV1Status status;
    CHECK(cetta_json_direct_v1_scalar_array((const uint8_t *)"-1.25",5,root,NULL,0,CETTA_JSON_SCALAR_NUMBER_V1,0,test_numeric_consume,&sink,&found,&status,NULL,0));
    CHECK(found && sink.count==1 && sink.total==-1.25);
    sink=(NumericSinkTest){.refuse=SIZE_MAX};
    CHECK(cetta_json_direct_v1_scalar_array((const uint8_t *)"[]",2,root,(size_t[]){0,99},2,CETTA_JSON_SCALAR_NUMBER_V1,0,test_numeric_consume,&sink,&found,&status,NULL,0));
    CHECK(found && !sink.count);
    sink=(NumericSinkTest){.refuse=1};
    CHECK(!cetta_json_direct_v1_scalar_array((const uint8_t *)"[1,2,3]",7,root,(size_t[]){3},1,CETTA_JSON_SCALAR_NUMBER_V1,0,test_numeric_consume,&sink,&found,&status,NULL,0));
    CHECK(!found && sink.count==2);
    sink=(NumericSinkTest){.refuse=SIZE_MAX};
    CHECK(!cetta_json_direct_v1_scalar_array((const uint8_t *)"[true,0]",8,root,(size_t[]){2},1,CETTA_JSON_SCALAR_BOOLEAN_V1,0,test_numeric_consume,&sink,&found,&status,NULL,0));
    CHECK(!sink.count && !found);
    CHECK(!cetta_json_direct_v1_scalar_array((const uint8_t *)"[true]",6,root,(size_t[]){1},1,CETTA_JSON_SCALAR_NUMBER_V1,0,test_numeric_consume,&sink,&found,&status,NULL,0));
    CHECK(!sink.count && !found);
    arena_free(&arena);
}

int main(int argc,char **argv) {
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols=&symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    bool values=argc==3 && !strcmp(argv[1],"--value-cost");
    bool fast_only=values || (argc==3 && (!strcmp(argv[1],"--bench") || !strcmp(argv[1],"--corpus-fast")));
    CettaJsonRuntimeV1 *runtime=fast_only?NULL:reference();
    bool ok=fast_only || runtime;
    if(ok && values) {
        char *end = NULL;
        unsigned long long size = strtoull(argv[2], &end, 10);
        ok = end && *end == '\0' && end != argv[2] && argv[2][0] != '-' && size <= SIZE_MAX;
        if (ok) ok = value_cost((size_t)size);
    }
    else if(ok && argc==1) { unit(runtime); numeric_observer(); printf("JsonDirectSummary %u %u\n",passed,failed); ok=!failed; }
    else if(ok && argc==3 && !strncmp(argv[1],"--corpus",8)) ok=corpus(runtime,argv[2],!fast_only);
    else if(ok && argc==3) ok=lines(runtime,argv[2],!fast_only);
    else ok=false;
    cetta_json_runtime_v1_free(runtime); g_symbols=NULL; symbol_table_free(&symbols); return ok?0:1;
}

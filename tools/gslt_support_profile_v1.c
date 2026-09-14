#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "gslt_support_profile_v1.h"
#include "native_sha256.h"
#include "symbol.h"
#include "gslt_native_emission_v1.h"

typedef struct { const char *manifest, *header, *source, *symbol, *include; } Options;

static bool options_read(int argc, char **argv, Options *options) {
    const char *names[] = {"--manifest", "--header", "--source", "--symbol", "--header-include"};
    const char **fields[] = {&options->manifest, &options->header, &options->source,
                             &options->symbol, &options->include};
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) return false;
        size_t j = 0;
        while (j < 5 && strcmp(argv[i], names[j])) j++;
        if (j == 5 || *fields[j] || !argv[i + 1][0]) return false;
        *fields[j] = argv[i + 1];
    }
    for (size_t j = 0; j < 5; j++) if (!*fields[j]) return false;
    if (!identifier(options->symbol)) return false;
    for (const unsigned char *p = (const unsigned char *)options->include; *p; p++)
        if (*p > 127 || !(isalnum(*p) || strchr("_./-", *p))) return false;
    return true;
}

static void emit_declarations(FILE *out, const char *symbol, const char *role,
                               const CettaGsltSupportOperatorDeclV1 *declarations,
                               size_t count) {
    if (!count) return;
    fprintf(out, "static const CettaGsltSupportOperatorDeclV1 %s_%s[] = {\n", symbol, role);
    for (size_t i = 0; i < count; i++) {
        fputs("    {.syntax_symbol = ", out); c_string(out, declarations[i].syntax_symbol);
        fprintf(out, ", .argument_count = %uu, .operator_id = ", declarations[i].argument_count);
        c_string(out, declarations[i].operator_id); fputs("},\n", out);
    }
    fputs("};\n\n", out);
}

static bool emit(const Options *options, const CettaGsltSupportTransformProfileV1 *p) {
    char *header = NULL, *source = NULL;
    size_t header_size = 0, source_size = 0;
    FILE *h = open_memstream(&header, &header_size), *c = open_memstream(&source, &source_size);
    if (!h || !c) {
        if (h) fclose(h);
        if (c) fclose(c);
        free(header); free(source); return false;
    }
    fprintf(h, "#ifndef CETTA_GENERATED_%s_H\n#define CETTA_GENERATED_%s_H\n\n",
            options->symbol, options->symbol);
    fprintf(h, "#include \"gslt_support_transform_runtime.h\"\n\nextern const CettaGsltSupportTransformProfileV1 %s;\n\n#endif\n", options->symbol);
    fprintf(c, "#include \"%s\"\n\n", options->include);
    emit_declarations(c, options->symbol, "sources", p->source_declarations, p->source_declaration_count);
    emit_declarations(c, options->symbol, "sinks", p->sink_declarations, p->sink_declaration_count);
    fprintf(c, "static const uint8_t %s_packet[] = {\n", options->symbol);
    for (size_t i = 0; i < p->physical_profile_packet_size; i++) {
        if (i % 16 == 0) fputs("    ", c);
        fprintf(c, "0x%02x,%s", p->physical_profile_packet[i],
                i % 16 == 15 || i + 1 == p->physical_profile_packet_size ? "\n" : " ");
    }
    fprintf(c, "};\n\nconst CettaGsltSupportTransformProfileV1 %s = {\n    .abi_version = 1u,\n", options->symbol);
#define TEXT(field) do { fputs("    ." #field " = ", c); c_string(c, p->field); fputs(",\n", c); } while (0)
#define NUMBER(field) fprintf(c, "    ." #field " = %uu,\n", p->field)
    TEXT(language_name); TEXT(profile_name); TEXT(manifest_sha256); TEXT(compiler_sha256);
    TEXT(work_symbol); TEXT(compat_input_symbol); TEXT(compat_input_operator_id);
    TEXT(explicit_input_symbol); TEXT(compat_output_symbol); TEXT(compat_output_operator_id);
    TEXT(explicit_output_symbol);
    NUMBER(work_arity); NUMBER(location_position); NUMBER(input_position); NUMBER(output_position);
#undef TEXT
#undef NUMBER
    fputs("    .scheduler = CETTA_GSLT_SUPPORT_SCHEDULER_LEAST_MORK_COMPACT_EXPRESSION_KEY_V1,\n"
          "    .unsupported_policy = CETTA_GSLT_SUPPORT_UNSUPPORTED_LEAVE_INERT,\n", c);
    if (p->source_declaration_count) fprintf(c, "    .source_declarations = %s_sources,\n", options->symbol);
    else fputs("    .source_declarations = NULL,\n", c);
    if (p->sink_declaration_count) fprintf(c, "    .sink_declarations = %s_sinks,\n", options->symbol);
    else fputs("    .sink_declarations = NULL,\n", c);
    fprintf(c, "    .source_declaration_count = %zuu,\n    .sink_declaration_count = %zuu,\n"
            "    .physical_profile_packet = %s_packet,\n"
            "    .physical_profile_packet_size = sizeof(%s_packet),\n};\n",
            p->source_declaration_count, p->sink_declaration_count, options->symbol, options->symbol);
    bool ok = !ferror(h) && !ferror(c);
    if (fclose(h)) ok = false;
    if (fclose(c)) ok = false;
    if (ok) ok = write_changed(options->header, header, header_size) &&
                 write_changed(options->source, source, source_size);
    free(header); free(source);
    return ok;
}

int main(int argc, char **argv) {
    Options options = {0};
    if (!options_read(argc, argv, &options)) {
        fputs("usage: gslt-support-profile-v1 --manifest FILE --header FILE --source FILE --symbol NAME --header-include NAME\n", stderr);
        return 2;
    }
    char *manifest = realpath(options.manifest, NULL);
    char *header = output_path(options.header), *source = output_path(options.source);
    bool ok = manifest && header && source && !same_file(header, source) &&
        !same_file(header, manifest) && !same_file(source, manifest) &&
        !same_file(header, "/proc/self/exe") && !same_file(source, "/proc/self/exe") &&
        !same_file(header, argv[0]) && !same_file(source, argv[0]);
    if (!ok) {
        fputs("error: output is absent or aliases another output or input\n", stderr);
        free(manifest); free(header); free(source); return 1;
    }
    options.header = header; options.source = source;
    size_t manifest_size = 0, executable_size = 0;
    unsigned char *bytes = read_bytes(manifest, &manifest_size);
    unsigned char *executable = read_bytes("/proc/self/exe", &executable_size);
    if (!executable) executable = read_bytes(argv[0], &executable_size);
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols; g_var_intern = &variables; g_hashcons = NULL;
    Arena arena;
    arena_init(&arena);
    char error[512] = {0}, compiler[65];
    CettaGsltSupportTransformProfileV1 profile = {0};
    ok = bytes && executable;
    if (!ok) snprintf(error, sizeof(error), "cannot read source or generator executable");
    if (ok) {
        /* Exact executable identity, not a source fingerprint or a proof. */
        cetta_native_sha256_hex(executable, executable_size, compiler);
        ok = cetta_gslt_support_profile_from_source_v1(&arena, bytes, manifest_size,
                compiler, &profile, error, sizeof(error));
    }
    if (ok && !emit(&options, &profile)) {
        snprintf(error, sizeof(error), "cannot write generated support profile"); ok = false;
    }
    if (!ok) fprintf(stderr, "error: %s\n", error);
    arena_free(&arena);
    g_var_intern = NULL; var_intern_free(&variables);
    g_symbols = NULL; symbol_table_free(&symbols);
    free(manifest); free(header); free(source); free(bytes); free(executable);
    return ok ? 0 : 1;
}

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

/* A selected source-to-ABI lowering for the existing RuleMachine consumer.
 * This is not a rule evaluator or a BNF representation. The input applicability
 * contracts are checked modulo bijective, rule-local variable renaming; the
 * instruction sequence is read from the actual output, never substituted. */
#include "native/operational_language_def_v1.h"
#include "native/gslt_composition_v1.h"
#include "native_sha256.h"
#include "symbol.h"
#include "gslt_native_emission_v1.h"
#include <inttypes.h>
#include <stdarg.h>

typedef struct { const char *core, *program, *out; } Options;
typedef struct { const char *role[8], *actual[8]; size_t count; Atom *code; } Bindings;
typedef struct { const char *name, *relation, *contract, *c_name; } Contract;

/* These patterns describe the consumer's admitted input and transport, not its
 * generated instructions. ?CODE captures the authored code subtree. */
static const Contract contracts[] = {
    {"compile-rule-program-axiom", "compile-rule-program-block",
     "(contract (compile-rule-program-block"
     " (bc-block ?id ?source (bc-match ?schema) (bc-goals rm-nil)"
     " (bc-build (rm-proof-atom ?proof)) bc-emit)"
     " (rule-program-block ?id ?source (rule-program-template ?schema) ?CODE)) (body))",
     "rule_program_axiom"},
    {"compile-rule-program-inverse-mp", "compile-rule-program-block",
     "(contract (compile-rule-program-block"
     " (bc-block ?id ?source (bc-match ?conclusion)"
     " (bc-goals (rm-cons (rm-premise ?left ?antecedent)"
     " (rm-cons (rm-premise ?right (imp ?antecedent ?conclusion)) rm-nil)))"
     " (bc-build (rm-proof-app ?proof (rm-cons ?left (rm-cons ?right rm-nil)))) bc-emit)"
     " (rule-program-block ?id ?source (rule-program-template rule-program-none) ?CODE)) (body))",
     "rule_program_inverse_mp"},
    {"compile-rule-program-source-block", "compile-rule-program-source",
     "(contract (compile-rule-program-source ?guest ?id ?program)"
     " (body (source-block-id ?guest ?id ?source) (compile-block ?source ?bytecode)"
     " (compile-rule-program-block ?bytecode ?program)))", NULL},
    {"compile-block-generic", "compile-block",
     "(contract (compile-block (rm-block ?id ?source ?proof ?premises ?conclusion)"
     " (bc-block ?id ?source (bc-match ?conclusion) (bc-goals ?premises)"
     " (bc-build ?proof) bc-emit)) (body))", NULL},
};

static bool fail(char *error, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format); vsnprintf(error, size, format, args); va_end(args);
    return false;
}

static bool form(const Atom *term, const char *name, size_t arity) {
    return term && term->kind == ATOM_EXPR && term->expr.len == arity + 1u &&
        term->expr.elems[0]->kind == ATOM_SYMBOL &&
        !strcmp(atom_name_cstr(term->expr.elems[0]), name);
}

static bool symbol(const Atom *term, const char *name) {
    return term && term->kind == ATOM_SYMBOL &&
        !strcmp(atom_name_cstr((Atom *)term), name);
}

/* Lift the neutral reader's structure directly into the existing composition
 * carrier. No namespace resolution, host evaluation or text round trip occurs.
 * The selected ABI admits only canonical nonnegative signed-64-bit literals. */
static Atom *lift(Arena *arena, const CettaOpLangV1SExpr *term) {
    if (!term) return NULL;
    switch (term->kind) {
    case CETTA_OP_LANG_V1_SEXPR_SYMBOL:
        return atom_symbol(arena, term->as.symbol);
    case CETTA_OP_LANG_V1_SEXPR_STRING: {
        if (memchr(term->as.string.bytes, 0, term->as.string.len)) return NULL;
        size_t size = term->as.string.len;
        if (size == SIZE_MAX) return NULL;
        char *copy = arena_alloc(arena, size + 1u);
        memcpy(copy, term->as.string.bytes, size);
        copy[size] = 0;
        return atom_string(arena, copy);
    }
    case CETTA_OP_LANG_V1_SEXPR_NATURAL: {
        const char *p = term->as.natural;
        uint64_t n = 0;
        if (!*p || (p[0] == '0' && p[1])) return NULL;
        for (; *p; ++p) {
            if (*p < '0' || *p > '9' || n > (INT64_MAX - (uint64_t)(*p - '0')) / 10u)
                return NULL;
            n = n * 10u + (uint64_t)(*p - '0');
        }
        return atom_int(arena, (int64_t)n);
    }
    case CETTA_OP_LANG_V1_SEXPR_APPLICATION: {
        size_t count = (size_t)term->as.application.argument_len + 1u;
        if (count > SIZE_MAX / sizeof(Atom *)) return NULL;
        Atom **items = malloc(count * sizeof(*items));
        if (!items) return NULL;
        items[0] = atom_symbol(arena, term->as.application.head);
        bool ok = true;
        for (size_t i = 1; i < count; ++i) {
            items[i] = lift(arena, term->as.application.arguments[i - 1u]);
            if (!items[i]) { ok = false; break; }
        }
        Atom *result = ok ? atom_expr(arena, items, count) : NULL;
        free(items); return result;
    }
    }
    return NULL;
}

static Atom *read_source(Arena *arena, const uint8_t *bytes, size_t size,
                         char *error, size_t error_size) {
    CettaOpLangV1Document document;
    cetta_op_lang_v1_document_init(&document);
    CettaOpLangV1Status status;
    bool ok = cetta_op_lang_v1_parse_commented_document_bytes(&document, bytes, size,
        UINT32_MAX, UINT32_MAX, &status, error, error_size);
    Atom *result = ok ? lift(arena, document.root) : NULL;
    cetta_op_lang_v1_document_free(&document);
    if (ok && !result) fail(error, error_size, "unsupported source scalar or allocation failure");
    return result;
}

static bool admit(const CettaGsltCompositionV1 *source, char *error, size_t size) {
    if (source->equation_count)
        return fail(error, size, "equational source compilation is outside this selected ABI");
    for (size_t i = 0; i < source->operator_count; ++i)
        for (size_t j = 0; j < i; ++j) {
            const CettaGsltOperatorV1 *a = &source->operators[i], *b = &source->operators[j];
            if (!strcmp(a->name, b->name) &&
                (a->arity != b->arity || !strcmp(a->presentation_name, b->presentation_name)))
                return fail(error, size, "duplicate or conflicting operator %s", a->name);
        }
    for (size_t i = 0; i < source->rewrite_count; ++i) {
        const CettaGsltRewriteV1 *r = &source->rewrites[i];
        if (!r->head || r->head->kind != ATOM_EXPR || !r->head->expr.len ||
            cetta_gslt_source_variable_v1(r->head->expr.elems[0]))
            return fail(error, size, "rule %s has an unsupported head", r->name);
        if (!cetta_gslt_composition_validate_term_v1(source, r->head, SIZE_MAX, error, size))
            return false;
        for (CettaExprIndex j = 1; j < r->body->expr.len; ++j) {
            Atom *goal = r->body->expr.elems[j];
            if (!goal || goal->kind != ATOM_EXPR || !goal->expr.len ||
                cetta_gslt_source_variable_v1(goal->expr.elems[0]))
                return fail(error, size, "rule %s has an unsupported goal", r->name);
            if (!cetta_gslt_composition_validate_term_v1(source, goal, SIZE_MAX, error, size))
                return false;
        }
    }
    return true;
}

static bool alpha_contract(const Atom *pattern, Atom *term, Bindings *bindings) {
    if (symbol(pattern, "?CODE")) {
        if (bindings->code) return false;
        bindings->code = term; return true;
    }
    if (cetta_gslt_source_variable_v1(pattern)) {
        if (!cetta_gslt_source_variable_v1(term)) return false;
        const char *role = cetta_gslt_source_variable_name_v1(pattern);
        const char *name = cetta_gslt_source_variable_name_v1(term);
        for (size_t i = 0; i < bindings->count; ++i) {
            if (!strcmp(role, bindings->role[i])) return !strcmp(name, bindings->actual[i]);
            if (!strcmp(name, bindings->actual[i])) return false;
        }
        if (bindings->count == sizeof(bindings->role) / sizeof(*bindings->role)) return false;
        bindings->role[bindings->count] = role;
        bindings->actual[bindings->count++] = name;
        return true;
    }
    if (!pattern || !term || pattern->kind != term->kind) return false;
    if (pattern->kind != ATOM_EXPR) return atom_eq((Atom *)pattern, term);
    if (pattern->expr.len != term->expr.len) return false;
    for (CettaExprIndex i = 0; i < pattern->expr.len; ++i)
        if (!alpha_contract(pattern->expr.elems[i], term->expr.elems[i], bindings)) return false;
    return true;
}

static bool reg(const Atom *term) {
    if (!term || term->kind != ATOM_SYMBOL) return false;
    const char *p = atom_name_cstr((Atom *)term);
    return p[0] == 'r' && p[1] >= '0' && p[1] <= '5' && !p[2];
}

/* The existing instruction consumer's operand contract. Ordering and operands
 * are retained as authored. Eligibility for its native shortcut is decided by
 * that consumer from the resulting instructions, independently of this tool. */
static bool instruction(const Atom *op, const char *proof, bool axiom) {
    if (symbol(op, "rmbc-emit")) return true;
    if (!op || op->kind != ATOM_EXPR || op->expr.len < 2) return false;
    Atom **a = op->expr.elems;
    if (form(op, "rmbc-require-fun", 2) || form(op, "rmbc-unify", 2))
        return reg(a[1]) && reg(a[2]);
    if (form(op, "rmbc-make-imp", 3) || form(op, "rmbc-make-fun", 3))
        return reg(a[1]) && reg(a[2]) && reg(a[3]);
    if (form(op, "rmbc-fresh", 1) || form(op, "rmbc-set-type", 1)) return reg(a[1]);
    if (form(op, "rmbc-instantiate-template", 2))
        return axiom && reg(a[1]) && symbol(a[2], "template0");
    if (form(op, "rmbc-proof-apply", 1) || form(op, "rmbc-proof-wrap", 1))
        return cetta_gslt_source_variable_v1(a[1]) &&
            !strcmp(cetta_gslt_source_variable_name_v1(a[1]), proof);
    if (form(op, "rmbc-hyp-add", 1))
        return a[1]->kind == ATOM_GROUNDED && a[1]->ground.gkind == GV_INT &&
            a[1]->ground.ival >= 0 && a[1]->ground.ival <= INT32_MAX;
    return false;
}

static bool code_admit(Atom *code, const Bindings *bindings, bool axiom,
                       char *error, size_t size) {
    const char *proof = NULL;
    for (size_t i = 0; i < bindings->count; ++i)
        if (!strcmp(bindings->role[i], "proof")) proof = bindings->actual[i];
    if (!proof || !form(code, "rule-program-code", 1))
        return fail(error, size, "missing code envelope or proof binder");
    Atom *cursor = code->expr.elems[1];
    size_t proofs = 0, count = 0;
    bool emitted = false;
    while (form(cursor, "rule-program-cons", 2)) {
        Atom *op = cursor->expr.elems[1];
        if (emitted || !instruction(op, proof, axiom))
            return fail(error, size, "unsupported instruction/operand or instruction after emit");
        if (form(op, "rmbc-proof-apply", 1) || form(op, "rmbc-proof-wrap", 1)) ++proofs;
        emitted = symbol(op, "rmbc-emit");
        ++count; cursor = cursor->expr.elems[2];
    }
    if (!symbol(cursor, "rule-program-nil") || !count || count > UINT32_MAX ||
        !emitted || proofs != 1)
        return fail(error, size, "code needs a proper nonempty list, one proof binding and final emit");
    return true;
}

static bool select_code(Arena *arena, const CettaGsltCompositionV1 *source,
                        Atom *code[2], char *error, size_t size) {
    const CettaGsltRewriteV1 *found[4] = {0};
    for (size_t i = 0; i < source->rewrite_count; ++i) {
        const CettaGsltRewriteV1 *r = &source->rewrites[i];
        const char *relation = atom_name_cstr(r->head->expr.elems[0]);
        bool selected_relation = false;
        size_t named = 4;
        for (size_t j = 0; j < 4; ++j) {
            if (!strcmp(relation, contracts[j].relation)) selected_relation = true;
            if (!strcmp(r->name, contracts[j].name)) named = j;
        }
        if (!selected_relation && named == 4) continue;
        if (named == 4 || found[named] || strcmp(relation, contracts[named].relation))
            return fail(error, size, "unaccounted or renamed compiler clause %s", r->name);
        found[named] = r;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (!found[i]) return fail(error, size, "missing compiler clause %s", contracts[i].name);
        Atom *pattern = read_source(arena, (const uint8_t *)contracts[i].contract,
                                    strlen(contracts[i].contract), error, size);
        if (!pattern) return false;
        Atom *parts[] = {atom_symbol(arena, "contract"), found[i]->head, found[i]->body};
        Bindings bindings = {0};
        if (!alpha_contract(pattern, atom_expr(arena, parts, 3), &bindings))
            return fail(error, size, "unsupported applicability, transport or ordered body in %s",
                        contracts[i].name);
        if (i < 2) {
            if (!code_admit(bindings.code, &bindings, i == 0, error, size)) return false;
            code[i] = bindings.code;
        }
    }
    return true;
}

static void emit_operand(FILE *out, const Atom *term) {
    if (!term) fputs("{RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}", out);
    else if (cetta_gslt_source_variable_v1(term))
        fputs("{RM_RULE_PROGRAM_OPERAND_PROOF, NULL, INT64_C(0)}", out);
    else if (term->kind == ATOM_SYMBOL) {
        fputs("{RM_RULE_PROGRAM_OPERAND_SYMBOL, ", out);
        c_string(out, atom_name_cstr((Atom *)term)); fputs(", INT64_C(0)}", out);
    } else fprintf(out, "{RM_RULE_PROGRAM_OPERAND_INT, NULL, INT64_C(%" PRId64 ")}",
                   term->ground.ival);
}

static bool emit_code(FILE *out, const char *name, Atom *code) {
    if (!identifier(name)) return false;
    fprintf(out, "static const RMRuleProgramGeneratedOp rm_generated_%s_ops[] = {\n", name);
    for (Atom *cursor = code->expr.elems[1]; form(cursor, "rule-program-cons", 2);
         cursor = cursor->expr.elems[2]) {
        Atom *op = cursor->expr.elems[1];
        size_t argc = op->kind == ATOM_EXPR ? op->expr.len - 1u : 0;
        const char *opcode = atom_name_cstr(argc ? op->expr.elems[0] : op);
        fputs("    {\n        .opcode = ", out); c_string(out, opcode);
        fprintf(out, ",\n        .nargs = %zu,\n        .operands = {\n", argc);
        for (size_t i = 0; i < 3; ++i) {
            fputs("            ", out);
            emit_operand(out, i < argc ? op->expr.elems[i + 1u] : NULL);
            fputs(",\n", out);
        }
        fputs("        },\n    },\n", out);
    }
    fprintf(out, "};\nstatic const uint32_t rm_generated_%s_op_count =\n"
            "    (uint32_t)(sizeof(rm_generated_%s_ops) /\n"
            "               sizeof(rm_generated_%s_ops[0]));\n\n", name, name, name);
    return !ferror(out);
}

static bool emit(const Options *options, Atom *code[2], const char *digest,
                  const char *core_sha, const char *program_sha) {
    char *text = NULL; size_t size = 0;
    FILE *out = open_memstream(&text, &size);
    if (!out) return false;
    fputs("/* Generated by the native selected RuleMachine source lowering. */\n"
          "/* Composition identity uses ordered GSLTCompositionV1, not the legacy package digest. */\n"
          "#ifndef CETTA_RULE_MACHINE_PROGRAM_V1_GENERATED_H\n"
          "#define CETTA_RULE_MACHINE_PROGRAM_V1_GENERATED_H\n\n", out);
    fprintf(out, "#define RM_RULE_PROGRAM_GSLT_DIGEST \"%s\"\n"
        "#define RM_RULE_PROGRAM_GSLT_IDENTITY \"HilbertBFCProgramSpecializationV1-%s\"\n"
        "#define RM_RULE_PROGRAM_CORE_SOURCE_SHA256 \"%s\"\n"
        "#define RM_RULE_PROGRAM_PROGRAM_SOURCE_SHA256 \"%s\"\n\n",
        digest, digest, core_sha, program_sha);
    bool ok = emit_code(out, contracts[0].c_name, code[0]) &&
              emit_code(out, contracts[1].c_name, code[1]);
    fputs("#endif\n", out);
    if (ferror(out)) ok = false;
    if (fclose(out)) ok = false;
    if (ok) ok = write_changed(options->out, text, size);
    free(text); return ok;
}

static bool options_read(int argc, char **argv, Options *options) {
    const char *names[] = {"--core", "--program-gslt", "--out"};
    const char **fields[] = {&options->core, &options->program, &options->out};
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 == argc) return false;
        size_t j = 0; while (j < 3 && strcmp(argv[i], names[j])) ++j;
        if (j == 3 || *fields[j] || !*argv[i + 1]) return false;
        *fields[j] = argv[i + 1];
    }
    return options->core && options->program && options->out;
}

int main(int argc, char **argv) {
    Options options = {0};
    if (!options_read(argc, argv, &options)) {
        fputs("usage: rule-machine-program-v1 --core FILE --program-gslt FILE --out HEADER\n", stderr);
        return 2;
    }
    char *out = output_path(options.out);
    if (!out || same_file(out, options.core) || same_file(out, options.program) ||
        same_file(out, "/proc/self/exe") || same_file(out, argv[0])) {
        free(out); fputs("error: output unavailable or aliases an input\n", stderr); return 1;
    }
    options.out = out;
    size_t core_size = 0, program_size = 0;
    uint8_t *core = read_bytes(options.core, &core_size);
    uint8_t *program = read_bytes(options.program, &program_size);
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    VarInternTable variables; var_intern_init(&variables); g_var_intern = &variables;
    g_hashcons = NULL;
    Arena arena; arena_init(&arena);
    CettaGsltCompositionV1 composition = {0};
    char error[512] = {0}, digest[65], core_sha[65], program_sha[65];
    bool ok = core && program;
    if (!ok) fail(error, sizeof(error), "cannot read source");
    Atom *sources[2] = {0}, *code[2] = {0};
    if (ok) {
        sources[0] = read_source(&arena, core, core_size, error, sizeof(error));
        if (sources[0]) sources[1] = read_source(&arena, program, program_size, error, sizeof(error));
        ok = sources[0] && sources[1];
    }
    if (ok) ok = cetta_gslt_composition_build_v1(sources, 2, &composition, error, sizeof(error));
    if (ok && (strcmp(composition.presentations[0].name, "RuleMachineCoreV1") ||
               strcmp(composition.presentations[1].name, "HilbertBFCProgramSpecializationV1")))
        ok = fail(error, sizeof(error), "unsupported source presentation identities");
    if (ok) ok = admit(&composition, error, sizeof(error)) &&
        select_code(&arena, &composition, code, error, sizeof(error)) &&
        cetta_gslt_composition_digest_v1(sources, 2, digest, error, sizeof(error));
    if (ok) {
        cetta_native_sha256_hex(core, core_size, core_sha);
        cetta_native_sha256_hex(program, program_size, program_sha);
        ok = emit(&options, code, digest, core_sha, program_sha);
        if (!ok) fail(error, sizeof(error), "cannot emit generated header");
    }
    if (!ok) fprintf(stderr, "RuleMachineProgramGenerationError: %s\n", error);
    cetta_gslt_composition_free_v1(&composition);
    arena_free(&arena); var_intern_free(&variables); symbol_table_free(&symbols);
    g_symbols = NULL; g_var_intern = NULL;
    free(core); free(program); free(out);
    return ok ? 0 : 1;
}

#define _GNU_SOURCE
#define CETTA_OP_LANG_PREPARE_HOST_V1 1
#include "native/operational_language_def_v1.c"
#include "native/prepared_artifact_v1.h"
#include <inttypes.h>

/* Build host for the fixed CettaTerm reader. Runtime imports these existing
 * parser-program carriers; it never rediscovers FIRST/FOLLOW or LR states. */
static void u32s(FILE *f, const char *name, const uint32_t *values, size_t n) {
    fprintf(f, "static const uint32_t %s[] = {", name);
    for (size_t i = 0; i < n; ++i) fprintf(f, "%" PRIu32 "u,", values[i]);
    fprintf(f, "%s};\n", n ? "" : "0");
}
static void words(FILE *f, const char *name, const uint64_t *values, size_t n) {
    fprintf(f, "static const uint64_t %s[] = {", name);
    for (size_t i = 0; i < n; ++i) fprintf(f, "UINT64_C(%" PRIu64 "),", values[i]);
    fprintf(f, "%s};\n", n ? "" : "0");
}
static void symbols(FILE *f, const char *name, const CettaLpNativeSymbol *values, size_t n) {
    fprintf(f, "static const CettaLpNativeSymbol %s[] = {", name);
    for (size_t i = 0; i < n; ++i)
        fprintf(f, "{%u,%uu,%uu},", values[i].kind, values[i].name, values[i].scope);
    fprintf(f, "%s};\n", n ? "" : "{0}");
}
static void emit(FILE *f, const CettaLpNativeGllProgram *g, const CettaLpNativeSlrProgram *s) {
    fprintf(f, "/* Generated fixed CettaTerm parser decisions; do not edit. */\n");
    u32s(f, "op_lang_nt_v1", g->nonterminals, g->nonterminal_len);
    u32s(f, "op_lang_tm_v1", g->terminals, g->terminal_len);
    fprintf(f, "static const uint8_t op_lang_nullable_v1[] = {");
    for (uint32_t i = 0; i < g->nonterminal_len; ++i) fprintf(f, "%u,", g->nullable[i]);
    fprintf(f, "};\n");
    words(f, "op_lang_first_v1", g->first, (size_t)g->nonterminal_len * g->word_len);
    words(f, "op_lang_follow_v1", g->follow, (size_t)g->nonterminal_len * g->word_len);
    for (uint32_t i = 0; i < g->production_len; ++i) {
        char name[64]; snprintf(name, sizeof(name), "op_lang_rhs_%u_v1", i);
        symbols(f, name, g->productions[i].rhs, g->productions[i].rhs_len);
    }
    fprintf(f, "static const CettaLpNativeProduction op_lang_productions_v1[] = {\n");
    for (uint32_t i = 0; i < g->production_len; ++i)
        fprintf(f, "{%uu,%uu,(CettaLpNativeSymbol *)op_lang_rhs_%u_v1,%uu},\n",
            g->productions[i].label, g->productions[i].lhs, i, g->productions[i].rhs_len);
    fprintf(f, "};\nstatic const CettaLpNativeGllProgram op_lang_gll_program_v1 = {\n"
        ".productions=(CettaLpNativeProduction *)op_lang_productions_v1,.production_len=%uu,"
        ".start_nonterminal=%uu,.nonterminals=(uint32_t *)op_lang_nt_v1,.nonterminal_len=%uu,"
        ".terminals=(uint32_t *)op_lang_tm_v1,.terminal_len=%uu,.nullable=(uint8_t *)op_lang_nullable_v1,"
        ".first=(uint64_t *)op_lang_first_v1,.follow=(uint64_t *)op_lang_follow_v1,.word_len=%uu};\n",
        g->production_len, g->start_nonterminal, g->nonterminal_len, g->terminal_len, g->word_len);
    symbols(f, "op_lang_glr_rhs_v1", s->rhs, s->rhs_len);
    fprintf(f, "static const CettaLpNativeSlrProgramProduction op_lang_glr_productions_v1[] = {\n");
    for (uint32_t i = 0; i < s->production_len; ++i) {
        const CettaLpNativeSlrProgramProduction *p = &s->productions[i];
        fprintf(f, "{%uu,%uu,%uu,%uu,%u,%u},\n", p->label, p->lhs, p->rhs_begin, p->rhs_len, p->authored, p->avoided);
    }
    fprintf(f, "};\nstatic const CettaLpNativeSlrProgramAction op_lang_actions_v1[] = {\n");
    for (uint32_t i = 0; i < s->action_len; ++i) fprintf(f, "{%u,%d},", s->actions[i].kind, s->actions[i].value);
    fprintf(f, "};\nstatic const CettaLpNativeGlrAction op_lang_glr_actions_v1[] = {\n");
    for (uint32_t i = 0; i < s->glr_action_len; ++i)
        fprintf(f, "{%uu,%uu,%u,%d},", s->glr_actions[i].state, s->glr_actions[i].token_idx,
            s->glr_actions[i].kind, s->glr_actions[i].value);
    fprintf(f, "};\n");
    u32s(f, "op_lang_gotos_v1", s->gotos, s->goto_len);
    fprintf(f, "static const CettaLpNativeSlrProgram op_lang_glr_program_v1 = {\n"
        ".start_nonterminal=%uu,.terminals=(uint32_t *)op_lang_tm_v1,.terminal_len=%uu,"
        ".nonterminals=(uint32_t *)op_lang_nt_v1,.nonterminal_len=%uu,"
        ".productions=(CettaLpNativeSlrProgramProduction *)op_lang_glr_productions_v1,"
        ".production_len=%uu,.authored_production_len=%uu,.rhs=(CettaLpNativeSymbol *)op_lang_glr_rhs_v1,.rhs_len=%uu,"
        ".actions=(CettaLpNativeSlrProgramAction *)op_lang_actions_v1,.action_len=%uu,"
        ".gotos=(uint32_t *)op_lang_gotos_v1,.goto_len=%uu,"
        ".glr_actions=(CettaLpNativeGlrAction *)op_lang_glr_actions_v1,.glr_action_len=%uu,"
        ".summary={%uu,%uu,%uu,%uu,%uu,%uu}};\n",
        s->start_nonterminal, s->terminal_len, s->nonterminal_len, s->production_len,
        s->authored_production_len, s->rhs_len, s->action_len, s->goto_len, s->glr_action_len,
        s->summary.state_len, s->summary.shift_len, s->summary.goto_len,
        s->summary.reduce_len, s->summary.accept_len, s->summary.conflict_len);
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    CettaLpNativeGrammar grammar = {0};
    CettaLpNativeGllPrepared gll = {0}; CettaLpNativeGlrPrepared glr = {0};
    CettaLpNativeGllProgram g = {0}; CettaLpNativeSlrProgram s = {0};
    char error[512] = {0}, *text = NULL; size_t len = 0;
    bool ok = op_lang_grammar_build(&grammar) &&
        cetta_lp_native_gll_prepare(&gll, &grammar, OP_LANG_NT_DOCUMENT, error, sizeof(error)) &&
        cetta_lp_native_glr_prepare(&glr, &grammar, OP_LANG_NT_DOCUMENT, error, sizeof(error)) &&
        cetta_lp_native_gll_prepared_export_program(&gll, &g, error, sizeof(error)) &&
        cetta_lp_native_glr_prepared_export_program(&glr, &s, error, sizeof(error));
    /* Check the exact exported pair before emitting its C representation. */
    CettaLpNativeGllPrepared restored_gll = {0}; CettaLpNativeGlrPrepared restored_glr = {0};
    if (ok) ok = cetta_lp_native_gll_prepared_import_program(&restored_gll, &g, error, sizeof(error)) &&
        cetta_lp_native_glr_prepared_import_program(&restored_glr, &s, &g, error, sizeof(error));
    if (ok) {
        FILE *f = open_memstream(&text, &len);
        if (!f) ok = false;
        else { emit(f, &g, &s); bool failed = ferror(f); if (fclose(f) || failed) ok = false; }
    }
    if (ok) ok = cetta_prepared_artifact_v1_publish(argv[1], (const uint8_t *)text, len, error, sizeof(error));
    if (!ok) fprintf(stderr, "emit-operational-reader: %s\n", error);
    free(text); cetta_lp_native_gll_prepared_free(&restored_gll); cetta_lp_native_glr_prepared_free(&restored_glr);
    cetta_lp_native_gll_program_free(&g); cetta_lp_native_slr_program_free(&s);
    cetta_lp_native_gll_prepared_free(&gll); cetta_lp_native_glr_prepared_free(&glr);
    cetta_lp_native_grammar_free(&grammar);
    return ok ? 0 : 1;
}

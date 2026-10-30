#include "cesta.h"

#include <stdlib.h>
#include <string.h>

CeText ce_builtin_CompactCModule(void);
CeText ce_builtin_CompactC2Module(void);
CeText ce_builtin_CMeraStyleModule(void);
CeText ce_builtin_TheoryModuleSyntax(void);

bool ce_builtin_view(const CeGrammar *g, const char *key, CeViewProfile *out, CeError *error) {
    bool ast = !strcmp(key,"ast/1");
    bool cmera = !strcmp(key,"cmera-style/1");
    bool compact2 = !strcmp(key,"compact-c/2");
    if (!ast && !cmera && !compact2 && strcmp(key,"compact-c/1")) return ce_error(error,0,"unknown presentation or version: %s",key);
    CeGrammar syntax = {0}; CeCatalog catalog = {.syntax=&syntax}; CeModule *module;
    CePresentation source = {0}; bool ok = false;
    if (!ast) {
        if (!ce_grammar_read(&syntax,ce_builtin_TheoryModuleSyntax(),error) || !ce_grammar_finish(&syntax,error)) goto done;
        if (compact2 ? !ce_module_read(&catalog,ce_cstr(&catalog.arena,"CompactC2.module"),ce_builtin_CompactC2Module(),&module,error) :
            !ce_module_read(&catalog,ce_cstr(&catalog.arena,"CompactC.module"),ce_builtin_CompactCModule(),&module,error)) goto done;
        if (cmera && !ce_module_read(&catalog,ce_cstr(&catalog.arena,"CMeraStyle.module"),ce_builtin_CMeraStyleModule(),&module,error)) goto done;
        if (!ce_module_check(&catalog,module,error) ||
            !ce_module_theory(&catalog,module,cmera ? "CMeraStyle" : compact2 ? "CompactC2" : "CompactC",&source,error)) goto done;
    }
    ok = ce_view_compile(out,g,&catalog,&source,ast ? "ast" : cmera ? "cmera-style" : "compact-c",compact2 ? 2 : 1,error);
done:
    ce_catalog_free(&catalog); ce_grammar_free(&syntax); return ok;
}

static bool document(const CeAtom *atom) {
    return atom->kind == CE_ATOM_LIST && atom->len && atom->items[0]->kind == CE_ATOM_SYMBOL && ce_is(atom->items[0]->text,"cesta");
}
static bool decode(const CeGrammar *g, const CeAtom *atom, const char *declared,
                    const char *fallback, CeArena *arena, CeTree **out, CeError *error) {
    const char *key = declared; const CeAtom *payload = atom;
    if (document(atom)) {
        if (atom->len != 6 || atom->items[1]->kind != CE_ATOM_SYMBOL)
            return ce_error(error,atom->offset,"malformed versioned Cesta document");
        if (!key) key = (const char *)atom->items[1]->text.data;
    }
    if (!key) key = fallback;
    CeViewProfile profile = {0}; bool ok = ce_builtin_view(g,key,&profile,error);
    if (ok && document(atom)) ok = ce_view_document_payload(&profile,g->entries[0],atom,&payload,error);
    if (ok) ok = !strcmp(key,"ast/1") ?
        ce_tree_decode_atom(g,payload,g->entries[0],-1,arena,NULL,NULL,out,error,0) :
        ce_view_expand(&profile,payload,g->entries[0],arena,out,NULL,error);
    ce_view_free(&profile); return ok;
}
bool ce_builtin_decode(const CeGrammar *g, CeText source, const char *key,
                        CeArena *arena, CeTree **out, CeError *error) {
    return ce_builtin_decode_default(g,source,key,"ast/1",arena,out,error);
}
bool ce_builtin_decode_default(const CeGrammar *g, CeText source, const char *key,
                                const char *fallback, CeArena *arena, CeTree **out, CeError *error) {
    CeAtom *atom;
    if (!ce_atom_read(arena,source,&atom,error)) return false;
    if (ce_wire_envelope(atom) && !ce_wire_unpack(arena,atom,&atom,error)) return false;
    return decode(g,atom,key,fallback,arena,out,error);
}

static bool source_tree(const CeGrammar *g, CeText source, CeArena *arena, CeTree **out,
                        uint64_t *payload, CeError *error) {
    CeDFA dfa = {0}; CeLR lr = {0}; CeTokens tokens = {0}; CeForest forest = {.root=-1};
    bool ok = ce_dfa_prepare(g,&dfa,error) && ce_lr_prepare(g,g->entries[0],&lr,error) &&
        ce_dfa_lex(g,&dfa,source,&tokens,error) && ce_lr_parse(g,&lr,&tokens,&forest,error) &&
        ce_tree_from_forest(g,&forest,&tokens,arena,out,error);
    if (ok && payload) for (size_t i = 0; i < tokens.len; ++i) *payload += tokens.data[i].text.len;
    ce_forest_free(&forest); ce_tokens_free(&tokens); ce_lr_free(&lr); ce_dfa_free(&dfa); return ok;
}
static uint64_t atom_payload(const CeAtom *atom) {
    if (atom->kind == CE_ATOM_SYMBOL) return atom->text.len;
    uint64_t bytes = 2;
    if (atom->kind == CE_ATOM_TEXT) for (size_t i = 0; i < atom->text.len; ++i) {
        unsigned c = atom->text.data[i];
        bytes += c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t' ? 2 : c < 32 || c >= 127 ? 4 : 1;
    } else for (size_t i = 0; i < atom->len; ++i) bytes += atom_payload(atom->items[i]);
    return bytes;
}
static bool stats_file(const char *path, const CeViewProfile *profile, const CeViewStats *stats,
                        const CeAtom *surface, const CeAtom *envelope, bool from_c,
                        uint64_t c_payload, CeError *error) {
    FILE *file = fopen(path,"wb");
    if (!file) return ce_error(error,0,"cannot open presentation statistics output");
    fputs("{\"format\":\"cesta.presentation-statistics.v1\",\"presentation\":",file);
    ce_quote(file,profile->name);
    fprintf(file,",\"version\":%u,\"constructors\":%llu,\"shortened\":%llu,\"ordered_lists\":%llu,\"lexical_tokens\":%llu,\"payload_bytes\":%llu,\"envelope_bytes\":%llu,\"C_payload_bytes\":",
        profile->version,(unsigned long long)stats->constructors,(unsigned long long)stats->shortened,
        (unsigned long long)stats->lists,(unsigned long long)stats->tokens,
        (unsigned long long)atom_payload(surface),(unsigned long long)(atom_payload(envelope)-atom_payload(surface)));
    if (from_c) fprintf(file,"%llu",(unsigned long long)c_payload); else fputs("null",file);
    fputs(",\"by_constructor\":[",file); bool first = true;
    for (size_t i = 0; i < stats->capacity; ++i) if (stats->by_constructor[i]) {
        if (!first) fputc(',',file);
        first = false;
        fputs("{\"name\":",file); ce_quote(file,profile->grammar->productions[i].label);
        fprintf(file,",\"occurrences\":%llu,\"shortened\":%llu}",
            (unsigned long long)stats->by_constructor[i],(unsigned long long)stats->short_by_constructor[i]);
    }
    fputs("]}\n",file); bool ok = !ferror(file); ok = fclose(file) == 0 && ok;
    return ok || ce_error(error,0,"cannot write presentation statistics output");
}
static bool suffix(const char *name, const char *ending) {
    size_t a = strlen(name), b = strlen(ending); return a >= b && !strcmp(name+a-b,ending);
}
int ce_view_command(int argc, char **argv) {
    const char *output_key = NULL, *input_key = NULL, *path = NULL, *statistics_path = NULL;
    bool gnu = true, statistics = false, portable = false;
    CeArena arena = {0}; CeGrammar g = {0}; CeViewProfile profile = {0}; CeViewStats stats = {0};
    CeError error = {{0},0}; CeText source = {NULL,0}; int result = 1;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i],"--as") || !strcmp(argv[i],"--to") || !strcmp(argv[i],"--from")) {
            bool from = !strcmp(argv[i],"--from");
            if (++i == argc) { ce_error(&error,0,"presentation option requires its versioned key"); goto done; }
            if (from) input_key = argv[i]; else output_key = argv[i];
        } else if (!strcmp(argv[i],"--std=c11")) gnu = false;
        else if (!strcmp(argv[i],"--std=gnu11")) gnu = true;
        else if (!strcmp(argv[i],"--stats")) statistics = true;
        else if (!strcmp(argv[i],"--wire=portable/1")) portable = true;
        else if (!strcmp(argv[i],"--stats-json")) {
            if (++i == argc) { ce_error(&error,0,"statistics option requires its output path"); goto done; }
            statistics_path = argv[i];
        }
        else if (argv[i][0] == '-' && strcmp(argv[i],"-")) { ce_error(&error,0,"unknown presentation command option"); goto done; }
        else if (path) { ce_error(&error,0,"presentation command takes one input file"); goto done; }
        else path = argv[i];
    }
    if (!output_key) { ce_error(&error,0,"presentation command requires --as or --to with an explicit version"); goto done; }
    if (!ce_builtin_language(&g,gnu,&error) || !ce_builtin_view(&g,output_key,&profile,&error)) goto done;
    if (!strcmp(argv[1],"export-view-rules")) {
        if (path) { ce_error(&error,0,"rule export does not take an input file"); goto done; }
        if (!ce_view_rules_export(&profile,stdout,&error)) goto done;
    } else {
        if (!path) { ce_error(&error,0,"presentation command requires an input file"); goto done; }
        if (!ce_read_file(&arena,path,&source,&error)) goto done;
        CeTree *tree; CeAtom *surface; uint64_t c_payload = 0;
        bool c = (input_key && !strcmp(input_key,"c")) ||
            (!input_key && !strcmp(argv[1],"view") && (suffix(path,".c") || suffix(path,".i")));
        if (c) {
            if (!source_tree(&g,source,&arena,&tree,&c_payload,&error)) goto done;
        } else if (!ce_builtin_decode(&g,source,input_key,&arena,&tree,&error)) goto done;
        if (!strcmp(output_key,"ast/1")) surface = ce_tree_to_atom(&g,tree,&arena);
        else if (!ce_view_tree(&profile,tree,g.entries[0],&arena,&surface,&error)) goto done;
        CeTree *expanded;
        if (!ce_view_expand(&profile,surface,g.entries[0],&arena,&expanded,&stats,&error) || !ce_tree_equal(&g,tree,expanded)) {
            if (!error.message[0]) ce_error(&error,0,"presentation expansion changed the AST");
            goto done;
        }
        CeAtom *sealed = ce_view_document(&profile,g.entries[0],surface,&arena);
        if (statistics_path && !stats_file(statistics_path,&profile,&stats,surface,sealed,c,c_payload,&error)) goto done;
        if (portable && !ce_wire_pack(&arena,sealed,&sealed,&error)) goto done;
        ce_atom_write(sealed,stdout); fputc('\n',stdout);
        if (statistics) fprintf(stderr,"AST constructors: %llu; covered by short forms: %llu; ordered lists: %llu; lexical tokens: %llu\n",
            (unsigned long long)stats.constructors,(unsigned long long)stats.shortened,
            (unsigned long long)stats.lists,(unsigned long long)stats.tokens);
    }
    result = ferror(stdout) ? 1 : 0;
done:
    if (result && error.message[0]) fprintf(stderr,"%s: byte %zu: %s\n",path ? path : "cesta",error.offset,error.message);
    ce_view_stats_free(&stats); ce_view_free(&profile); ce_grammar_free(&g); ce_arena_free(&arena); return result;
}

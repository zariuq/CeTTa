#define _POSIX_C_SOURCE 200809L
#include "native.h"
#include "macro.h"
#include "protocol.h"
#include "checked.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool head(const CeAtom *a, const char *name) {
    return a->kind == CE_ATOM_LIST && a->len &&
        a->items[0]->kind == CE_ATOM_SYMBOL && ce_is(a->items[0]->text,name);
}

bool ce_native_document(const CeAtom *a) {
    return ce_checked_document(a) || (head(a,"cesta") && a->len >= 2 &&
        a->items[1]->kind == CE_ATOM_SYMBOL && ce_is(a->items[1]->text,"native-c/1"));
}

static bool bytes(FILE *f, CeText text) {
    return fwrite(text.data,1,text.len,f) == text.len;
}

/* Directive operands are C spelling, not grounded MeTTa values. Physical
   newlines require an explicit native fragment; they cannot inject directives
   into a structured single-line form. */
static bool operand(const CeAtom *a, FILE *f, CeError *error) {
    if (a->kind != CE_ATOM_SYMBOL && a->kind != CE_ATOM_TEXT)
        return ce_error(error,a->offset,"directive operand requires C spelling as a symbol or string");
    for (size_t i = 0; i < a->text.len; ++i)
        if (!a->text.data[i] || a->text.data[i] == '\n' || a->text.data[i] == '\r')
            return ce_error(error,a->offset,"directive operand must occupy one physical line");
    if (a->text.len && a->text.data[a->text.len-1] == '\\')
        return ce_error(error,a->offset,"directive operand cannot end in a line splice");
    return bytes(f,a->text);
}

static bool identifier(const CeAtom *a, FILE *f, CeError *error) {
    if (a->kind != CE_ATOM_SYMBOL || !a->text.len)
        return ce_error(error,a->offset,"directive name requires an identifier symbol");
    for (size_t i = 0; i < a->text.len; ++i) {
        unsigned c = a->text.data[i];
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (i && c >= '0' && c <= '9') || c >= 128))
            return ce_error(error,a->offset,"invalid directive identifier");
    }
    return bytes(f,a->text);
}

static bool finish(CeArena *arena, FILE *file, char **buffer, size_t *length,
                    CeText *out, CeError *error) {
    bool ok = !ferror(file);
    if (fclose(file)) ok = false;
    if (ok) *out = ce_text(arena,*buffer,*length);
    free(*buffer);
    return ok || ce_error(error,0,"cannot finish native C output");
}

typedef struct {
    const CeAtom *atom;
    size_t begin, end;
    CeTree *site;
    bool protocol;
    CeText text;
    CeText *dependencies;
    size_t ndependencies;
} NativeImport;
typedef struct {
    const CeGrammar *grammar; CeMacros *macros;
    NativeImport *imports; size_t nimports;
    CeLazyLexer *lexer;
    const CeLineMarks *marks;   /* compile path: branches carry #line too */
    const CeAtom *ownership;
} Native;

static bool accepts(void *context, const CeAtom *a, int sort) {
    static const char *names[] = {"include","define","undef","cpp-if","cpp-ifdef","cpp-ifndef","comment","native","use-macros","use-protocols","ownership"};
    Native *native = context;
    const CeSymbol *symbol = &native->grammar->symbols[sort];
    if (symbol->kind != CE_NONTERMINAL || symbol->list_element >= 0) return false;
    if (ce_macros_accepts(native->macros,a,sort)) return true;
    for (size_t i = 0; i < sizeof(names)/sizeof(*names); ++i) if (head(a,names[i])) return true;
    return false;
}

static bool branch(Native *native, const CeAtom *a, size_t begin, int sort, int parent,
                     CeArena *arena, FILE *file, CeAtomDecoder recurse, void *context,
                     CeError *error, size_t depth) {
    for (size_t i = begin; i < a->len; ++i) {
        CeTree *tree;
        if (!recurse(context,a->items[i],sort,parent,arena,&tree,error,depth+1)) return false;
        bool printed = native->marks
            ? ce_tree_print_marked(native->grammar,native->lexer,tree,sort,native->marks,file,error)
            : ce_tree_print_open_shared(native->grammar,native->lexer,tree,sort,file,error);
        if (!printed) return false;
        fputc('\n',file);
    }
    return true;
}

static bool conditional(Native *native, const CeAtom *a, int sort, int parent,
                          CeArena *arena, FILE *file, CeAtomDecoder recurse, void *context,
                          CeError *error, size_t depth) {
    if (a->len < 3 || !head(a->items[2],"then"))
        return ce_error(error,a->offset,"conditional requires an operand and a (then ...) branch");
    const char *word = head(a,"cpp-if") ? "if" : head(a,"cpp-ifdef") ? "ifdef" : "ifndef";
    fprintf(file,"#%s ",word);
    if (!(head(a,"cpp-if") ? operand(a->items[1],file,error) : identifier(a->items[1],file,error))) return false;
    fputc('\n',file);
    if (!branch(native,a->items[2],1,sort,parent,arena,file,recurse,context,error,depth)) return false;
    bool otherwise = false;
    for (size_t i = 3; i < a->len; ++i) {
        const CeAtom *part = a->items[i]; size_t begin;
        if (otherwise) return ce_error(error,part->offset,"else must be the last conditional branch");
        if (head(part,"elif") && part->len >= 2) {
            fputs("#elif ",file);
            if (!operand(part->items[1],file,error)) return false;
            fputc('\n',file); begin = 2;
        } else if (head(part,"else")) {
            fputs("#else\n",file); otherwise = true; begin = 1;
        } else return ce_error(error,part->offset,"expected (elif CONDITION ...) or (else ...) branch");
        if (!branch(native,part,begin,sort,parent,arena,file,recurse,context,error,depth)) return false;
    }
    return fputs("#endif\n",file) >= 0;
}

static bool form(Native *native, const CeAtom *a, int sort, int parent, CeArena *arena,
                  FILE *file, CeAtomDecoder recurse, void *context, CeError *error, size_t depth) {
    if (head(a,"ownership")) {
        if (native->ownership != a)
            return ce_error(error,a->offset,"ownership declaration must be an unconditional unit form");
        CeText position = native->grammar->symbols[native->grammar->symbols[sort].sort].name;
        if (!ce_is(position,"ExternalDeclaration"))
            return ce_error(error,a->offset,"ownership declaration requires translation-unit scope");
        return fputs("#if defined(__SIZEOF_POINTER__) && defined(__SIZEOF_LONG__)\n"
            "_Static_assert(__SIZEOF_POINTER__ == 8 && __SIZEOF_LONG__ == 8, "
            "\"ownership ABI profile requires x86_64 SysV\");\n#endif\n"
            "_Static_assert((unsigned char)256 == 0 && (char)-1 < 0 && "
            "sizeof(void *) == 8 && sizeof(long) == 8 && "
            "sizeof(int) == 4 && sizeof(short) == 2 && sizeof(long long) == 8 && "
            "sizeof(float) == 4 && sizeof(double) == 8 && sizeof(long double) == 16 && "
            "sizeof(enum { __cesta_ownership_abi_enum = 1 }) == 4 && "
            "_Alignof(void *) == 8 && _Alignof(long) == 8 && "
            "_Alignof(long double) == 16 && sizeof(struct { char a; long b; }) == 16 && "
            "sizeof(struct { char a; long double b; }) == 32, "
            "\"ownership ABI profile requires x86_64 SysV\");\n",file) >= 0;
    }
    if (head(a,"use-macros") || head(a,"use-protocols")) {
        CeText position = native->grammar->symbols[native->grammar->symbols[sort].sort].name;
        return ((native->macros || head(a,"use-protocols")) && ce_is(position,"ExternalDeclaration")) ||
            ce_error(error,a->offset,"module imports must occur directly in the unit");
    }
    if (head(a,"cpp-if") || head(a,"cpp-ifdef") || head(a,"cpp-ifndef"))
        return conditional(native,a,sort,parent,arena,file,recurse,context,error,depth);
    if (head(a,"define")) {
        if (a->len != 3) return ce_error(error,a->offset,"define requires a name or parameter list and replacement spelling");
        fputs("#define ",file); const CeAtom *name = a->items[1];
        if (name->kind == CE_ATOM_LIST) {
            if (!name->len) return ce_error(error,name->offset,"function-like define requires a macro name");
            if (!identifier(name->items[0],file,error)) return false;
            fputc('(',file);
            for (size_t i = 1; i < name->len; ++i) {
                if (i > 1) fputc(',',file);
                if (name->items[i]->kind == CE_ATOM_SYMBOL && ce_is(name->items[i]->text,"...")) {
                    if (i+1 != name->len) return ce_error(error,name->items[i]->offset,"variadic parameter must be last");
                    fputs("...",file);
                } else if (!identifier(name->items[i],file,error)) return false;
            }
            fputc(')',file);
        } else if (!identifier(name,file,error)) return false;
        fputc(' ',file);
        if (!operand(a->items[2],file,error)) return false;
        return fputc('\n',file) != EOF;
    }
    if (a->len != 2) return ce_error(error,a->offset,"native source form requires one operand");
    const CeAtom *value = a->items[1];
    if (head(a,"include")) {
        fputs("#include ",file);
        if (value->kind == CE_ATOM_TEXT) {
            if (memchr(value->text.data,'"',value->text.len) || memchr(value->text.data,'\\',value->text.len))
                return ce_error(error,value->offset,"quoted include requires an unescaped header name");
            fputc('"',file);
            if (!operand(value,file,error)) return false;
            fputc('"',file);
        } else if (!operand(value,file,error)) return false;
        return fputc('\n',file) != EOF;
    }
    if (head(a,"undef")) {
        fputs("#undef ",file);
        return identifier(value,file,error) && fputc('\n',file) != EOF;
    }
    if (value->kind != CE_ATOM_TEXT) return ce_error(error,value->offset,"native and comment forms require a string");
    if (memchr(value->text.data,0,value->text.len)) return ce_error(error,value->offset,"NUL byte in native C source");
    if (head(a,"comment")) {
        for (size_t i = 1; i < value->text.len; ++i)
            if (value->text.data[i-1] == '*' && value->text.data[i] == '/')
                return ce_error(error,value->offset+i-1,"comment text contains its closing delimiter");
        fputs("/*",file); bytes(file,value->text); fputs("*/\n",file); return true;
    }
    /* Opaque expression fragments are grouped so they cannot change the
       surrounding grammar production's operator precedence. */
    CeText sort_name = native->grammar->symbols[native->grammar->symbols[sort].sort].name;
    bool expression = ce_is(sort_name,"Expr");
    if (expression) fputc('(',file);
    bytes(file,value->text);
    if (expression) fputc(')',file);
    fputc('\n',file); return true;
}

static unsigned extension(void *context, const CeAtom *a, int sort, int parent,
                            CeArena *arena, CeTree **out, CeAtomDecoder recurse,
                            void *recurse_context, CeError *error, size_t depth) {
    Native *native = context;
    unsigned macro = ce_macros_expand(native->macros,a,sort,parent,arena,out,recurse,recurse_context,error,depth);
    if (macro != CE_EXTENSION_UNHANDLED) return macro;
    static const char *names[] = {"include","define","undef","cpp-if","cpp-ifdef","cpp-ifndef","comment","native","use-macros","use-protocols","ownership"};
    bool known = false;
    for (size_t i = 0; i < sizeof(names)/sizeof(*names); ++i) known |= head(a,names[i]);
    if (!known) return CE_EXTENSION_UNHANDLED;
    const CeSymbol *symbol = &native->grammar->symbols[sort];
    if (symbol->kind != CE_NONTERMINAL || symbol->list_element >= 0) {
        ce_error(error,a->offset,"source form requires a C syntax position, not a token or list slot");
        return CE_EXTENSION_ERROR;
    }
    char *buffer = NULL; size_t length = 0; FILE *file = open_memstream(&buffer,&length);
    if (!file) { ce_error(error,a->offset,"cannot allocate native C output"); return CE_EXTENSION_ERROR; }
    bool ok = form(native,a,sort,parent,arena,file,recurse,recurse_context,error,depth);
    CeText spelling;
    if (!ok) { fclose(file); free(buffer); return CE_EXTENSION_ERROR; }
    if (!finish(arena,file,&buffer,&length,&spelling,error)) return CE_EXTENSION_ERROR;
    CeTree *tree = ce_alloc(arena,sizeof(*tree));
    tree->symbol = sort; tree->production = CE_TREE_SOURCE; tree->offset = a->offset; tree->spelling = spelling;
    if (head(a,"use-macros") || head(a,"use-protocols"))
        for (size_t i = 0; i < native->nimports; ++i)
            if (native->imports[i].atom == a) native->imports[i].site = tree;
    *out = tree; return CE_EXTENSION_EXPANDED;
}

bool ce_native_text_at(const CeGrammar *g, CeArena *arena, CeText source,
                       const char *presentation, const char *identity,
                       const CeNativeEnvironment *environment,
                       CeNativeOutput *out, CeError *error) {
    *out = (CeNativeOutput){0};
    CeAtom *atom;
    if (!ce_atom_read(arena,source,&atom,error)) return false;
    if (ce_wire_envelope(atom) && !ce_wire_unpack(arena,atom,&atom,error)) return false;
    if (ce_checked_document(atom)) {
        if (presentation && strcmp(presentation,"native-c/1") && strcmp(presentation,"checked-c/1"))
            return ce_error(error,atom->offset,"checked document disagrees with the requested presentation");
        return ce_checked_text(arena,source,identity,environment && environment->lines,
                               environment ? environment->plan : NULL,out,error);
    }
    const char *key = "compact-c/2";
    if (ce_native_document(atom)) {
        if (atom->len != 4 || atom->items[2]->kind != CE_ATOM_SYMBOL)
            return ce_error(error,atom->offset,"native document requires (cesta native-c/1 PRESENTATION BODY)");
        key = (const char *)atom->items[2]->text.data; atom = atom->items[3];
        if (presentation && strcmp(presentation,"native-c/1"))
            return ce_error(error,atom->offset,"native document disagrees with the requested presentation");
    } else if (!presentation || strcmp(presentation,"native-c/1"))
        return ce_error(error,atom->offset,"native source forms require a native-c/1 document or presentation selection");
    CeViewProfile profile = {0}; CeTree *tree; CeLazyLexer own = {0};
    Native native = {.grammar=g,.lexer=environment && environment->lexer ? environment->lexer : &own};
    if (head(atom,"unit")) native.imports = ce_alloc(arena,atom->len*sizeof(*native.imports));
    /* Conditional branches print while expanding; their statements carry
       #line directives of their own (lines, without a token map). */
    CeLineMarks branch_marks = {0};
    if (environment && environment->lines && identity) {
        branch_marks = (CeLineMarks){.sorts=ce_native_line_sorts(g,arena),.source=source,.path=identity};
        native.marks = &branch_marks;
    }
    bool ok = true;
    if (head(atom,"unit")) for (size_t i = 1; i < atom->len && ok; ++i) {
        const CeAtom *use = atom->items[i];
        if (head(use,"ownership")) {
            if (out->ownership) { ok = ce_error(error,use->offset,"duplicate ownership declaration"); break; }
            out->ownership = native.ownership = use; continue;
        }
        bool protocol = head(use,"use-protocols");
        if (!head(use,"use-macros") && !protocol) continue;
        if (use->len != 3 || use->items[1]->kind != CE_ATOM_TEXT || use->items[2]->kind != CE_ATOM_SYMBOL) {
            ok = ce_error(error,use->offset,"module import requires a filename and theory name"); break;
        }
        const char *name = (const char *)use->items[1]->text.data;
        const char *path = name;
        if (strlen(name) != use->items[1]->text.len) {
            ok = ce_error(error,use->offset,"module filename contains a null byte"); break;
        }
        if (name[0] != '/' && identity) {
            const char *slash = strrchr(identity,'/');
            if (slash) {
                size_t prefix = (size_t)(slash-identity)+1;
                char *full = ce_alloc(arena,prefix+strlen(name)+1);
                memcpy(full,identity,prefix); strcpy(full+prefix,name); path = full;
            }
        }
        if (protocol) {
            NativeImport *import = &native.imports[native.nimports++];
            import->atom = use; import->protocol = true;
            ok = ce_protocol_expand(arena,path,(const char *)use->items[2]->text.data,
                                    &import->text,&import->dependencies,&import->ndependencies,
                                    environment ? environment->plan : NULL,error);
            if (!ok) error->offset = use->offset;
            continue;
        }
        if (!native.macros) {
            native.macros = ce_macros_new(g,atom,error);
            if (native.macros) ce_macros_share_lexer(native.macros,native.lexer);
            if (native.macros && environment && environment->readable) ce_macros_readable(native.macros);
            if (native.macros && environment && environment->plan) ce_macros_plan(native.macros,environment->plan);
            if (native.macros && environment)
                ce_macros_environment(native.macros,environment->identifiers,environment->unguarded);
        }
        size_t begin = ce_macros_rules(native.macros);
        ok = native.macros && ce_macros_load(native.macros,path,(const char *)use->items[2]->text.data,error);
        if (ok) native.imports[native.nimports++] = (NativeImport){.atom=use,.begin=begin,.end=ce_macros_rules(native.macros)};
        if (!ok) error->offset = use->offset;
    }
    ok = ok && ce_builtin_view(g,key,&profile,error) &&
        ce_view_expand_open(&profile,atom,g->entries[0],arena,&tree,extension,accepts,&native,error);
    for (size_t i = 0; i < native.nimports && ok; ++i) {
        NativeImport *use = &native.imports[i];
        if (!use->site) ok = ce_error(error,use->atom->offset,"module import has no source position");
        else if (use->protocol) use->site->spelling = use->text;
        else ok = ce_macros_prelude(native.macros,use->begin,use->end,arena,&use->site->spelling,error);
    }
    if (ok) {
        char *buffer = NULL; size_t length = 0; FILE *file = open_memstream(&buffer,&length);
        CeSourceMap map = {0};
        if (!file) ok = ce_error(error,0,"cannot allocate native C output");
        else {
            if (environment && environment->lines && identity) {
                CeLineMarks marks = {.sorts=ce_native_line_sorts(g,arena),.source=source,.path=identity,.map=&map};
                ok = ce_tree_print_marked(g,native.lexer,tree,g->entries[0],&marks,file,error);
            } else ok = ce_tree_print_open_shared(g,native.lexer,tree,g->entries[0],file,error);
            fputc('\n',file);
            if (ok) ok = finish(arena,file,&buffer,&length,&out->text,error);
            else { fclose(file); free(buffer); }
        }
        if (ok) ce_native_map_copy(arena,&map,out);
        ce_source_map_free(&map);
    }
    if (ok) {
        out->ndependencies = ce_macros_dependencies(native.macros);
        for (size_t i = 0; i < native.nimports; ++i) out->ndependencies += native.imports[i].ndependencies;
        out->dependencies = ce_alloc(arena,out->ndependencies*sizeof(*out->dependencies));
        size_t nmacros = ce_macros_dependencies(native.macros), next = nmacros;
        for (size_t i = 0; i < nmacros; ++i) {
            CeText path = ce_macros_dependency(native.macros,i);
            out->dependencies[i] = ce_text(arena,path.data,path.len);
        }
        for (size_t i = 0; i < native.nimports; ++i)
            for (size_t j = 0; j < native.imports[i].ndependencies; ++j)
                out->dependencies[next++] = native.imports[i].dependencies[j];
        out->needs_macro_inventory = out->ndependencies != 0;
    }
    ce_macros_free(native.macros); ce_view_free(&profile); ce_lazy_lexer_free(&own); return ok;
}
const unsigned char *ce_native_line_sorts(const CeGrammar *g, CeArena *arena) {
    static const char *const levels[] = {"ExternalDeclaration","BlockItem","Statement","StructDeclaration"};
    unsigned char *sorts = ce_alloc(arena,g->nsymbols);
    memset(sorts,0,g->nsymbols);
    for (size_t i = 0; i < g->nsymbols; ++i)
        for (size_t k = 0; k < sizeof(levels)/sizeof(*levels); ++k)
            if (g->symbols[i].kind == CE_NONTERMINAL && ce_is(g->symbols[i].name,levels[k])) sorts[i] = 1;
    return sorts;
}

bool ce_native_map_copy(CeArena *arena, const CeSourceMap *map, CeNativeOutput *out) {
    if (map->ntokens) {
        CeMapToken *tokens = ce_alloc(arena,map->ntokens*sizeof(*tokens));
        memcpy(tokens,map->tokens,map->ntokens*sizeof(*tokens));
        out->tokens = tokens; out->ntokens = map->ntokens;
    }
    if (map->ndirectives) {
        CeMapDirective *directives = ce_alloc(arena,map->ndirectives*sizeof(*directives));
        memcpy(directives,map->directives,map->ndirectives*sizeof(*directives));
        out->directives = directives; out->ndirectives = map->ndirectives;
    }
    return true;
}

static const char *ce_cstr_join(CeArena *arena, const char *a, const char *b) {
    size_t n = strlen(a)+strlen(b)+1;
    char *s = ce_alloc(arena,n); snprintf(s,n,"%s%s",a,b); return s;
}

const char *ce_generated_cache(CeArena *arena, const char *output) {
    const char *cache = getenv("CESTA_BUILD_DIR");
    if (cache && *cache) return cache;
    if (!output) {
        /* Nothing compiled from these files names their path. */
        const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
        if (xdg && *xdg) return ce_cstr_join(arena,xdg,"/cesta/generated");
        if (home && *home) return ce_cstr_join(arena,home,"/.cache/cesta/generated");
        return ".cesta-generated";
    }
    const char *slash = strrchr(output,'/');
    if (!slash) return ".cesta-generated";
    size_t length = (size_t)(slash-output);
    char *directory = ce_alloc(arena,length+20);
    memcpy(directory,output,length); strcpy(directory+length,"/.cesta-generated");
    return directory;
}

bool ce_native_text(const CeGrammar *g, CeArena *arena, CeText source,
                     const char *presentation, CeText *out, CeError *error) {
    CeNativeOutput result = {0};
    bool ok = ce_native_text_at(g,arena,source,presentation,NULL,NULL,&result,error);
    if (ok) *out = result.text;
    return ok;
}

static int expand_command(int argc, char **argv, bool readable) {
    const char *plan_path = NULL; FILE *plan = NULL;
    char *plan_text = NULL; size_t plan_length = 0;
    const char *path = NULL, *output = NULL, *presentation = NULL; bool gnu = true;
    const char *compiler = getenv("CESTA_CC"), *cache = NULL;
    if (!compiler || !*compiler) compiler = "cc";
    CeArena arena = {0}; CeGrammar grammar = {0}; CeError error = {{0},0}; CeText source = {0}, text;
    int result = 1; CeNativeOutput expanded = {0};
    char **flags = ce_alloc(&arena,((size_t)argc+5)*sizeof(*flags)); size_t nflags = 1;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i],"--")) {
            while (++i < argc) flags[nflags++] = argv[i];
            break;
        }
        if (!strcmp(argv[i],"--cc")) {
            if (++i == argc) { ce_error(&error,0,"--cc requires a compiler"); goto done; }
            compiler = argv[i]; continue;
        }
        if (readable && !strcmp(argv[i],"--plan")) {
            if (++i == argc) { ce_error(&error,0,"--plan requires a file"); goto done; }
            plan_path = argv[i];
        } else if (!strcmp(argv[i],"-o") || !strcmp(argv[i],"--presentation")) {
            bool is_output = !strcmp(argv[i],"-o");
            if (++i == argc) { ce_error(&error,0,"expand option requires a value"); goto done; }
            if (is_output) output = argv[i]; else presentation = argv[i];
        } else if (!strcmp(argv[i],"--std=c11")) { gnu = false; flags[nflags++] = "-std=c11"; }
        else if (!strcmp(argv[i],"--std=gnu11")) { gnu = true; flags[nflags++] = "-std=gnu11"; }
        else if (path) { ce_error(&error,0,"expand takes one input file"); goto done; }
        else path = argv[i];
    }
    if (!path) { ce_error(&error,0,"expand requires an input file"); goto done; }
    cache = ce_generated_cache(&arena,output);
    flags[0] = (char *)compiler;
    if (!ce_read_file(&arena,path,&source,&error) || !ce_builtin_language(&grammar,gnu,&error) ||
        (plan_path && !(plan = open_memstream(&plan_text,&plan_length)) &&
             ce_error(&error,0,"cannot allocate export plan")) ||
        !(readable ? ce_native_export(&grammar,&arena,source,presentation,path,nflags,flags,cache,plan,&expanded,&error)
                   : ce_native_prepare(&grammar,&arena,source,presentation,path,nflags,flags,cache,&expanded,&error)))
        goto done;
    if (plan) {
        bool good = !ferror(plan);
        if (fclose(plan)) good = false;
        plan = NULL;
        if (!good) { ce_error(&error,0,"cannot finish export plan"); goto done; }
    }
    /* Read every imported module before opening either output. An output alias
     * must not truncate source, including a transitive import or a hard link. */
    const char *outputs[] = {output,plan_path};
    for (size_t i = 0; i < 2; ++i) if (outputs[i]) {
        if (ce_same_file(outputs[i],path)) {
            ce_error(&error,0,"output would overwrite the input document"); goto done;
        }
        for (size_t j = 0; j < expanded.ndependencies; ++j)
            if (ce_same_file(outputs[i],(const char *)expanded.dependencies[j].data)) {
                ce_error(&error,0,"output would overwrite a source dependency"); goto done;
            }
    }
    if (output && plan_path && ce_same_file(output,plan_path)) {
        ce_error(&error,0,"C and plan outputs must have distinct paths"); goto done;
    }
    text = expanded.text;
    FILE *file = output ? fopen(output,"wb") : stdout;
    if (!file) { ce_error(&error,0,"cannot open expanded C: %s",strerror(errno)); goto done; }
    bool ok = bytes(file,text);
    if (output) ok = fclose(file) == 0 && ok;
    if (!ok) { ce_error(&error,0,"cannot write expanded C"); goto done; }
    if (plan_path) {
        FILE *record = fopen(plan_path,"wb");
        if (!record) { ce_error(&error,0,"cannot open %s",plan_path); goto done; }
        bool written = fwrite(plan_text,1,plan_length,record) == plan_length;
        if (fclose(record)) written = false;
        if (!written) { ce_error(&error,0,"cannot write export plan"); goto done; }
    }
    result = 0;
done:
    if (result) {
        size_t line = 1, column = 1;
        for (size_t i = 0; i < error.offset && i < source.len; ++i)
            if (source.data[i] == '\n') { ++line; column = 1; } else ++column;
        fprintf(stderr,"%s:%zu:%zu: %s\n",path ? path : "cesta",line,column,error.message);
    }
    if (plan) fclose(plan);
    free(plan_text);
    ce_grammar_free(&grammar); ce_arena_free(&arena); return result;
}

int ce_expand_command(int argc, char **argv) { return expand_command(argc,argv,false); }
int ce_export_c_command(int argc, char **argv) { return expand_command(argc,argv,true); }

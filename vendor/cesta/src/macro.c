#define _POSIX_C_SOURCE 200809L
#include "macro.h"
#include "sema.h"

#include <stdlib.h>
#include <string.h>

CeText ce_builtin_TheoryModuleSyntax(void);

enum { ARG_ONCE, ARG_DEFERRED, ARG_REPEAT, ARG_SYNTAX };
enum { CATEGORY_VALUE, CATEGORY_ADDRESSABLE, CATEGORY_MODIFIABLE, CATEGORY_SYNTAX };
typedef struct {
    CeText name;
    int sort;
    CeAtom *type;
    unsigned evaluation, category;
    size_t repetitions, uses;
} MacroArgument;
typedef struct { CeText name, space; } MacroLocal;
typedef struct {
    CeText key, name, header, type_name, alias;
    CeAtom *type;
    bool used, intrinsic;
} MacroPort;
typedef struct {
    CeText label, head, path, theory;
    bool gnu, type_parameters;
    int result;
    MacroArgument *arguments;
    size_t arity;
    MacroLocal *locals;
    size_t nlocals;
    MacroPort *ports;
    size_t nports;
    CeAtom *type, *body, *contract;
    CeText *helpers;
    size_t nhelpers;
} MacroRule;
struct CeMacros {
    CeArena arena;
    CeGrammar syntax;
    CeCatalog catalog;
    const CeGrammar *grammar;
    CeViewProfile view;
    MacroRule *rules;
    size_t len;
    CeText *reserved;
    size_t nreserved, serial;
    const CeTree **safe;
    size_t nsafe;
    const CeAtom **private_atoms;
    CeTree **private_trees;
    size_t nprivate;
    CeText *guards;
    size_t nguards, expanding;
    CeText unpack, diagnostic_begin, diagnostic_end;
    bool prelude_emitted;
    bool unguarded;
    /* Printing asks a lexer where tokens may touch; building one dominates
       expansion time. The module syntax grammar is this context's own; the
       C lexer may be lent by the caller rendering the same grammar. */
    CeLazyLexer own_lexer, syntax_lexer, *lexer;
    /* Readable program export: names written in the unit or used by native
       macro definitions, which readable generated names must avoid. */
    bool readable;
    CeText *visible; size_t nvisible;
    /* Native macros whose expansion differs at each copy. */
    CeText *positional; size_t npositional;
    FILE *plan;
};
static bool definition_shape(CeMacros *macros, MacroRule *rule, CeError *error);

static void inventory(CeMacros *macros, const CeAtom *atom);
static CeText fresh(CeMacros *macros);

static bool form(const CeAtom *a, const char *key) {
    return a->kind == CE_ATOM_LIST && a->len && a->items[0]->kind == CE_ATOM_SYMBOL &&
        ce_is(a->items[0]->text,key);
}
static CeAtom *symbol(CeArena *a, CeText text, size_t offset) {
    CeAtom *atom = ce_atom_new(a,CE_ATOM_SYMBOL,ce_text(a,text.data,text.len),0);
    atom->offset = offset; return atom;
}
static CeAtom *word(CeArena *a, const char *name) { return symbol(a,ce_cstr(a,name),0); }
static CeAtom *list(CeArena *a, const char *head, size_t n) {
    CeAtom *atom = ce_atom_new(a,CE_ATOM_LIST,(CeText){0},n+1);
    atom->items[0] = word(a,head); return atom;
}
static int named(const CeGrammar *g, CeText name) {
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].kind != CE_LITERAL && ce_equal(g->symbols[i].name,name)) return (int)i;
    return -1;
}
static bool same(const CeGrammar *g, int a, int b) {
    return a >= 0 && b >= 0 && g->symbols[a].sort == g->symbols[b].sort;
}
static bool identifier(CeText name) {
    if (!name.len) return false;
    for (size_t i = 0; i < name.len; ++i) {
        unsigned c = name.data[i];
        if (!(c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (i && c >= '0' && c <= '9') || c >= 128)) return false;
    }
    return true;
}
static bool space(CeText name) {
    return ce_is(name,"ordinary") || ce_is(name,"tag") || ce_is(name,"member") || ce_is(name,"label");
}

/* Module law data as atoms; the module parser owns the syntax. */
static CeAtom *source_atom(CeMacros *m, const CeTree *t, CeError *error) {
    return ce_law_atom(&m->syntax,&m->syntax_lexer,&m->arena,t,error);
}
static bool field(const CeAtom *atom, const char *name, size_t count, CeError *error) {
    return (form(atom,name) && atom->len == count+1) ||
        ce_error(error,atom->offset,"macro contract requires (%s ...) with %zu fields",name,count);
}
static bool text(const CeAtom *a, CeText *out, CeError *error) {
    if (a->kind != CE_ATOM_TEXT) return ce_error(error,a->offset,"contract word requires a string");
    *out = a->text; return true;
}
static bool variable(const CeAtom *a, CeText *out, CeError *error) {
    if (a->kind != CE_ATOM_SYMBOL || !identifier(a->text))
        return ce_error(error,a->offset,"contract variable requires an identifier");
    *out = a->text; return true;
}
static bool natural(const CeAtom *a, size_t *out, CeError *error) {
    if (a->kind != CE_ATOM_SYMBOL || !a->text.len) return ce_error(error,a->offset,"repeat count requires an integer");
    size_t n = 0;
    for (size_t i = 0; i < a->text.len; ++i) {
        unsigned c = a->text.data[i];
        if (c < '0' || c > '9' || n > (SIZE_MAX-(c-'0'))/10)
            return ce_error(error,a->offset,"invalid repeat count");
        n = n*10+c-'0';
    }
    *out = n; return true;
}
static bool ctype(const CeAtom *a, CeError *error) {
    return (form(a,"CType") && (a->len == 2 || a->len == 3)) ||
        ce_error(error,a->offset,"CType requires specifiers and an optional TypeSlot declarator");
}
static MacroArgument *argument(MacroRule *r, CeText key) {
    for (size_t i = 0; i < r->arity; ++i) if (ce_equal(r->arguments[i].name,key)) return &r->arguments[i];
    return NULL;
}
static size_t local(MacroRule *r, CeText ns, CeText key) {
    for (size_t i = 0; i < r->nlocals; ++i)
        if (ce_equal(r->locals[i].space,ns) && ce_equal(r->locals[i].name,key)) return i;
    return SIZE_MAX;
}
static size_t port(MacroRule *r, CeText key) {
    for (size_t i = 0; i < r->nports; ++i) if (ce_equal(r->ports[i].key,key)) return i;
    return SIZE_MAX;
}
static bool type_parameter(MacroRule *r, const CeAtom *a, const CeGrammar *g, CeError *error) {
    CeText key;
    if (!r->type_parameters || !field(a,"TypeArgument",1,error) ||
        !variable(a->items[1],&key,error))
        return ce_error(error,a->offset,"TypeArgument requires a version 3 type parameter");
    MacroArgument *arg = argument(r,key);
    if (!arg || arg->category != CATEGORY_SYNTAX ||
        !ce_is(g->symbols[g->symbols[arg->sort].sort].name,"DeclarationSpecifiers"))
        return ce_error(error,a->offset,"TypeArgument must name a DeclarationSpecifiers syntax argument");
    return true;
}
static bool type_parameters_check(MacroRule *r, const CeAtom *a, const CeGrammar *g,
                                   bool allowed, CeError *error) {
    if (form(a,"TypeArgument"))
        return allowed ? type_parameter(r,a,g,error) :
            ce_error(error,a->offset,"definition-site ports cannot depend on use-site type arguments");
    if (form(a,"Argument") || form(a,"LocalRef") || form(a,"DefinitionRef") || form(a,"Capture"))
        return ce_error(error,a->offset,"CType parameters require TypeArgument");
    for (size_t i = 0; i < a->len; ++i)
        if (!type_parameters_check(r,a->items[i],g,allowed,error)) return false;
    return true;
}
static bool template_check(MacroRule *r, const CeAtom *a, CeError *error) {
    if (form(a,"Argument")) {
        CeText key;
        if (!field(a,"Argument",1,error) || !variable(a->items[1],&key,error)) return false;
        MacroArgument *arg = argument(r,key);
        if (!arg) return ce_error(error,a->offset,"unknown macro argument");
        ++arg->uses; return true;
    }
    if (form(a,"LocalRef")) {
        CeText ns, key;
        if (!field(a,"LocalRef",2,error) || !text(a->items[1],&ns,error) ||
            !variable(a->items[2],&key,error)) return false;
        return local(r,ns,key) != SIZE_MAX || ce_error(error,a->offset,"LocalRef has no binder in that namespace");
    }
    if (form(a,"Capture")) {
        CeText ns, key;
        return field(a,"Capture",2,error) && text(a->items[1],&ns,error) &&
            text(a->items[2],&key,error) && ((space(ns) && identifier(key)) ||
            ce_error(error,a->offset,"capture requires a C namespace and identifier"));
    }
    if (form(a,"DefinitionRef")) {
        CeText ns, key;
        if (!field(a,"DefinitionRef",2,error) || !text(a->items[1],&ns,error) ||
            !variable(a->items[2],&key,error)) return false;
        size_t i = port(r,key);
        return (ce_is(ns,"ordinary") && i != SIZE_MAX && !r->ports[i].intrinsic) ||
            ce_error(error,a->offset,"DefinitionRef has no native symbol port in that namespace");
    }
    if (form(a,"CompilerRef")) {
        CeText key;
        if (!field(a,"CompilerRef",1,error) || !variable(a->items[1],&key,error)) return false;
        size_t i = port(r,key);
        return (i != SIZE_MAX && r->ports[i].intrinsic) ||
            ce_error(error,a->offset,"CompilerRef has no declared compiler builtin");
    }
    if (form(a,"native") || form(a,"include") || form(a,"define") || form(a,"use-macros") || form(a,"use-protocols") ||
        form(a,"cpp-if") || form(a,"cpp-ifdef") || form(a,"cpp-ifndef"))
        return ce_error(error,a->offset,"macro template cannot hide binding or evaluation in opaque source");
    if (a->kind == CE_ATOM_LIST)
        for (size_t i = 0; i < a->len; ++i) if (!template_check(r,a->items[i],error)) return false;
    return true;
}

static bool contract(CeMacros *m, MacroRule *r, CeAtom *lhs, CeAtom *rhs,
                      const CeProduction *production, const CeGrammar *surface, CeError *error) {
    bool ports = form(rhs,"Macro") && rhs->len == 8 && rhs->items[1]->kind == CE_ATOM_TEXT &&
        (ce_is(rhs->items[1]->text,"cesta-macro/2") || ce_is(rhs->items[1]->text,"cesta-macro/3"));
    r->type_parameters = ports && ce_is(rhs->items[1]->text,"cesta-macro/3");
    if (!ports && (!form(rhs,"Macro") || rhs->len != 7 || rhs->items[1]->kind != CE_ATOM_TEXT ||
        !ce_is(rhs->items[1]->text,"cesta-macro/1")))
        return ce_error(error,rhs->offset,"macro interpretation requires a versioned cesta-macro contract");
    if (lhs->kind != CE_ATOM_LIST || lhs->len != r->arity+1)
        return ce_error(error,lhs->offset,"macro left side disagrees with Terms arity");
    const CeAtom *profile = rhs->items[2], *args = rhs->items[3+ports], *result = rhs->items[4+ports],
                 *locals = rhs->items[5+ports], *body = rhs->items[6+ports];
    inventory(m,rhs);
    if (ports) {
        const CeAtom *bindings = rhs->items[3];
        if (!form(bindings,"Ports")) return ce_error(error,bindings->offset,"version 2 requires Ports");
        r->nports = bindings->len-1; r->ports = ce_alloc(&m->arena,r->nports*sizeof(*r->ports));
        for (size_t i = 0; i < r->nports; ++i) {
            const CeAtom *p = bindings->items[i+1]; MacroPort *binding = &r->ports[i];
            if (form(p,"CompilerBuiltin")) {
                if (!r->type_parameters || !field(p,"CompilerBuiltin",2,error) ||
                    !variable(p->items[1],&binding->key,error) || !text(p->items[2],&binding->name,error))
                    return ce_error(error,p->offset,"CompilerBuiltin requires version 3");
                binding->intrinsic = true;
                if (binding->name.len < 11 || memcmp(binding->name.data,"__builtin_",10))
                    return ce_error(error,p->offset,"CompilerBuiltin requires a native __builtin_ name");
            } else {
              if (!field(p,"NativeSymbol",4,error) || !variable(p->items[1],&binding->key,error) ||
                !field(p->items[2],"SystemHeader",1,error) ||
                !text(p->items[2]->items[1],&binding->header,error) ||
                !ctype(p->items[3],error) || !text(p->items[4],&binding->name,error)) return false;
              if (!binding->header.len) return ce_error(error,p->offset,"native port requires its declaring header");
              for (size_t c = 0; c < binding->header.len; ++c)
                if (binding->header.data[c] <= 32 || binding->header.data[c] == '>' ||
                    binding->header.data[c] == '<' || binding->header.data[c] == '\\')
                    return ce_error(error,p->offset,"invalid system header spelling");
              binding->type = p->items[3];
            }
            if (!identifier(binding->name)) return ce_error(error,p->offset,"native symbol requires a C identifier");
            for (size_t j = 0; j < i; ++j) if (ce_equal(binding->key,r->ports[j].key))
                return ce_error(error,p->offset,"duplicate definition-site port");
            binding->type_name = fresh(m); binding->alias = fresh(m);
        }
    }
    CeText dialect, category;
    if (!field(profile,"Profile",1,error) || !text(profile->items[1],&dialect,error)) return false;
    if (!ce_is(dialect,"c11") && !ce_is(dialect,"gnu11"))
        return ce_error(error,profile->offset,"macro profile must be c11 or gnu11");
    r->gnu = ce_is(dialect,"gnu11");
    for (size_t i = 0; i < r->nports; ++i)
        if (r->ports[i].intrinsic && !r->gnu)
            return ce_error(error,profile->offset,"compiler builtin ports require the GNU profile");
    if (!form(args,"Arguments") || args->len != r->arity+1)
        return ce_error(error,args->offset,"Arguments must cover every Terms field in order");
    r->arguments = ce_alloc(&m->arena,r->arity*sizeof(*r->arguments));
    for (size_t i = 0; i < r->arity; ++i) {
        const CeAtom *a = args->items[i+1]; MacroArgument *arg = &r->arguments[i];
        if (!field(a,"Arg",4,error) || !variable(a->items[1],&arg->name,error) ||
            !ctype(a->items[2],error) || !text(a->items[3],&category,error)) return false;
        if (lhs->items[i+1]->kind != CE_ATOM_SYMBOL || !ce_equal(lhs->items[i+1]->text,arg->name))
            return ce_error(error,a->offset,"Arg name must agree with the corresponding left-side hole");
        for (size_t j = 0; j < i; ++j) if (ce_equal(arg->name,r->arguments[j].name))
            return ce_error(error,a->offset,"duplicate macro argument");
        int source_sort = production->rhs[i+2];
        arg->sort = named(m->grammar,surface->symbols[source_sort].name);
        if (arg->sort < 0) return ce_error(error,a->offset,"argument sort is outside the C signature");
        arg->type = a->items[2];
        if (ce_is(category,"value")) arg->category = CATEGORY_VALUE;
        else if (ce_is(category,"addressable")) arg->category = CATEGORY_ADDRESSABLE;
        else if (ce_is(category,"addressable-modifiable")) arg->category = CATEGORY_MODIFIABLE;
        else if (ce_is(category,"syntax")) arg->category = CATEGORY_SYNTAX;
        else return ce_error(error,a->offset,"unknown argument value category");
        const CeAtom *evaluation = a->items[4]; CeText policy;
        if (!form(evaluation,"Evaluation") || (evaluation->len != 2 && evaluation->len != 3) ||
            !text(evaluation->items[1],&policy,error)) return false;
        if (ce_is(policy,"once") && evaluation->len == 2) arg->evaluation = ARG_ONCE;
        else if (ce_is(policy,"deferred") && evaluation->len == 2) arg->evaluation = ARG_DEFERRED;
        else if (ce_is(policy,"syntax") && evaluation->len == 2) arg->evaluation = ARG_SYNTAX;
        else if (ce_is(policy,"repeat") && evaluation->len == 3) {
            arg->evaluation = ARG_REPEAT;
            if (!natural(evaluation->items[2],&arg->repetitions,error)) return false;
        } else return ce_error(error,evaluation->offset,"unknown argument evaluation contract");
        if ((arg->category == CATEGORY_SYNTAX) != (arg->evaluation == ARG_SYNTAX))
            return ce_error(error,a->offset,"syntax arguments require a syntax evaluation contract");
        if (arg->category != CATEGORY_SYNTAX &&
            !ce_is(m->grammar->symbols[m->grammar->symbols[arg->sort].sort].name,"Expr"))
            return ce_error(error,a->offset,"evaluated arguments must have Expr sort");
    }
    if (!field(result,"Result",2,error) || !ctype(result->items[1],error) ||
        !text(result->items[2],&category,error)) return false;
    r->type = result->items[1];
    CeText result_sort = m->grammar->symbols[m->grammar->symbols[r->result].sort].name;
    if ((!ce_is(result_sort,"Expr") || !ce_is(category,"value")) &&
        (!ce_is(result_sort,"Statement") || !ce_is(category,"statement")))
        return ce_error(error,result->offset,"macro result must be Expr/value or Statement/statement");
    if (!r->gnu && ce_is(result_sort,"Expr"))
        return ce_error(error,result->offset,"C11 expression lowering is not admitted yet; use a statement macro or GNU profile");
    if (!form(locals,"Locals")) return ce_error(error,locals->offset,"macro contract requires Locals");
    r->nlocals = locals->len-1; r->locals = ce_alloc(&m->arena,r->nlocals*sizeof(*r->locals));
    for (size_t i = 0; i < r->nlocals; ++i) {
        const CeAtom *a = locals->items[i+1]; MacroLocal *l = &r->locals[i];
        if (!field(a,"Local",2,error) || !variable(a->items[1],&l->name,error) ||
            !text(a->items[2],&l->space,error)) return false;
        if (!space(l->space)) return ce_error(error,a->offset,"unknown C binder namespace");
        for (size_t j = 0; j < i; ++j)
            if (ce_equal(l->name,r->locals[j].name) && ce_equal(l->space,r->locals[j].space))
                return ce_error(error,a->offset,"duplicate binder identity");
    }
    if (!field(body,"Template",1,error)) return false;
    r->body = body->items[1]; r->contract = rhs;
    if (!type_parameters_check(r,r->type,m->grammar,true,error)) return false;
    for (size_t i = 0; i < r->arity; ++i)
        if (!type_parameters_check(r,r->arguments[i].type,m->grammar,true,error)) return false;
    for (size_t i = 0; i < r->nports; ++i)
        if (!r->ports[i].intrinsic && !type_parameters_check(r,r->ports[i].type,m->grammar,false,error)) return false;
    if (!template_check(r,r->body,error)) return false;
    inventory(m,rhs);
    for (size_t i = 0; i < r->arity; ++i) {
        MacroArgument *a = &r->arguments[i];
        if (a->evaluation == ARG_REPEAT && a->uses != a->repetitions)
            return ce_error(error,r->body->offset,"repeat contract differs from Argument occurrence count");
        if (a->evaluation == ARG_DEFERRED && a->uses != 1)
            return ce_error(error,r->body->offset,"deferred argument must have one template occurrence");
    }
    return true;
}

static void see(CeMacros *m, CeText value) {
    for (size_t i = 0; i < m->nvisible; ++i) if (ce_equal(m->visible[i],value)) return;
    m->visible = ce_resize(m->visible,m->nvisible+1,sizeof(*m->visible));
    m->visible[m->nvisible++] = ce_text(&m->arena,value.data,value.len);
}
static void see_identifiers(CeMacros *m, CeText text) {
    for (size_t i = 0; i < text.len;) {
        unsigned c = text.data[i];
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128)) { ++i; continue; }
        size_t start = i++;
        while (i < text.len && (identifier((CeText){text.data+i,1}) || (text.data[i] >= '0' && text.data[i] <= '9'))) ++i;
        see(m,(CeText){text.data+start,i-start});
    }
}
static void see_atom(CeMacros *m, const CeAtom *a) {
    if (a->kind == CE_ATOM_LIST) { for (size_t i = 0; i < a->len; ++i) see_atom(m,a->items[i]); return; }
    see_identifiers(m,a->text);
}
static void reserve(CeMacros *m, CeText value) {
    for (size_t i = 0; i < m->nreserved; ++i) if (ce_equal(m->reserved[i],value)) return;
    m->reserved = ce_resize(m->reserved,m->nreserved+1,sizeof(*m->reserved));
    m->reserved[m->nreserved++] = ce_text(&m->arena,value.data,value.len);
}
static void inventory(CeMacros *m, const CeAtom *a) {
    if (a->kind == CE_ATOM_LIST) {
        for (size_t i = 0; i < a->len; ++i) inventory(m,a->items[i]);
        return;
    }
    /* Over-approximate written identifiers, including explicit native text and
       directive replacement spelling. Native header names require compiler
       admission; this source inventory does not claim to know their contents. */
    for (size_t i = 0; i < a->text.len;) {
        unsigned c = a->text.data[i];
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 128)) { ++i; continue; }
        size_t start = i++;
        while (i < a->text.len && (identifier((CeText){a->text.data+i,1}) ||
               (a->text.data[i] >= '0' && a->text.data[i] <= '9'))) ++i;
        reserve(m,(CeText){a->text.data+start,i-start});
    }
}
void ce_macros_environment(CeMacros *m, CeText identifiers, bool unguarded) {
    CeAtom bytes = {.kind=CE_ATOM_TEXT,.text=identifiers};
    inventory(m,&bytes); m->unguarded = unguarded;
    /* Macro names and the names their replacements mention. */
    for (size_t i = 0; i < identifiers.len;) {
        size_t end = i; while (end < identifiers.len && identifiers.data[end] != '\n') ++end;
        CeText line = {identifiers.data+i,end-i};
        if (line.len > 8 && !memcmp(line.data,"#define ",8)) {
            see_identifiers(m,line);
            bool counter = false;
            for (size_t k = 0; k < line.len && !counter; ++k)
                counter = (k+11 <= line.len && !memcmp(line.data+k,"__COUNTER__",11)) ||
                          (k+8 <= line.len && !memcmp(line.data+k,"__LINE__",8));
            size_t start = 8, stop = 8;
            while (stop < line.len && (identifier((CeText){line.data+stop,1}) ||
                   (stop > start && line.data[stop] >= '0' && line.data[stop] <= '9'))) ++stop;
            if (counter && stop > start) {
                m->positional = ce_resize(m->positional,m->npositional+1,sizeof(*m->positional));
                m->positional[m->npositional++] = ce_text(&m->arena,line.data+start,stop-start);
            }
        }
        i = end+1;
    }
}
void ce_macros_share_lexer(CeMacros *m, CeLazyLexer *lexer) { m->lexer = lexer; }
void ce_macros_readable(CeMacros *m) { m->readable = true; }
void ce_macros_plan(CeMacros *m, FILE *plan) { m->plan = plan; }
static CeText fresh(CeMacros *m) {
    for (;;) {
        char buffer[64]; snprintf(buffer,sizeof(buffer),"cesta_macro_%zu",m->serial++);
        CeText name = {(unsigned char *)buffer,strlen(buffer)}; bool found = false;
        for (size_t i = 0; i < m->nreserved; ++i) found |= ce_equal(m->reserved[i],name);
        if (!found) { reserve(m,name); return m->reserved[m->nreserved-1]; }
    }
}
CeMacros *ce_macros_new(const CeGrammar *g, const CeAtom *source, CeError *error) {
    CeMacros *m = calloc(1,sizeof(*m));
    if (!m) { ce_error(error,0,"cannot allocate macro context"); return NULL; }
    m->grammar = g; m->catalog.syntax = &m->syntax; m->lexer = &m->own_lexer;
    if (!ce_grammar_read(&m->syntax,ce_builtin_TheoryModuleSyntax(),error) ||
        !ce_grammar_finish(&m->syntax,error) || !ce_builtin_view(g,"compact-c/2",&m->view,error)) {
        ce_macros_free(m); return NULL;
    }
    if (source) { inventory(m,source); see_atom(m,source); }
    return m;
}
void ce_macros_free(CeMacros *m) {
    if (!m) return;
    ce_view_free(&m->view); ce_catalog_free(&m->catalog); ce_grammar_free(&m->syntax);
    ce_lazy_lexer_free(&m->own_lexer); ce_lazy_lexer_free(&m->syntax_lexer); free(m->visible); free(m->positional);
    for (size_t i = 0; i < m->len; ++i) free(m->rules[i].helpers);
    ce_arena_free(&m->arena); free(m->rules); free(m->reserved); free(m->safe);
    free(m->private_atoms); free(m->private_trees); free(m->guards); free(m);
}
bool ce_macros_load(CeMacros *m, const char *path, const char *theory, CeError *error) {
    CeModule *module; CePresentation presentation = {0}; CeGrammar surface = {0};
    size_t begin = m->len;
    if (!ce_module_load(&m->catalog,path,&module,error) || !ce_module_check(&m->catalog,module,error) ||
        !ce_module_theory(&m->catalog,module,theory,&presentation,error) ||
        !ce_presentation_signature(&m->catalog,&presentation,m->grammar,&surface,error)) {
        ce_grammar_free(&surface); return false;
    }
    bool ok = true;
    for (size_t i = 0; i < surface.nproductions && ok; ++i) {
        const CeProduction *p = &surface.productions[i];
        if (p->internal || surface.symbols[p->lhs].erased || surface.symbols[p->lhs].list_element >= 0) continue;
        if (p->len < 3 || surface.symbols[p->rhs[0]].kind != CE_LITERAL ||
            !ce_is(surface.symbols[p->rhs[0]].name,"(") ||
            surface.symbols[p->rhs[1]].kind != CE_LITERAL ||
            !ce_is(surface.symbols[p->rhs[p->len-1]].name,")")) {
            ok = ce_error(error,p->offset,"macro Terms require a headed S-expression"); break;
        }
        MacroRule r = {.label=p->label,.head=surface.symbols[p->rhs[1]].name,
                       .path=ce_module_path(module),.theory=ce_cstr(&m->arena,theory),.arity=p->len-3};
        r.result = named(m->grammar,surface.symbols[p->lhs].name);
        if (r.result < 0) { ok = ce_error(error,p->offset,"macro result sort is outside the C signature"); break; }
        for (size_t j = 0; j < r.arity; ++j)
            if (surface.symbols[p->rhs[j+2]].kind == CE_LITERAL)
                ok = ce_error(error,p->offset,"macro field requires a declared syntax sort");
        if (!ok) break;
        for (size_t j = 0; j < m->len; ++j)
            if (same(m->grammar,r.result,m->rules[j].result) && ce_equal(r.head,m->rules[j].head))
                ok = ce_error(error,p->offset,"duplicate macro dispatch head in the same result sort");
        for (size_t j = 0; j < m->view.len; ++j)
            if (same(m->grammar,r.result,m->view.rules[j].result) &&
                ce_equal(r.head,m->view.rules[j].head) && m->view.rules[j].shape == CE_VIEW_FORM)
                ok = ce_error(error,p->offset,"macro head would change an existing compact form");
        const char *reserved[] = {"include","define","undef","cpp-if","cpp-ifdef","cpp-ifndef","comment","native","use-macros","use-protocols"};
        for (size_t j = 0; j < sizeof(reserved)/sizeof(*reserved); ++j)
            if (ce_is(r.head,reserved[j])) ok = ce_error(error,p->offset,"macro head is a native source form");
        if (!ok) break;
        bool found = false;
        for (size_t j = 0; j < presentation.nlaws && ok; ++j) {
            const CeTree *law = presentation.laws[j].tree;
            if (ce_tree_label(&m->syntax,law,"RDecl")) law = law->children[1];
            if (!ce_tree_label(&m->syntax,law,"EquationImpl") && !ce_tree_label(&m->syntax,law,"RewriteBase"))
                continue;
            CeAtom *lhs = source_atom(m,law->children[0],error);
            if (!lhs) { ok = false; break; }
            if (lhs->kind != CE_ATOM_LIST || !lhs->len || !ce_equal(lhs->items[0]->text,p->label)) continue;
            if (found) { ok = ce_error(error,law->offset,"duplicate macro interpretation"); break; }
            CeAtom *rhs = source_atom(m,law->children[1],error);
            ok = rhs && contract(m,&r,lhs,rhs,p,&surface,error); found = true;
        }
        if (ok && !found) ok = ce_error(error,p->offset,"macro Terms constructor has no contract");
        if (ok) ok = definition_shape(m,&r,error);
        if (ok) {
            /* Source grammar storage is temporary; retain its names in the
               macro arena before freeing it. */
            r.label = ce_text(&m->arena,r.label.data,r.label.len);
            r.head = ce_text(&m->arena,r.head.data,r.head.len);
            m->rules = ce_resize(m->rules,m->len+1,sizeof(*m->rules)); m->rules[m->len++] = r;
        }
    }
    for (size_t j = 0; j < presentation.nlaws && ok; ++j) {
        const CeTree *law = presentation.laws[j].tree;
        if (ce_tree_label(&m->syntax,law,"RDecl")) law = law->children[1];
        if (!ce_tree_label(&m->syntax,law,"EquationImpl") && !ce_tree_label(&m->syntax,law,"RewriteBase")) {
            ok = ce_error(error,law->offset,"macro law requires an equation or rewrite"); break;
        }
        CeAtom *lhs = source_atom(m,law->children[0],error);
        bool found = false;
        if (lhs && lhs->kind == CE_ATOM_LIST && lhs->len)
            for (size_t i = begin; i < m->len; ++i)
                found |= ce_equal(lhs->items[0]->text,m->rules[i].label);
        if (!found) ok = ce_error(error,law->offset,"macro law names no Terms constructor");
    }
    ce_grammar_free(&surface); return ok;
}
bool ce_macros_accepts(const CeMacros *m, const CeAtom *a, int sort) {
    if (!m) return false;
    for (size_t i = 0; i < m->nprivate; ++i)
        if (m->private_atoms[i] == a && same(m->grammar,sort,m->private_trees[i]->symbol)) return true;
    if (a->kind != CE_ATOM_LIST || !a->len || a->items[0]->kind != CE_ATOM_SYMBOL) return false;
    for (size_t i = 0; i < m->len; ++i)
        if (same(m->grammar,sort,m->rules[i].result) && ce_equal(a->items[0]->text,m->rules[i].head)) return true;
    return false;
}

static bool private_accepts(void *context, const CeAtom *a, int sort) {
    CeMacros *m = context;
    for (size_t i = 0; i < m->nprivate; ++i)
        if (m->private_atoms[i] == a && same(m->grammar,sort,m->private_trees[i]->symbol)) return true;
    return false;
}
static unsigned private_extension(void *context, const CeAtom *a, int sort, int parent,
                                  CeArena *arena, CeTree **out, CeAtomDecoder recurse,
                                  void *recurse_context, CeError *error, size_t depth) {
    (void)parent; (void)arena; (void)recurse; (void)recurse_context; (void)error; (void)depth;
    CeMacros *m = context;
    for (size_t i = 0; i < m->nprivate; ++i)
        if (m->private_atoms[i] == a && same(m->grammar,sort,m->private_trees[i]->symbol)) {
            *out = m->private_trees[i]; return CE_EXTENSION_EXPANDED;
        }
    return CE_EXTENSION_UNHANDLED;
}

typedef struct {
    CeMacros *macros; MacroRule *rule; CeAtom **arguments; CeText *locals; CeArena *arena;
    bool checking;
} Instance;
static CeAtom *instantiate(Instance *in, const CeAtom *a, CeError *error) {
    if (form(a,"TypeArgument")) {
        if (!type_parameter(in->rule,a,in->macros->grammar,error)) return NULL;
        if (in->checking) return word(in->arena,"int");
        MacroArgument *arg = argument(in->rule,a->items[1]->text);
        return in->arguments[(size_t)(arg-in->rule->arguments)];
    }
    if (form(a,"Argument")) {
        MacroArgument *arg = argument(in->rule,a->items[1]->text);
        return in->arguments[(size_t)(arg-in->rule->arguments)];
    }
    if (form(a,"LocalRef")) {
        size_t i = local(in->rule,a->items[1]->text,a->items[2]->text);
        return symbol(in->arena,in->locals[i],a->offset);
    }
    if (form(a,"Capture")) return symbol(in->arena,a->items[2]->text,a->offset);
    if (form(a,"DefinitionRef")) {
        MacroPort *binding = &in->rule->ports[port(in->rule,a->items[2]->text)];
        if (in->checking) return symbol(in->arena,binding->alias,a->offset);
        binding->used = true;
        CeAtom *reference = list(in->arena,"*",1);
        reference->items[1] = symbol(in->arena,binding->alias,a->offset);
        return reference;
    }
    if (form(a,"CompilerRef")) {
        MacroPort *binding = &in->rule->ports[port(in->rule,a->items[1]->text)];
        if (in->checking) return symbol(in->arena,binding->alias,a->offset);
        binding->used = true;
        return symbol(in->arena,binding->name,a->offset);
    }
    CeAtom *out = ce_atom_new(in->arena,a->kind,ce_text(in->arena,a->text.data,a->text.len),a->len);
    out->offset = a->offset;
    for (size_t i = 0; i < a->len; ++i) {
        out->items[i] = instantiate(in,a->items[i],error);
        if (!out->items[i]) return NULL;
    }
    return out;
}
static CeAtom *type_slot(CeMacros *m, CeArena *arena, const CeAtom *a, CeText name, size_t *uses) {
    for (size_t i = 0; i < m->nprivate; ++i)
        if (m->private_atoms[i] == a) return (CeAtom *)a;
    if (a->kind == CE_ATOM_SYMBOL && ce_is(a->text,"TypeSlot")) { ++*uses; return symbol(arena,name,a->offset); }
    CeAtom *out = ce_atom_new(arena,a->kind,ce_text(arena,a->text.data,a->text.len),a->len);
    out->offset = a->offset;
    for (size_t i = 0; i < a->len; ++i) out->items[i] = type_slot(m,arena,a->items[i],name,uses);
    return out;
}
static bool type_alias(CeMacros *m, CeArena *arena, const CeAtom *type, CeText name,
                        FILE *file, CeError *error) {
    size_t uses = 0;
    CeAtom *declarator = type->len == 3 ? type_slot(m,arena,type->items[2],name,&uses) : symbol(arena,name,0);
    if (type->len == 3 && uses != 1)
        return ce_error(error,type->offset,"CType declarator must contain exactly one TypeSlot");
    CeAtom *decl = list(arena,"var",2); decl->items[1] = type->items[1]; decl->items[2] = declarator;
    int sort = named(m->grammar,ce_cstr(arena,"Declaration")); CeTree *tree;
    if (!ce_view_expand_open(&m->view,decl,sort,arena,&tree,private_extension,private_accepts,m,error)) return false;
    fputs("typedef ",file);
    return ce_tree_print_open_shared(m->grammar,m->lexer,tree,sort,file,error);
}

typedef struct { Instance instance; CeText *captures; size_t ncaptures; const CeTree *body; } Shape;
static bool shape_accepts(void *context, const CeAtom *a, int sort) {
    Shape *shape = context;
    if (form(a,"MacroShapeBody"))
        return shape->body && same(shape->instance.macros->grammar,sort,shape->body->symbol);
    if (!form(a,"MacroShapeHole") || a->len != 2) return false;
    size_t index; CeError error = {{0},0};
    return natural(a->items[1],&index,&error) && index < shape->instance.rule->arity &&
        same(shape->instance.macros->grammar,sort,shape->instance.rule->arguments[index].sort);
}
static unsigned shape_extension(void *context, const CeAtom *a, int sort, int parent,
                                 CeArena *arena, CeTree **out, CeAtomDecoder recurse,
                                 void *recurse_context, CeError *error, size_t depth) {
    (void)parent; (void)recurse; (void)recurse_context; (void)depth;
    if (!shape_accepts(context,a,sort)) return CE_EXTENSION_UNHANDLED;
    Shape *shape = context;
    if (form(a,"MacroShapeBody")) { *out = (CeTree *)shape->body; return CE_EXTENSION_EXPANDED; }
    const CeGrammar *g = shape->instance.macros->grammar;
    CeText name = g->symbols[g->symbols[sort].sort].name;
    CeAtom *witness;
    if (ce_is(name,"Expr")) witness = word(arena,"0");
    else if (ce_is(name,"Statement")) witness = list(arena,"block",0);
    else if (ce_is(name,"DeclarationSpecifiers")) witness = word(arena,"int");
    else {
        ce_error(error,a->offset,"no definition-check witness for syntax sort %.*s",(int)name.len,name.data);
        return CE_EXTENSION_ERROR;
    }
    return ce_view_expand(&shape->instance.macros->view,witness,sort,arena,out,NULL,error) ?
        CE_EXTENSION_EXPANDED : CE_EXTENSION_ERROR;
}
static void capture_inventory(Shape *shape, const CeAtom *a) {
    if (form(a,"Capture") && ce_is(a->items[1]->text,"ordinary")) {
        shape->captures = ce_resize(shape->captures,shape->ncaptures+1,sizeof(*shape->captures));
        shape->captures[shape->ncaptures++] = a->items[2]->text; return;
    }
    for (size_t i = 0; i < a->len; ++i) capture_inventory(shape,a->items[i]);
}
static bool explicit_references(const Shape *shape, const CeTree *tree, CeError *error) {
    const CeGrammar *g = shape->instance.macros->grammar;
    if (ce_tree_label(g,tree,"Var")) {
        CeText name = tree->children[0]->spelling; bool allowed = false;
        for (size_t i = 0; i < shape->instance.rule->nlocals; ++i)
            allowed |= ce_is(shape->instance.rule->locals[i].space,"ordinary") &&
                ce_equal(name,shape->instance.locals[i]);
        for (size_t i = 0; i < shape->ncaptures; ++i) allowed |= ce_equal(name,shape->captures[i]);
        if (!allowed) return ce_error(error,tree->offset,"template identifier needs LocalRef, Capture or a definition-site port");
    }
    for (size_t i = 0; i < tree->len; ++i)
        if (!explicit_references(shape,tree->children[i],error)) return false;
    return true;
}
static bool binder_admission(Shape *shape, CeError *error) {
    CeMacros *m = shape->instance.macros; MacroRule *r = shape->instance.rule;
    CeArena *arena = shape->instance.arena;
    CeAtom *unit = list(arena,"unit",shape->ncaptures+1);
    for (size_t i = 0; i < shape->ncaptures; ++i) {
        CeAtom *decl = list(arena,"var",2);
        decl->items[1] = word(arena,"int"); decl->items[2] = symbol(arena,shape->captures[i],0);
        unit->items[i+1] = decl;
    }
    bool expr = ce_is(m->grammar->symbols[m->grammar->symbols[r->result].sort].name,"Expr");
    CeAtom *function = list(arena,"fn",expr ? 4 : 5);
    function->items[1] = word(arena,"int"); function->items[2] = word(arena,"cesta_binding_check");
    function->items[3] = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},0);
    CeAtom *body = list(arena,"MacroShapeBody",0), *ret = list(arena,"return",1);
    ret->items[1] = expr ? body : word(arena,"0");
    function->items[4] = expr ? ret : body;
    if (!expr) function->items[5] = ret;
    unit->items[shape->ncaptures+1] = function;
    CeTree *tree;
    if (!ce_view_expand_open(&m->view,unit,m->grammar->entries[0],arena,&tree,
            shape_extension,shape_accepts,shape,error)) return false;
    CeSema sema = {0};
    if (!ce_sema_init(&sema,m->grammar,(CeText){0},error)) return false;
    bool ok = ce_bind(&sema,tree,error);
    for (size_t i = 0; i < r->nlocals && ok; ++i) {
        MacroLocal *local = &r->locals[i]; CeEntity *owner = NULL;
        if (ce_is(local->space,"member")) {
            ok = ce_error(error,r->body->offset,"member binder admission requires a record-owner port"); break;
        }
        unsigned ns = ce_is(local->space,"tag") ? CE_SPACE_TAG :
                      ce_is(local->space,"label") ? CE_SPACE_LABEL : CE_SPACE_ORDINARY;
        for (size_t j = 0; j < sema.nentities && ok; ++j) {
            CeEntity *entity = sema.entities[j];
            if (!ce_equal(entity->name,shape->instance.locals[i])) continue;
            if (entity->space != ns) {
                ok = ce_error(error,r->body->offset,"LocalRef binder is declared in a different C namespace"); break;
            }
            if (owner && owner != entity) {
                ok = ce_error(error,r->body->offset,"one LocalRef identity names multiple C declarations"); break;
            }
            owner = entity;
        }
        if (ok && !owner) ok = ce_error(error,r->body->offset,"LocalRef identity has no C declaration");
        for (size_t j = 0; j < sema.nuses && ok; ++j) {
            const CeUse *use = &sema.uses[j];
            if (ce_equal(use->entity->name,shape->instance.locals[i]) && use->entity != owner)
                ok = ce_error(error,r->body->offset,"LocalRef resolves to a different declaration");
        }
    }
    ce_sema_free(&sema); return ok;
}
static bool definition_shape(CeMacros *m, MacroRule *r, CeError *error) {
    CeArena arena = {0}; Shape shape = {.instance={.macros=m,.rule=r,.arena=&arena,.checking=true}};
    shape.instance.arguments = ce_alloc(&arena,r->arity*sizeof(*shape.instance.arguments));
    shape.instance.locals = ce_alloc(&arena,r->nlocals*sizeof(*shape.instance.locals));
    FILE *file = tmpfile();
    if (!file) return ce_error(error,r->body->offset,"cannot check C type declarations");
    bool ok = true;
    for (size_t i = 0; i < r->nports && ok; ++i) {
        shape.captures = ce_resize(shape.captures,shape.ncaptures+1,sizeof(*shape.captures));
        shape.captures[shape.ncaptures++] = r->ports[i].alias;
        if (!r->ports[i].intrinsic)
            ok = type_alias(m,&arena,r->ports[i].type,ce_cstr(&arena,"cesta_port_type_check"),file,error);
    }
    for (size_t i = 0; i < r->arity && ok; ++i) {
        char number[32]; snprintf(number,sizeof(number),"%zu",i);
        CeAtom *hole = list(&arena,"MacroShapeHole",1); hole->items[1] = word(&arena,number);
        shape.instance.arguments[i] = hole;
    }
    CeAtom *checked_type = instantiate(&shape.instance,r->type,error);
    ok = ok && checked_type && type_alias(m,&arena,checked_type,ce_cstr(&arena,"cesta_type_check"),file,error);
    for (size_t i = 0; i < r->arity && ok; ++i) {
        checked_type = instantiate(&shape.instance,r->arguments[i].type,error);
        ok = checked_type && type_alias(m,&arena,checked_type,ce_cstr(&arena,"cesta_argument_type_check"),file,error);
    }
    fclose(file);
    for (size_t i = 0; i < r->nlocals; ++i) {
        shape.instance.locals[i] = fresh(m);
    }
    capture_inventory(&shape,r->body);
    CeAtom *body = ok ? instantiate(&shape.instance,r->body,error) : NULL; CeTree *tree;
    ok = ok && body && ce_view_expand_open(&m->view,body,r->result,&arena,&tree,
        shape_extension,shape_accepts,&shape,error) && explicit_references(&shape,tree,error);
    if (ok) { shape.body = tree; ok = binder_admission(&shape,error); }
    free(shape.captures); ce_arena_free(&arena); return ok;
}
static bool safe_tree(const CeMacros *m, const CeTree *tree) {
    if (tree->production == CE_TREE_SOURCE) {
        for (size_t i = 0; i < m->nsafe; ++i) if (m->safe[i] == tree) return true;
        return false;
    }
    for (size_t i = 0; i < tree->len; ++i) if (!safe_tree(m,tree->children[i])) return false;
    return true;
}
static void guard(CeMacros *m, FILE *f, CeText name) {
    if (m->expanding) {
        for (size_t i = 0; i < m->nguards; ++i) if (ce_equal(m->guards[i],name)) return;
        m->guards = ce_resize(m->guards,m->nguards+1,sizeof(*m->guards));
        m->guards[m->nguards++] = name; return;
    }
    if (m->unguarded) fputs("\n#if 0\n#error Cesta_macro_name_is_a_preprocessor_macro\n#endif\n",f);
    else fprintf(f,"\n#if defined(%.*s)\n#error Cesta_macro_name_is_a_preprocessor_macro\n#endif\n",
                 (int)name.len,name.data);
}
static void admission_diagnostic(CeMacros *m, FILE *f, bool begin) {
    CeText name = begin ? m->diagnostic_begin : m->diagnostic_end;
    fprintf(f,"\n%.*s\n",(int)name.len,name.data);
}
static bool print_tree(CeMacros *m, const CeTree *t, FILE *f, CeError *error) {
    return ce_tree_print_open_shared(m->grammar,m->lexer,t,t->symbol,f,error);
}
static CeAtom *private_argument(CeMacros *m, CeArena *arena, int sort, CeText parameter,
                                size_t offset, const CeAtom *written) {
    CeAtom *atom = list(arena,"MacroNativeArgument",1);
    atom->items[1] = symbol(arena,parameter,offset); atom->offset = offset;
    CeTree *tree = ce_alloc(arena,sizeof(*tree));
    tree->symbol = sort; tree->production = CE_TREE_SOURCE; tree->offset = offset;
    /* An expression argument is opaque to the printer; group it so it keeps
       its own precedence wherever the template places it. */
    bool expression = ce_is(m->grammar->symbols[m->grammar->symbols[sort].sort].name,"Expr");
    size_t n = m->unpack.len+parameter.len+4;
    tree->spelling = (CeText){ce_alloc(arena,n),0};
    snprintf((char *)tree->spelling.data,n,expression ? "(%.*s %.*s)" : "%.*s %.*s",
             (int)m->unpack.len,m->unpack.data,(int)parameter.len,parameter.data);
    tree->spelling.len = strlen((char *)tree->spelling.data);
    m->private_atoms = ce_resize(m->private_atoms,m->nprivate+1,sizeof(*m->private_atoms));
    m->private_trees = ce_resize(m->private_trees,m->nprivate+1,sizeof(*m->private_trees));
    m->private_atoms[m->nprivate] = atom; m->private_trees[m->nprivate++] = tree;
    m->safe = ce_resize(m->safe,m->nsafe+1,sizeof(*m->safe)); m->safe[m->nsafe++] = tree;
    if (ce_is(m->grammar->symbols[m->grammar->symbols[sort].sort].name,"DeclarationSpecifiers")) {
        const char *contexts[] = {"SpecifierQualifiers", "TypeName", "Parameter"};
        for (size_t i = 0; i < sizeof(contexts)/sizeof(*contexts); ++i) {
            int target = named(m->grammar,ce_cstr(arena,contexts[i]));
            CeTree *checked; CeError error = {{0},0};
            if (target < 0 || !ce_view_expand(&m->view,written,target,arena,&checked,NULL,&error)) continue;
            CeTree *adapted = ce_alloc(arena,sizeof(*adapted)); *adapted = *tree; adapted->symbol = target;
            m->private_atoms = ce_resize(m->private_atoms,m->nprivate+1,sizeof(*m->private_atoms));
            m->private_trees = ce_resize(m->private_trees,m->nprivate+1,sizeof(*m->private_trees));
            m->private_atoms[m->nprivate] = atom; m->private_trees[m->nprivate++] = adapted;
            m->safe = ce_resize(m->safe,m->nsafe+1,sizeof(*m->safe)); m->safe[m->nsafe++] = adapted;
        }
    }
    return atom;
}
static bool helper(MacroRule *r, CeMacros *m, CeText name, const CeText *parameters,
                    size_t arity, const char *body, size_t length, CeError *error) {
    char *buffer = NULL; size_t size = 0; FILE *file = open_memstream(&buffer,&size);
    if (!file) return ce_error(error,0,"cannot allocate native macro helper");
    fprintf(file,"#define %.*s(",(int)name.len,name.data);
    for (size_t i = 0; i < arity; ++i)
        fprintf(file,"%s%.*s",i ? "," : "",(int)parameters[i].len,parameters[i].data);
    fputs(") \\\n",file);
    for (size_t i = 0; i < length; ++i) {
        if (body[i] == '\n') fputs(" \\\n",file); else fputc(body[i],file);
    }
    fputc('\n',file);
    bool ok = !ferror(file); ok = fclose(file) == 0 && ok;
    if (ok) {
        r->helpers = ce_resize(r->helpers,r->nhelpers+1,sizeof(*r->helpers));
        r->helpers[r->nhelpers++] = ce_text(&m->arena,buffer,size);
    } else ce_error(error,0,"cannot write native macro helper");
    free(buffer); return ok;
}
static unsigned expand_rule(CeMacros *m, const CeAtom *a, int sort, int parent, CeArena *arena,
                          CeTree **out, CeAtomDecoder recurse, void *context, CeError *error, size_t depth) {
    if (!ce_macros_accepts(m,a,sort)) return CE_EXTENSION_UNHANDLED;
    MacroRule *r = NULL;
    for (size_t i = 0; i < m->len; ++i)
        if (same(m->grammar,sort,m->rules[i].result) && ce_equal(a->items[0]->text,m->rules[i].head)) { r=&m->rules[i]; break; }
    if (a->len != r->arity+1) {
        ce_error(error,a->offset,"macro %.*s requires %zu arguments",(int)r->head.len,r->head.data,r->arity);
        return CE_EXTENSION_ERROR;
    }
    bool expression = ce_is(m->grammar->symbols[m->grammar->symbols[sort].sort].name,"Expr");
    if (!m->unpack.len) {
        m->unpack = fresh(m); m->diagnostic_begin = fresh(m); m->diagnostic_end = fresh(m);
    }
    CeAtom **arguments = ce_alloc(arena,r->arity*sizeof(*arguments));
    CeTree **trees = ce_alloc(arena,r->arity*sizeof(*trees));
    CeTree **originals = ce_alloc(arena,r->arity*sizeof(*originals));
    CeText *parameters = ce_alloc(arena,r->arity*sizeof(*parameters));
    CeText *type_names = ce_alloc(arena,r->arity*sizeof(*type_names));
    CeText *names = ce_alloc(arena,r->arity*sizeof(*names)), *locals = ce_alloc(arena,r->nlocals*sizeof(*locals));
    for (size_t i = 0; i < r->nlocals; ++i) locals[i] = fresh(m);
    for (size_t i = 0; i < r->arity; ++i) {
        MacroArgument *arg = &r->arguments[i];
        if (!recurse(context,a->items[i+1],arg->sort,parent,arena,&originals[i],error,depth+1)) {
            char detail[sizeof(error->message)]; memcpy(detail,error->message,sizeof(detail));
            CeText expected = m->grammar->symbols[arg->sort].name;
            ce_error(error,error->offset,"macro %.*s argument %.*s requires sort %.*s: %.240s",
                (int)r->head.len,r->head.data,(int)arg->name.len,arg->name.data,
                (int)expected.len,expected.data,detail);
            return CE_EXTENSION_ERROR;
        }
        if (!safe_tree(m,originals[i])) {
            ce_error(error,a->items[i+1]->offset,"typed macro argument cannot contain opaque native source");
            return CE_EXTENSION_ERROR;
        }
        parameters[i] = fresh(m);
        arguments[i] = private_argument(m,arena,arg->sort,parameters[i],a->items[i+1]->offset,a->items[i+1]);
        trees[i] = m->private_trees[m->nprivate-1];
        if (arg->category == CATEGORY_SYNTAX) continue;
        type_names[i] = fresh(m);
        if (arg->evaluation == ARG_ONCE) {
            names[i] = fresh(m); arguments[i] = symbol(arena,names[i],a->items[i+1]->offset);
            if (arg->category != CATEGORY_VALUE) {
                CeAtom *indirection = list(arena,"*",1); indirection->items[1] = arguments[i]; arguments[i] = indirection;
            }
        }
    }
    Instance instance = {.macros=m,.rule=r,.arguments=arguments,.locals=locals,.arena=arena};
    CeAtom *body = instantiate(&instance,r->body,error);
    CeTree *body_tree;
    if (!body || !recurse(context,body,sort,parent,arena,&body_tree,error,depth+1)) return CE_EXTENSION_ERROR;
    char *buffer = NULL; size_t length = 0; FILE *file = open_memstream(&buffer,&length);
    if (!file) { ce_error(error,a->offset,"cannot allocate macro output"); return CE_EXTENSION_ERROR; }
    if (expression) fputs("__extension__ (",file);
    fputs("{\n",file); bool ok = true;
    for (size_t i = 0; i < r->nports; ++i)
        if (r->ports[i].intrinsic && r->ports[i].used) guard(m,file,r->ports[i].name);
    for (size_t i = 0; i < r->nlocals; ++i) guard(m,file,locals[i]);
    for (size_t i = 0; i < r->arity && ok; ++i) {
        MacroArgument *arg = &r->arguments[i];
        if (arg->category == CATEGORY_SYNTAX) continue;
        guard(m,file,type_names[i]);
        CeAtom *actual_type = instantiate(&instance,arg->type,error);
        ok = actual_type && type_alias(m,arena,actual_type,type_names[i],file,error); fputc('\n',file);
        if (!ok) break;
        fprintf(file,"_Static_assert(sizeof(%.*s), \"Cesta macro CType must have constant object size\");\n",
                (int)type_names[i].len,type_names[i].data);
        /* These expressions intentionally ask for a type without evaluating
           the user's effect. Keep suppression local to generated admission;
           diagnostics on executable template code remain enabled. */
        admission_diagnostic(m,file,true);
        fputs("_Static_assert(_Generic(",file);
        fputs(arg->category == CATEGORY_VALUE ? "(" : "&(",file);
        ok = print_tree(m,trees[i],file,error);
        fprintf(file,"), %.*s%s: 1, default: 0), \"Cesta macro argument type\");\n",
                (int)type_names[i].len,type_names[i].data,arg->category == CATEGORY_VALUE ? "" : "*");
        if (arg->category == CATEGORY_ADDRESSABLE) {
            fputs("(void)sizeof &(",file); ok = ok && print_tree(m,trees[i],file,error); fputs(");\n",file);
        } else if (arg->category == CATEGORY_MODIFIABLE) {
            fputs("(void)sizeof ((",file); ok = ok && print_tree(m,trees[i],file,error);
            fputs(") = (",file); ok = ok && print_tree(m,trees[i],file,error); fputs("));\n",file);
        }
        admission_diagnostic(m,file,false);
        if (arg->evaluation == ARG_ONCE) {
            guard(m,file,names[i]);
            fprintf(file,"%.*s %s%.*s = %s(",(int)type_names[i].len,type_names[i].data,
                arg->category == CATEGORY_VALUE ? "" : "*",(int)names[i].len,names[i].data,
                arg->category == CATEGORY_VALUE ? "" : "&");
            ok = ok && print_tree(m,trees[i],file,error); fputs(");\n",file);
        }
    }
    if (ok && expression) {
        CeText result_type = fresh(m); guard(m,file,result_type);
        CeAtom *actual_type = instantiate(&instance,r->type,error);
        ok = actual_type && type_alias(m,arena,actual_type,result_type,file,error); fputc('\n',file);
        fprintf(file,"_Static_assert(sizeof(%.*s), \"Cesta macro CType must have constant object size\");\n",
                (int)result_type.len,result_type.data);
        CeText result_helper = fresh(m), result_parameter = fresh(m);
        char *result_body = NULL; size_t result_size = 0;
        FILE *result_file = open_memstream(&result_body,&result_size);
        if (!result_file) ok = ce_error(error,a->offset,"cannot allocate result admission helper");
        else {
            admission_diagnostic(m,result_file,true);
            fprintf(result_file,"_Static_assert(_Generic((%.*s %.*s), %.*s: 1, default: 0), \"Cesta macro result type\");\n",
                (int)m->unpack.len,m->unpack.data,(int)result_parameter.len,result_parameter.data,
                (int)result_type.len,result_type.data);
            admission_diagnostic(m,result_file,false);
            fprintf(result_file,"%.*s %.*s",(int)m->unpack.len,m->unpack.data,
                    (int)result_parameter.len,result_parameter.data);
            ok = !ferror(result_file) && ok; ok = fclose(result_file) == 0 && ok;
            if (ok) ok = helper(r,m,result_helper,&result_parameter,1,result_body,result_size,error);
            free(result_body);
            fprintf(file,"%.*s((",(int)result_helper.len,result_helper.data);
            if (ok) ok = print_tree(m,body_tree,file,error);
            fputs("))",file);
        }
    }
    if (ok && !expression) ok = print_tree(m,body_tree,file,error);
    if (expression) fputs(";\n})",file); else fputs("\n}",file);
    ok = !ferror(file) && ok; ok = fclose(file) == 0 && ok;
    if (!ok) { free(buffer); return CE_EXTENSION_ERROR; }
    CeText helper_name = fresh(m);
    ok = helper(r,m,helper_name,parameters,r->arity,buffer,length,error); free(buffer);
    if (!ok) return CE_EXTENSION_ERROR;
    buffer = NULL; length = 0; file = open_memstream(&buffer,&length);
    if (!file) { ce_error(error,a->offset,"cannot allocate native macro call"); return CE_EXTENSION_ERROR; }
    fprintf(file,"%.*s(",(int)helper_name.len,helper_name.data);
    for (size_t i = 0; i < r->arity && ok; ++i) {
        if (i) fputc(',',file);
        fputc('(',file); ok = print_tree(m,originals[i],file,error); fputc(')',file);
    }
    fputc(')',file); ok = !ferror(file) && ok; ok = fclose(file) == 0 && ok;
    if (!ok) { free(buffer); return CE_EXTENSION_ERROR; }
    CeTree *tree = ce_alloc(arena,sizeof(*tree));
    tree->symbol = sort; tree->production = CE_TREE_SOURCE; tree->offset = a->offset;
    tree->spelling = ce_text(arena,buffer,length); free(buffer); *out = tree;
    m->safe = ce_resize(m->safe,m->nsafe+1,sizeof(*m->safe)); m->safe[m->nsafe++] = tree;
    return CE_EXTENSION_EXPANDED;
}
/* ---- Readable program export ------------------------------------------------
   A use expands in place as structured C: each evaluated argument is acquired
   once, in declared order, into a named local whose exact type is checked by
   _Generic on that local, so argument text is never repeated. Template locals
   keep their module names, suffixed only where a name the unit writes or a
   native macro mentions would be shadowed. */
static CeAtom *with_declarator(CeArena *arena, const CeAtom *a, CeAtom *name, bool place, size_t *uses);
static CeAtom *address_of_object(CeAtom *a);
static bool taken_name(CeText *taken, size_t ntaken, CeText name) {
    for (size_t i = 0; i < ntaken; ++i) if (ce_equal(taken[i],name)) return true;
    return false;
}
static CeText pick(CeMacros *m, CeArena *arena, CeText base, CeText **taken, size_t *ntaken) {
    for (unsigned suffix = 0;; ++suffix) {
        CeText name = base;
        if (suffix) {
            size_t n = base.len+16; char *buffer = ce_alloc(arena,n);
            snprintf(buffer,n,"%.*s_%u",(int)base.len,base.data,suffix); name = ce_cstr(arena,buffer);
        }
        bool clash = taken_name(*taken,*ntaken,name);
        for (size_t i = 0; i < m->nvisible && !clash; ++i) clash = ce_equal(m->visible[i],name);
        if (clash) continue;
        *taken = ce_resize(*taken,*ntaken+1,sizeof(**taken)); (*taken)[(*ntaken)++] = name;
        return name;
    }
}
static void template_names(const CeAtom *a, CeText **taken, size_t *ntaken) {
    if (a->kind == CE_ATOM_LIST) {
        if (form(a,"Argument") || form(a,"TypeArgument") || form(a,"LocalRef")) return;
        for (size_t i = 0; i < a->len; ++i) template_names(a->items[i],taken,ntaken);
    } else if (a->kind == CE_ATOM_SYMBOL && identifier(a->text) && !taken_name(*taken,*ntaken,a->text)) {
        *taken = ce_resize(*taken,*ntaken+1,sizeof(**taken)); (*taken)[(*ntaken)++] = a->text;
    }
}
/* The caller's own tree for an argument. Declaration specifiers are also
   read in the type-name and parameter positions the template may use. */
static CeAtom *own_argument(CeMacros *m, CeArena *arena, CeTree *tree, int sort, const CeAtom *written) {
    CeAtom *atom = list(arena,"MacroNativeArgument",1); atom->items[1] = word(arena,"original"); atom->offset = written->offset;
    m->private_atoms = ce_resize(m->private_atoms,m->nprivate+1,sizeof(*m->private_atoms));
    m->private_trees = ce_resize(m->private_trees,m->nprivate+1,sizeof(*m->private_trees));
    m->private_atoms[m->nprivate] = atom; m->private_trees[m->nprivate++] = tree;
    m->safe = ce_resize(m->safe,m->nsafe+1,sizeof(*m->safe)); m->safe[m->nsafe++] = tree;
    if (ce_is(m->grammar->symbols[m->grammar->symbols[sort].sort].name,"DeclarationSpecifiers")) {
        const char *contexts[] = {"SpecifierQualifiers", "TypeName", "Parameter"};
        for (size_t i = 0; i < sizeof(contexts)/sizeof(*contexts); ++i) {
            int target = named(m->grammar,ce_cstr(arena,contexts[i]));
            CeTree *checked; CeError ignored = {{0},0};
            if (target < 0 || !ce_view_expand(&m->view,written,target,arena,&checked,NULL,&ignored)) continue;
            m->private_atoms = ce_resize(m->private_atoms,m->nprivate+1,sizeof(*m->private_atoms));
            m->private_trees = ce_resize(m->private_trees,m->nprivate+1,sizeof(*m->private_trees));
            m->private_atoms[m->nprivate] = atom; m->private_trees[m->nprivate++] = checked;
            m->safe = ce_resize(m->safe,m->nsafe+1,sizeof(*m->safe)); m->safe[m->nsafe++] = checked;
        }
    }
    return atom;
}
/* Copies of these spellings can expand differently. */
static bool mentions_macro(CeMacros *m, const CeAtom *a) {
    if (a->kind == CE_ATOM_LIST) {
        for (size_t i = 0; i < a->len; ++i) if (mentions_macro(m,a->items[i])) return true;
        return false;
    }
    if (a->kind != CE_ATOM_SYMBOL) return false;
    if (ce_is(a->text,"__COUNTER__") || ce_is(a->text,"__LINE__")) return true;
    for (size_t i = 0; i < m->npositional; ++i) if (ce_equal(m->positional[i],a->text)) return true;
    return false;
}
/* The type name for an exact _Generic association, declaring a typedef for
   types with declarators. */
static CeAtom *admitted_type(CeMacros *m, CeArena *arena, Instance *in, const CeAtom *ctype, bool place,
                             CeText base, CeText **taken, size_t *ntaken, CeAtom **typedef_item, CeError *error) {
    CeAtom *type = instantiate(in,ctype,error);
    if (!type) return NULL;
    CeAtom *name = type->items[1]; *typedef_item = NULL;
    /* Pointer chains are written inline, as in int **; other declarators
       get a named typedef. */
    size_t depth = 0;
    if (type->len == 3 && type->items[2]->kind == CE_ATOM_LIST && type->items[2]->len == 2 &&
        type->items[2]->items[0]->kind == CE_ATOM_SYMBOL && type->items[2]->items[1]->kind == CE_ATOM_SYMBOL &&
        ce_is(type->items[2]->items[1]->text,"TypeSlot")) {
        CeText stars = type->items[2]->items[0]->text;
        depth = ce_is(stars,"*") ? 1 : ce_is(stars,"**") ? 2 : ce_is(stars,"***") ? 3 : 0;
    }
    if (depth && depth+place <= 3) {
        CeAtom *pointer = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},2);
        pointer->items[0] = type->items[1];
        pointer->items[1] = word(arena,depth+place == 1 ? "*" : depth+place == 2 ? "**" : "***");
        return pointer;
    }
    if (type->len == 3) {
        size_t n = base.len+6; char *buffer = ce_alloc(arena,n);
        snprintf(buffer,n,"%.*s_type",(int)base.len,base.data);
        CeText alias = pick(m,arena,ce_cstr(arena,buffer),taken,ntaken); size_t uses = 0;
        CeAtom *declarator = with_declarator(arena,type->items[2],symbol(arena,alias,0),false,&uses);
        if (uses != 1) { ce_error(error,ctype->offset,"CType declarator must contain exactly one TypeSlot"); return NULL; }
        CeAtom *item = list(arena,"typedef",2); item->items[1] = type->items[1]; item->items[2] = declarator;
        *typedef_item = item; name = symbol(arena,alias,0);
    }
    if (!place) return name;
    CeAtom *pointer = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},2);
    pointer->items[0] = name; pointer->items[1] = word(arena,"*"); return pointer;
}
static CeAtom *admission(CeArena *arena, CeAtom *subject, CeAtom *type, const char *message) {
    CeAtom *association = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},2);
    association->items[0] = type; association->items[1] = word(arena,"1");
    CeAtom *fallback = list(arena,"default",1); fallback->items[1] = word(arena,"0");
    CeAtom *generic = list(arena,"_Generic",3);
    generic->items[1] = subject; generic->items[2] = association; generic->items[3] = fallback;
    size_t n = strlen(message)+3; char *quoted = ce_alloc(arena,n); snprintf(quoted,n,"\"%s\"",message);
    CeAtom *check = list(arena,"_Static_assert",2); check->items[1] = generic;
    check->items[2] = ce_atom_new(arena,CE_ATOM_TEXT,ce_cstr(arena,quoted),0);
    return check;
}
/* An expression whose second, unevaluated copy in _Generic is harmless. */
static bool effect_free(const CeAtom *a) {
    static const char *const effects[] = {"=","+=","-=","*=","/=","%=","<<=",">>=","&=","^=","|=",
                                          "++","--","post++","post--","call","block","MacroNativeArgument"};
    if (a->kind != CE_ATOM_LIST) return true;
    if (a->len && a->items[0]->kind == CE_ATOM_SYMBOL)
        for (size_t i = 0; i < sizeof(effects)/sizeof(*effects); ++i) if (ce_is(a->items[0]->text,effects[i])) return false;
    for (size_t i = 1; i < a->len; ++i) if (!effect_free(a->items[i])) return false;
    return true;
}
typedef struct { CeAtom **items; size_t len; } Block;
/* Placeholder atoms of an expansion and the structure each one stands for. */
typedef struct { const CeAtom **from, **to; size_t len; } Resolution;
static void resolve_as(Resolution *r, const CeAtom *from, const CeAtom *to) {
    r->from = ce_resize(r->from,r->len+1,sizeof(*r->from)); r->to = ce_resize(r->to,r->len+1,sizeof(*r->to));
    r->from[r->len] = from; r->to[r->len++] = to;
}
/* Equality of two atoms as written: placeholders compare by what they denote,
   not by their shared placeholder spelling. */
static bool resolved_equal(const CeAtom *a, const CeAtom *b, const Resolution *r) {
    for (size_t i = 0; i < r->len; ++i) if (r->from[i] == a) { a = r->to[i]; break; }
    for (size_t i = 0; i < r->len; ++i) if (r->from[i] == b) { b = r->to[i]; break; }
    if (a->kind != b->kind) return false;
    if (a->kind != CE_ATOM_LIST) return ce_equal(a->text,b->text);
    if (a->len != b->len) return false;
    for (size_t i = 0; i < a->len; ++i) if (!resolved_equal(a->items[i],b->items[i],r)) return false;
    return true;
}
/* Write the expansion as built, with every placeholder replaced by the
   written argument or block it denotes. */
static void write_resolved(FILE *f, const CeAtom *a, const Resolution *r) {
    for (size_t i = 0; i < r->len; ++i) if (r->from[i] == a) { write_resolved(f,r->to[i],r); return; }
    if (a->kind != CE_ATOM_LIST) { ce_atom_write(a,f); return; }
    fputc('(',f);
    for (size_t i = 0; i < a->len; ++i) { if (i) fputc(' ',f); write_resolved(f,a->items[i],r); }
    fputc(')',f);
}
static void add(Block *b, CeAtom *item) { b->items = ce_resize(b->items,b->len+1,sizeof(*b->items)); b->items[b->len++] = item; }
static unsigned expand_readable(CeMacros *m, const CeAtom *a, int sort, int parent, CeArena *arena,
                                CeTree **out, CeAtomDecoder recurse, void *context, CeError *error, size_t depth) {
    MacroRule *r = NULL;
    for (size_t i = 0; i < m->len; ++i)
        if (same(m->grammar,sort,m->rules[i].result) && ce_equal(a->items[0]->text,m->rules[i].head)) { r=&m->rules[i]; break; }
    if (a->len != r->arity+1) {
        ce_error(error,a->offset,"macro %.*s requires %zu arguments",(int)r->head.len,r->head.data,r->arity);
        return CE_EXTENSION_ERROR;
    }
    if (!r->gnu) {
        ce_error(error,a->offset,"readable export of %.*s needs the gnu11 profile; cesta expand keeps its exact form",
                 (int)r->head.len,r->head.data);
        return CE_EXTENSION_ERROR;
    }
    bool expression = ce_is(m->grammar->symbols[m->grammar->symbols[sort].sort].name,"Expr");
    CeAtom **arguments = ce_alloc(arena,r->arity*sizeof(*arguments)), **owned = ce_alloc(arena,r->arity*sizeof(*owned));
    CeText *locals = ce_alloc(arena,(r->nlocals+1)*sizeof(*locals));
    CeText *taken = NULL; size_t ntaken = 0; Block block = {0}; unsigned result = CE_EXTENSION_ERROR;
    Resolution resolution = {0};
    template_names(r->body,&taken,&ntaken);
    size_t seeded = ntaken;
    Instance types = {.macros=m,.rule=r,.arguments=arguments,.locals=locals,.arena=arena};
    for (size_t i = 0; i < r->arity; ++i) {
        MacroArgument *arg = &r->arguments[i]; CeTree *original;
        if (!recurse(context,a->items[i+1],arg->sort,parent,arena,&original,error,depth+1)) goto done;
        if (!safe_tree(m,original)) { ce_error(error,a->items[i+1]->offset,"typed macro argument cannot contain opaque native source"); goto done; }
        CeAtom *value = owned[i] = own_argument(m,arena,original,arg->sort,a->items[i+1]);
        resolve_as(&resolution,value,a->items[i+1]);
        if (arg->category == CATEGORY_SYNTAX) {
            if (arg->uses > 1 && mentions_macro(m,a->items[i+1])) {
                ce_error(error,a->items[i+1]->offset,"argument %.*s is copied and names a preprocessor counter; cesta expand keeps one expansion",
                         (int)arg->name.len,arg->name.data);
                goto done;
            }
            arguments[i] = value; continue;
        }
        if (arg->evaluation != ARG_ONCE) {
            ce_error(error,a->items[i+1]->offset,"readable export of deferred or repeated arguments is not supported; cesta expand keeps the exact form");
            goto done;
        }
    }
    /* Acquire evaluated arguments in declared order. */
    for (size_t i = 0; i < r->arity; ++i) {
        MacroArgument *arg = &r->arguments[i];
        if (arg->category == CATEGORY_SYNTAX) continue;
        bool place = arg->category != CATEGORY_VALUE;
        CeText name = pick(m,arena,arg->name,&taken,&ntaken);
        CeAtom *source = owned[i], *initial = source;
        const CeAtom *written = a->items[i+1];
        if (place && written->kind == CE_ATOM_LIST && written->len == 2 && written->items[0]->kind == CE_ATOM_SYMBOL &&
            ce_is(written->items[0]->text,"*")) {
            /* &*E is E without evaluating either operator (C11 6.5.3.2). */
            CeTree *pointer;
            if (!recurse(context,written->items[1],arg->sort,parent,arena,&pointer,error,depth+1)) goto done;
            initial = own_argument(m,arena,pointer,arg->sort,written->items[1]);
            resolve_as(&resolution,initial,written->items[1]);
        } else if (place) { initial = list(arena,"&",1); initial->items[1] = source; }
        CeAtom *declaration = list(arena,"var",3); declaration->items[1] = word(arena,"__auto_type");
        declaration->items[2] = symbol(arena,name,0); declaration->items[3] = initial; add(&block,declaration);
        CeAtom *typedef_item; char message[160];
        CeAtom *type = admitted_type(m,arena,&types,arg->type,place,name,&taken,&ntaken,&typedef_item,error);
        if (!type) goto done;
        if (typedef_item) add(&block,typedef_item);
        snprintf(message,sizeof(message),"%.*s argument %.*s has its declared type",(int)r->head.len,r->head.data,
                 (int)arg->name.len,arg->name.data);
        add(&block,admission(arena,symbol(arena,name,0),type,message));
        if (place) { CeAtom *object = list(arena,"*",1); object->items[1] = symbol(arena,name,0); arguments[i] = object; }
        else arguments[i] = symbol(arena,name,0);
    }
    for (size_t i = 0; i < r->nlocals; ++i) locals[i] = pick(m,arena,r->locals[i].name,&taken,&ntaken);
    Instance instance = {.macros=m,.rule=r,.arguments=arguments,.locals=locals,.arena=arena};
    CeAtom *body = instantiate(&instance,r->body,error);
    if (!body) goto done;
    body = address_of_object(body);
    bool body_block = form(body,"block");
    size_t n = body_block ? body->len-1 : 1; CeAtom *first_template = NULL; size_t before = block.len;
    for (size_t i = 0; i < n; ++i) {
        CeAtom *item = body_block ? body->items[i+1] : body;
        if (expression && i+1 == n) {
            /* The value's declared type, checked without evaluating it twice. */
            CeAtom *value = item, *typedef_item; CeText result_name = {0};
            CeAtom *declared = instantiate(&instance,r->type,error);
            if (!declared) goto done;
            bool fixed = false;
            for (size_t k = 0; k+1 < n && item->kind == CE_ATOM_SYMBOL && declared->len == 2; ++k) {
                const CeAtom *d = body->items[k+1];
                fixed |= form(d,"var") && d->len >= 3 && d->items[2]->kind == CE_ATOM_SYMBOL &&
                    ce_equal(d->items[2]->text,item->text) && resolved_equal(d->items[1],declared->items[1],&resolution);
            }
            if (fixed) { add(&block,item); continue; }
            if (item->kind != CE_ATOM_SYMBOL && !effect_free(item)) {
                result_name = pick(m,arena,ce_cstr(arena,"result"),&taken,&ntaken);
                CeAtom *declaration = list(arena,"var",3); declaration->items[1] = word(arena,"__auto_type");
                declaration->items[2] = symbol(arena,result_name,0); declaration->items[3] = item; add(&block,declaration);
                value = symbol(arena,result_name,0);
            }
            CeAtom *type = admitted_type(m,arena,&instance,r->type,false,ce_cstr(arena,"result"),&taken,&ntaken,&typedef_item,error);
            if (!type) goto done;
            if (typedef_item) add(&block,typedef_item);
            char message[160];
            snprintf(message,sizeof(message),"%.*s result has its declared type",(int)r->head.len,r->head.data);
            add(&block,admission(arena,value,type,message));
            add(&block,value);
        } else add(&block,item);
    }
    if (block.len > before) first_template = block.items[before];
    /* Acquisitions, then the template as its own block: the same nesting as
       the in-place expansion, so unoptimized code keeps its layout too. */
    size_t acquired = 0;
    for (size_t i = 0; i < block.len; ++i) if (block.items[i] == first_template) { acquired = i; break; }
    CeAtom *inner = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},block.len-acquired+1);
    inner->items[0] = word(arena,"block");
    for (size_t i = acquired; i < block.len; ++i) inner->items[i-acquired+1] = block.items[i];
    CeAtom *expansion = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},acquired+2);
    expansion->items[0] = word(arena,"block");
    for (size_t i = 0; i < acquired; ++i) expansion->items[i+1] = block.items[i];
    expansion->items[acquired+1] = inner;
    if (!acquired) expansion = inner;
    else if (!body_block) {
        /* A bare template expression stays in the acquiring block, as in the
           in-place expansion. */
        expansion = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},block.len+1);
        expansion->items[0] = word(arena,"block");
        for (size_t i = 0; i < block.len; ++i) expansion->items[i+1] = block.items[i];
    } else if (expression) {
        /* A block in statement position is a compound statement; the value
           block is expanded as an expression first and placed as one. */
        CeTree *value;
        if (!ce_view_expand_open(&m->view,inner,sort,arena,&value,private_extension,private_accepts,m,error)) goto done;
        CeAtom *placed = list(arena,"MacroNativeArgument",1); placed->items[1] = word(arena,"value");
        m->private_atoms = ce_resize(m->private_atoms,m->nprivate+1,sizeof(*m->private_atoms));
        m->private_trees = ce_resize(m->private_trees,m->nprivate+1,sizeof(*m->private_trees));
        m->private_atoms[m->nprivate] = placed; m->private_trees[m->nprivate++] = value;
        m->safe = ce_resize(m->safe,m->nsafe+1,sizeof(*m->safe)); m->safe[m->nsafe++] = value;
        expansion->items[acquired+1] = placed;
        resolve_as(&resolution,placed,inner);
    }
    expansion->offset = a->offset;
    if (!ce_view_expand_open(&m->view,expansion,sort,arena,out,private_extension,private_accepts,m,error)) goto done;
    m->safe = ce_resize(m->safe,m->nsafe+1,sizeof(*m->safe)); m->safe[m->nsafe++] = *out;
    if (m->plan) {
        /* The rule's identity, the use as written, the names chosen and the
           structure handed to the printer. */
        fputs("(use cesta-export-plan/2 (rule ",m->plan); ce_quote(m->plan,r->path); fputc(' ',m->plan);
        ce_quote(m->plan,r->theory); fputc(' ',m->plan); ce_quote(m->plan,r->label); fputs(") (arguments",m->plan);
        for (size_t i = 0; i < r->arity; ++i) { fputc(' ',m->plan); ce_atom_write(a->items[i+1],m->plan); }
        fputs(") (acquired",m->plan);
        for (size_t i = 0; i < r->arity; ++i) {
            fputc(' ',m->plan);
            if (r->arguments[i].category == CATEGORY_SYNTAX) fputc('-',m->plan);
            else ce_atom_write(arguments[i]->kind == CE_ATOM_LIST ? arguments[i]->items[1] : arguments[i],m->plan);
        }
        fputs(") (locals",m->plan);
        for (size_t i = 0; i < r->nlocals; ++i) { fputc(' ',m->plan); ce_quote(m->plan,locals[i]); }
        fputs(") (picked",m->plan);
        for (size_t i = seeded; i < ntaken; ++i) { fputc(' ',m->plan); ce_quote(m->plan,taken[i]); }
        fprintf(m->plan,") (position %s) (target ",expression ? "expression" : "statement");
        write_resolved(m->plan,expansion,&resolution);
        fputs("))\n",m->plan);
    }
    result = CE_EXTENSION_EXPANDED;
done:
    free(taken); free(block.items); free(resolution.from); free(resolution.to); return result;
}
unsigned ce_macros_expand(CeMacros *m, const CeAtom *a, int sort, int parent, CeArena *arena,
                          CeTree **out, CeAtomDecoder recurse, void *context, CeError *error, size_t depth) {
    if (!m) return CE_EXTENSION_UNHANDLED;
    unsigned private_result = private_extension(m,a,sort,parent,arena,out,recurse,context,error,depth);
    if (private_result != CE_EXTENSION_UNHANDLED) return private_result;
    if (!ce_macros_accepts(m,a,sort)) return CE_EXTENSION_UNHANDLED;
    bool outer = m->expanding == 0;
    if (outer) m->nguards = 0;
    if (m->readable) {
        ++m->expanding;
        unsigned result = expand_readable(m,a,sort,parent,arena,out,recurse,context,error,depth);
        --m->expanding; return result;
    }
    ++m->expanding;
    unsigned result = expand_rule(m,a,sort,parent,arena,out,recurse,context,error,depth);
    --m->expanding;
    if (outer && result == CE_EXTENSION_EXPANDED) {
        char *buffer = NULL; size_t length = 0; FILE *file = open_memstream(&buffer,&length);
        if (!file) { ce_error(error,a->offset,"cannot allocate native name guards"); return CE_EXTENSION_ERROR; }
        for (size_t i = 0; i < m->nguards; ++i) guard(m,file,m->guards[i]);
        fwrite((*out)->spelling.data,1,(*out)->spelling.len,file);
        bool ok = !ferror(file); ok = fclose(file) == 0 && ok;
        if (ok) (*out)->spelling = ce_text(arena,buffer,length);
        else ce_error(error,a->offset,"cannot write native name guards");
        free(buffer); if (!ok) return CE_EXTENSION_ERROR;
    }
    return result;
}
size_t ce_macros_dependencies(const CeMacros *m) { return m ? m->catalog.len : 0; }
CeText ce_macros_dependency(const CeMacros *m, size_t index) { return ce_module_path(m->catalog.modules[index]); }
size_t ce_macros_rules(const CeMacros *m) { return m ? m->len : 0; }
bool ce_macros_prelude(CeMacros *m, size_t begin, size_t end, CeArena *arena,
                       CeText *out, CeError *error) {
    char *buffer = NULL; size_t length = 0; FILE *file = open_memstream(&buffer,&length);
    if (!file) return ce_error(error,0,"cannot allocate definition-site declarations");
    bool ok = true;
    if (!m->prelude_emitted && m->unpack.len) {
        m->prelude_emitted = true;
        guard(m,file,m->unpack); guard(m,file,m->diagnostic_begin); guard(m,file,m->diagnostic_end);
        fprintf(file,"#define %.*s(...) __VA_ARGS__\n",(int)m->unpack.len,m->unpack.data);
        if (m->unguarded)
            fprintf(file,"#define %.*s\n#define %.*s\n",
                (int)m->diagnostic_begin.len,m->diagnostic_begin.data,
                (int)m->diagnostic_end.len,m->diagnostic_end.data);
        else fprintf(file,"#ifdef __clang__\n#define %.*s _Pragma(\"clang diagnostic push\") "
            "_Pragma(\"clang diagnostic ignored \\\"-Wunevaluated-expression\\\"\")\n"
            "#define %.*s _Pragma(\"clang diagnostic pop\")\n#else\n#define %.*s\n#define %.*s\n#endif\n",
            (int)m->diagnostic_begin.len,m->diagnostic_begin.data,(int)m->diagnostic_end.len,m->diagnostic_end.data,
            (int)m->diagnostic_begin.len,m->diagnostic_begin.data,(int)m->diagnostic_end.len,m->diagnostic_end.data);
    }
    for (size_t i = begin; i < end && ok; ++i)
        for (size_t j = 0; j < m->rules[i].nports && ok; ++j) {
            MacroPort *binding = &m->rules[i].ports[j];
            if (!binding->used) continue;
            if (binding->intrinsic) {
                guard(m,file,binding->name);
                fprintf(file,"\n#if !defined(__has_builtin)\n#error Cesta_compiler_requires_builtin_admission\n"
                    "#elif !__has_builtin(%.*s)\n#error Cesta_compiler_builtin_unavailable\n#endif\n",
                    (int)binding->name.len,binding->name.data);
                continue;
            }
            fprintf(file,"#include <%.*s>\n",(int)binding->header.len,binding->header.data);
            /* NativeSymbol captures a C entity, not a preprocessor alias.
               Preserve the caller's macro environment after acquiring it. */
            fprintf(file,"#pragma push_macro(\"%.*s\")\n#undef %.*s\n",
                (int)binding->name.len,binding->name.data,(int)binding->name.len,binding->name.data);
            guard(m,file,binding->type_name); guard(m,file,binding->alias);
            ok = type_alias(m,arena,binding->type,binding->type_name,file,error);
            fprintf(file,"\n_Static_assert(_Generic(&%.*s, %.*s*: 1, default: 0), \"Cesta native port type\");\n",
                (int)binding->name.len,binding->name.data,(int)binding->type_name.len,binding->type_name.data);
            fprintf(file,"static %.*s *const %.*s __attribute__((unused)) = &%.*s;\n",
                (int)binding->type_name.len,binding->type_name.data,(int)binding->alias.len,binding->alias.data,
                (int)binding->name.len,binding->name.data);
            fprintf(file,"#pragma pop_macro(\"%.*s\")\n",(int)binding->name.len,binding->name.data);
        }
    for (size_t i = begin; i < end && ok; ++i)
        for (size_t j = 0; j < m->rules[i].nhelpers; ++j)
            ok = fwrite(m->rules[i].helpers[j].data,1,m->rules[i].helpers[j].len,file) == m->rules[i].helpers[j].len;
    ok = !ferror(file) && ok; ok = fclose(file) == 0 && ok;
    if (ok) *out = ce_text(arena,buffer,length);
    free(buffer); return ok;
}
bool ce_macros_metadata(const CeMacros *m, FILE *file, CeError *error) {
    fputs("(macros cesta-macro/1",file);
    for (size_t i = 0; i < m->len; ++i) {
        const MacroRule *r = &m->rules[i];
        fputs("\n  (rule ",file); ce_quote(file,r->label); fputc(' ',file); ce_quote(file,r->head);
        fputc(' ',file); ce_quote(file,m->grammar->symbols[r->result].name);
        fputc(' ',file); ce_atom_write(r->contract,file); fputc(')',file);
    }
    fputs(")\n",file); return !ferror(file) || ce_error(error,0,"cannot write macro metadata");
}
int ce_macro_command(int argc, char **argv) {
    if (argc != 4) { fputs("usage: cesta check-macros MODULE THEORY\n",stderr); return 1; }
    CeGrammar grammar = {0}; CeError error = {{0},0}; CeMacros *macros = NULL; int result = 1;
    if (!ce_builtin_language(&grammar,true,&error) || !(macros=ce_macros_new(&grammar,NULL,&error)) ||
        !ce_macros_load(macros,argv[2],argv[3],&error) || !ce_macros_metadata(macros,stdout,&error))
        fprintf(stderr,"%s: byte %zu: %s\n",argv[2],error.offset,error.message);
    else result = 0;
    ce_macros_free(macros); ce_grammar_free(&grammar); return result;
}

/* ---- Library export --------------------------------------------------------
   A concrete instance of a constructor becomes one C definition. Functions
   are used where the whole contract survives the move into a helper: every
   evaluated argument is acquired once and the template has no caller
   context. A C call then supplies one evaluation per argument, in an order C
   leaves unspecified, with the conversions of its prototype; the header API
   states that contract. Syntax-sensitive constructs become function-like
   macros whose text is printed from the same template. */

static bool contains_head(const CeAtom *a, const char *const *heads, size_t n) {
    if (a->kind == CE_ATOM_SYMBOL) {
        for (size_t i = 0; i < n; ++i) if (ce_is(a->text,heads[i])) return true;
        return false;
    }
    if (a->kind != CE_ATOM_LIST) return false;
    for (size_t i = 0; i < a->len; ++i) if (contains_head(a->items[i],heads,n)) return true;
    return false;
}
/* name, or with place set a pointer to name, at the declarator's TypeSlot.
   Compact C spells stacked pointers as one "*", "**" or "***" node and
   needs parentheses elsewhere. */
static CeAtom *with_declarator(CeArena *arena, const CeAtom *a, CeAtom *name, bool place, size_t *uses) {
    /* A caller's argument is identified by its atom; keep it, do not copy it. */
    if (form(a,"MacroNativeArgument")) return (CeAtom *)a;
    if (a->kind == CE_ATOM_SYMBOL && ce_is(a->text,"TypeSlot")) {
        ++*uses;
        if (!place) return name;
        CeAtom *pointer = list(arena,"*",1); pointer->items[1] = name;
        CeAtom *group = list(arena,"c:ParenthesizedDeclarator",1); group->items[1] = pointer; return group;
    }
    if (place && a->kind == CE_ATOM_LIST && a->len == 2 && a->items[0]->kind == CE_ATOM_SYMBOL &&
        a->items[1]->kind == CE_ATOM_SYMBOL && ce_is(a->items[1]->text,"TypeSlot") &&
        (ce_is(a->items[0]->text,"*") || ce_is(a->items[0]->text,"**"))) {
        ++*uses;
        CeAtom *pointer = list(arena,ce_is(a->items[0]->text,"*") ? "**" : "***",1); pointer->items[1] = name;
        return pointer;
    }
    CeAtom *out = ce_atom_new(arena,a->kind,ce_text(arena,a->text.data,a->text.len),a->len);
    out->offset = a->offset;
    for (size_t i = 0; i < a->len; ++i) out->items[i] = with_declarator(arena,a->items[i],name,place,uses);
    return out;
}
/* (CType SPECIFIERS [DECLARATOR]) declaring name. */
static CeAtom *declaration_of(Instance *in, const CeAtom *ctype, CeAtom *name, bool place, CeError *error) {
    CeAtom *type = instantiate(in,ctype,error);
    if (!type) return NULL;
    size_t uses = 0;
    CeAtom *declarator = name;
    if (type->len == 3) declarator = with_declarator(in->arena,type->items[2],name,place,&uses);
    else if (place) { declarator = list(in->arena,"*",1); declarator->items[1] = name; }
    if (type->len == 3 && uses != 1) {
        ce_error(error,ctype->offset,"CType declarator must contain exactly one TypeSlot"); return NULL;
    }
    CeAtom *out = ce_atom_new(in->arena,CE_ATOM_LIST,(CeText){0},2);
    out->items[0] = type->items[1]; out->items[1] = declarator; return out;
}
static void note(CeText **names, size_t *len, CeText name) {
    for (size_t i = 0; i < *len; ++i) if (ce_equal((*names)[i],name)) return;
    *names = ce_resize(*names,*len+1,sizeof(**names)); (*names)[(*len)++] = name;
}
static bool written_name(const CeAtom *a, CeText name) {
    if (a->kind != CE_ATOM_LIST) return a->kind == CE_ATOM_SYMBOL && ce_equal(a->text,name);
    for (size_t i = 0; i < a->len; ++i) if (written_name(a->items[i],name)) return true;
    return false;
}
/* Readable names: the module's own spelling, suffixed only on collision. */
static CeText readable(CeMacros *m, CeArena *arena, CeText base, const CeText *taken, size_t ntaken,
                       const CeAtom *types) {
    for (unsigned suffix = 0;; ++suffix) {
        CeText name = base;
        if (suffix) {
            size_t n = base.len+16; char *buffer = ce_alloc(arena,n);
            snprintf(buffer,n,"%.*s_%u",(int)base.len,base.data,suffix);
            name = ce_cstr(arena,buffer);
        }
        bool free_name = !written_name(types,name);
        for (size_t i = 0; i < ntaken && free_name; ++i) free_name = !ce_equal(taken[i],name);
        (void)m;
        if (free_name) return name;
    }
}

/* A header plan record: the rule, the instance's type parameters, the names
   chosen in order, and the structure handed to the printer (for a macro, its
   expression before it becomes text). */
static void header_plan(FILE *plan, const MacroRule *r, const CeMacroExportRequest *request,
                        const CeText *picked, size_t npicked, const CeAtom *emitted) {
    fputs("(header cesta-header-plan/1 (rule ",plan); ce_quote(plan,r->path); fputc(' ',plan);
    ce_quote(plan,r->theory); fputc(' ',plan); ce_quote(plan,r->label);
    fprintf(plan,") (%s %.*s) (types",request->function ? "function" : "macro",(int)request->name.len,request->name.data);
    for (size_t i = 0; i < request->types->len; ++i) { fputc(' ',plan); ce_atom_write(request->types->items[i],plan); }
    fputs(") (picked",plan);
    for (size_t i = 0; i < npicked; ++i) { fputc(' ',plan); ce_quote(plan,picked[i]); }
    fprintf(plan,") (%s ",request->function ? "definition" : "expression");
    ce_atom_write(emitted,plan);
    fputs("))\n",plan);
}

/* A function-like macro that is one expression, usable wherever a call is.
   Static assertions become sizeof checks of a structure holding them; its
   member takes the macro's own name, which cannot re-expand inside itself. */
static bool macro_definition(CeMacros *m, MacroRule *r, const CeMacroExportRequest *request, CeArena *arena,
                             CeAtom *body, CeAtom **arguments, bool expression, const CeText *picked, size_t npicked,
                             CeMacroExport *out, CeError *error) {
    if (r->nlocals) return ce_error(error,request->offset,"macro exports cannot declare locals; export %.*s as a function",
                                    (int)r->label.len,r->label.data);
    int expr_sort = named(m->grammar,ce_cstr(arena,"Expr"));
    const CeAtom *block = body->kind == CE_ATOM_LIST && body->len && body->items[0]->kind == CE_ATOM_SYMBOL &&
        ce_is(body->items[0]->text,"block") ? body : NULL;
    if (expression && block) return ce_error(error,request->offset,"%.*s needs a statement expression and stays an in-place expansion",
                                             (int)r->label.len,r->label.data);
    size_t n = block ? block->len-1 : 1; CeAtom *combined = NULL;
    for (size_t i = 0; i < n; ++i) {
        CeAtom *item = block ? block->items[i+1] : body, *part;
        if (item->kind == CE_ATOM_LIST && item->len == 3 && item->items[0]->kind == CE_ATOM_SYMBOL &&
            ce_is(item->items[0]->text,"_Static_assert")) {
            CeAtom *member = list(arena,"char",1); member->items[1] = symbol(arena,request->name,0);
            CeAtom *record = list(arena,"struct",2); record->items[1] = item; record->items[2] = member;
            CeAtom *size = list(arena,"sizeof",2); size->items[1] = word(arena,"type"); size->items[2] = record;
            part = list(arena,"cast",2); part->items[1] = word(arena,"void"); part->items[2] = size;
        } else {
            CeTree *probe; CeError ignored = {{0},0};
            if (!ce_view_expand_open(&m->view,item,expr_sort,arena,&probe,private_extension,private_accepts,m,&ignored))
                return ce_error(error,request->offset,"%.*s contains a statement and stays an in-place expansion",
                                (int)r->label.len,r->label.data);
            part = item;
        }
        if (!combined) combined = part;
        else { CeAtom *comma = list(arena,",",2); comma->items[1] = combined; comma->items[2] = part; combined = comma; }
    }
    if (request->plan) header_plan(request->plan,r,request,picked,npicked,combined);
    /* One top-level part per continuation line, the whole grouped once. */
    size_t nparts = 1;
    for (const CeAtom *a = combined; a->kind == CE_ATOM_LIST && a->len == 3 && ce_is(a->items[0]->text,","); a = a->items[1]) ++nparts;
    const CeAtom **parts = ce_alloc(arena,nparts*sizeof(*parts)); size_t k = nparts;
    const CeAtom *rest = combined;
    while (k > 1) { parts[--k] = rest->items[2]; rest = rest->items[1]; }
    parts[0] = rest;
    char *text = NULL; size_t text_length = 0; FILE *joined = open_memstream(&text,&text_length);
    if (!joined) return ce_error(error,request->offset,"cannot allocate macro text");
    fprintf(joined,"#define %.*s(",(int)request->name.len,request->name.data);
    bool first = true;
    for (size_t j = 0; j < r->arity; ++j) {
        if (r->arguments[j].category == CATEGORY_SYNTAX &&
            ce_is(m->grammar->symbols[m->grammar->symbols[r->arguments[j].sort].sort].name,"DeclarationSpecifiers")) continue;
        fprintf(joined,"%s%.*s",first ? "" : ", ",(int)arguments[j]->text.len,arguments[j]->text.data); first = false;
    }
    fputs(") \\\n",joined);
    bool ok = true; const CeDFA *lexer = ce_lazy_lexer(m->lexer,m->grammar);
    if (!lexer) ok = ce_error(error,request->offset,"cannot prepare the C lexer");
    for (size_t part = 0; ok && part < nparts; ++part) {
        CeTree *tree; char *buffer = NULL; size_t length = 0; FILE *file;
        if (!ce_view_expand_open(&m->view,parts[part],expr_sort,arena,&tree,private_extension,private_accepts,m,error)) { ok = false; break; }
        if (!(file = open_memstream(&buffer,&length))) { ok = ce_error(error,request->offset,"cannot allocate macro text"); break; }
        ok = ce_tree_print_open_shared(m->grammar,m->lexer,tree,expr_sort,file,error);
        ok = fclose(file) == 0 && ok;
        CeTokens tokens = {0};
        if (ok) ok = ce_dfa_lex(m->grammar,lexer,(CeText){(unsigned char *)buffer,length},&tokens,error);
        fputs(part ? "     " : "    (",joined);
        /* Printed tokens rejoin on one line. A parameter is grouped where used
           unless it is already a whole call argument. */
        for (size_t i = 0; ok && i < tokens.len; ++i) {
            CeText token = tokens.data[i].text;
            if (i && tokens.data[i].offset > tokens.data[i-1].offset+tokens.data[i-1].text.len) fputc(' ',joined);
            bool parameter = false;
            for (size_t j = 0; j < r->arity && !parameter; ++j)
                parameter = r->arguments[j].category == CATEGORY_SYNTAX && arguments[j]->kind == CE_ATOM_SYMBOL &&
                    !ce_is(m->grammar->symbols[m->grammar->symbols[r->arguments[j].sort].sort].name,"DeclarationSpecifiers") &&
                    ce_equal(arguments[j]->text,token);
            if (parameter && i && i+1 < tokens.len) {
                CeText before = tokens.data[i-1].text, after = tokens.data[i+1].text;
                if ((ce_is(before,"(") || ce_is(before,",")) && (ce_is(after,")") || ce_is(after,","))) parameter = false;
            }
            if (parameter) fputc('(',joined);
            fwrite(token.data,1,token.len,joined);
            if (parameter) fputc(')',joined);
        }
        fputs(part+1 < nparts ? ", \\\n" : ")",joined);
        ce_tokens_free(&tokens); free(buffer);
    }
    ok = fclose(joined) == 0 && ok;
    if (!ok) { free(text); return false; }
    CeAtom *definition = list(arena,"native",1);
    definition->items[1] = ce_atom_new(arena,CE_ATOM_TEXT,ce_text(arena,(unsigned char *)text,text_length),0);
    free(text); out->definition = definition; return true;
}

/* &*E is E without evaluating either operator (C11 6.5.3.2); place
   parameters otherwise read as &*name. */
static CeAtom *address_of_object(CeAtom *a) {
    if (a->kind != CE_ATOM_LIST) return a;
    for (size_t i = 0; i < a->len; ++i) a->items[i] = address_of_object(a->items[i]);
    if (a->len == 2 && a->items[0]->kind == CE_ATOM_SYMBOL && ce_is(a->items[0]->text,"&") &&
        a->items[1]->kind == CE_ATOM_LIST && a->items[1]->len == 2 && a->items[1]->items[0]->kind == CE_ATOM_SYMBOL &&
        ce_is(a->items[1]->items[0]->text,"*")) return a->items[1]->items[1];
    return a;
}

bool ce_macros_export(CeMacros *m, size_t begin, size_t end, const CeMacroExportRequest *request,
                      CeArena *arena, CeMacroExport *out, CeError *error) {
    MacroRule *r = NULL;
    for (size_t i = begin; i < end; ++i) if (ce_equal(m->rules[i].label,request->constructor)) r = &m->rules[i];
    if (!r) return ce_error(error,request->offset,"unknown constructor %.*s in the selected theory",
                            (int)request->constructor.len,request->constructor.data);
    static const char *const contextual[] = {"return","break","continue","goto","__func__","Capture","DefinitionRef"};
    if (contains_head(r->body,contextual,sizeof(contextual)/sizeof(*contextual)))
        return ce_error(error,request->offset,"%.*s depends on its use site and stays an in-place expansion",
                        (int)r->label.len,r->label.data);
    bool expression = ce_is(m->grammar->symbols[m->grammar->symbols[r->result].sort].name,"Expr");
    CeAtom **arguments = ce_alloc(arena,r->arity*sizeof(*arguments));
    CeText *taken = NULL; size_t ntaken = 0; size_t substituted = 0;
    note(&taken,&ntaken,request->name);
    for (size_t i = 0; i < r->arity; ++i) {
        MacroArgument *arg = &r->arguments[i]; arguments[i] = NULL;
        bool type_hole = arg->category == CATEGORY_SYNTAX &&
            ce_is(m->grammar->symbols[m->grammar->symbols[arg->sort].sort].name,"DeclarationSpecifiers");
        for (size_t j = 0; j < request->types->len; ++j) {
            const CeAtom *pair = request->types->items[j];
            if (!ce_equal(pair->items[0]->text,arg->name)) continue;
            if (!type_hole) { free(taken); return ce_error(error,pair->offset,"%.*s is not a type parameter",(int)arg->name.len,arg->name.data); }
            if (arguments[i]) { free(taken); return ce_error(error,pair->offset,"duplicate type parameter"); }
            arguments[i] = pair->items[1]; ++substituted;
        }
        if (type_hole && !arguments[i]) {
            free(taken); return ce_error(error,request->offset,"missing type parameter %.*s",(int)arg->name.len,arg->name.data);
        }
        if (type_hole) continue;
        if (!request->function && arg->category != CATEGORY_SYNTAX) {
            free(taken); return ce_error(error,request->offset,
                "%.*s has evaluated arguments; export it as a function or keep its in-place expansion",
                (int)r->label.len,r->label.data);
        }
        if (request->function && arg->category == CATEGORY_SYNTAX) {
            free(taken); return ce_error(error,request->offset,"%.*s takes syntax and is exported as a macro",(int)r->label.len,r->label.data);
        }
        if (request->function && arg->evaluation != ARG_ONCE) {
            free(taken); return ce_error(error,request->offset,"%.*s argument %.*s is not evaluated once; export it as a macro",
                (int)r->label.len,r->label.data,(int)arg->name.len,arg->name.data);
        }
    }
    if (substituted != request->types->len) { free(taken); return ce_error(error,request->offset,"unknown type parameter"); }
    CeText *locals = ce_alloc(arena,(r->nlocals+1)*sizeof(*locals));
    CeAtom **parameters = NULL; size_t nparameters = 0;
    out->protected_names = NULL; out->nprotected = 0; out->builtins = NULL; out->nbuiltins = 0;
    CeText *picked = ce_alloc(arena,(r->arity+r->nlocals+1)*sizeof(*picked)); size_t npicked = 0;
    for (size_t i = 0; i < r->arity; ++i) {
        MacroArgument *arg = &r->arguments[i];
        if (arguments[i]) continue;
        CeText name = readable(m,arena,arg->name,taken,ntaken,request->types); note(&taken,&ntaken,name);
        picked[npicked++] = name;
        CeAtom *symbol_atom = symbol(arena,name,0);
        if (!request->function) { arguments[i] = symbol_atom; continue; }
        note(&out->protected_names,&out->nprotected,name);
        bool place = arg->category != CATEGORY_VALUE;
        if (place) { CeAtom *object = list(arena,"*",1); object->items[1] = symbol_atom; arguments[i] = object; }
        else arguments[i] = symbol_atom;
        Instance types = {.macros=m,.rule=r,.arguments=arguments,.locals=locals,.arena=arena};
        CeAtom *parameter = declaration_of(&types,arg->type,symbol_atom,place,error);
        if (!parameter) { free(taken); free(parameters); return false; }
        parameters = ce_resize(parameters,nparameters+1,sizeof(*parameters)); parameters[nparameters++] = parameter;
    }
    for (size_t i = 0; i < r->nlocals; ++i) {
        locals[i] = readable(m,arena,r->locals[i].name,taken,ntaken,request->types);
        note(&taken,&ntaken,locals[i]); note(&out->protected_names,&out->nprotected,locals[i]);
        picked[npicked++] = locals[i];
    }
    Instance instance = {.macros=m,.rule=r,.arguments=arguments,.locals=locals,.arena=arena};
    for (size_t i = 0; i < r->nports; ++i) r->ports[i].used = false;
    CeAtom *body = instantiate(&instance,r->body,error);
    if (!body) { free(taken); free(parameters); return false; }
    for (size_t i = 0; i < r->nports; ++i)
        if (r->ports[i].intrinsic && r->ports[i].used) note(&out->builtins,&out->nbuiltins,r->ports[i].name);
    free(taken);
    if (request->function) {
        body = address_of_object(body);
        CeAtom *items = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},0);
        const CeAtom *block = body->kind == CE_ATOM_LIST && body->len && body->items[0]->kind == CE_ATOM_SYMBOL &&
            ce_is(body->items[0]->text,"block") ? body : NULL;
        size_t n = block ? block->len-1 : 1;
        items->items = ce_alloc(arena,n*sizeof(*items->items)); items->len = n;
        for (size_t i = 0; i < n; ++i) items->items[i] = block ? block->items[i+1] : body;
        if (expression) {
            CeAtom *value = list(arena,"return",1); value->items[1] = items->items[n-1]; items->items[n-1] = value;
        }
        CeAtom *result = word(arena,"void");
        if (expression) {
            CeAtom *type = instantiate(&instance,r->type,error);
            if (!type || type->len == 3) {
                free(parameters);
                return type ? ce_error(error,request->offset,"exported functions return declaration specifiers only") : false;
            }
            result = type->items[1];
        }
        CeAtom *specifiers = list(arena,"static",1), *inline_specifier = list(arena,"inline",1);
        inline_specifier->items[1] = result; specifiers->items[1] = inline_specifier;
        CeAtom *definition = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},4+n);
        definition->items[0] = word(arena,"fn"); definition->items[1] = specifiers;
        definition->items[2] = symbol(arena,request->name,0);
        CeAtom *params = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},nparameters ? nparameters : 1);
        for (size_t i = 0; i < nparameters; ++i) params->items[i] = parameters[i];
        if (!nparameters) params->items[0] = word(arena,"void");
        free(parameters); definition->items[3] = params;
        for (size_t i = 0; i < n; ++i) definition->items[4+i] = items->items[i];
        if (request->plan) header_plan(request->plan,r,request,picked,npicked,definition);
        out->definition = definition; return true;
    }
    free(parameters);
    return macro_definition(m,r,request,arena,body,arguments,expression,picked,npicked,out,error);
}

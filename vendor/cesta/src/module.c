#define _XOPEN_SOURCE 700
#include "cesta.h"

#include <stdlib.h>
#include <string.h>

struct CeModule {
    CeText path, name, source;
    CeTree *tree;
    CeTree **declarations;
    size_t ndecls;
    bool loading;
};
CeText ce_module_path(const CeModule *module) { return module->path; }
CeText ce_module_source(const CeModule *module) { return module->source; }
typedef struct { CeText name; CePresentation value; } Binding;
typedef struct { Binding *bindings; size_t len; } Environment;

static bool labeled(CeCatalog *c, const CeTree *t, const char *name) { return ce_tree_label(c->syntax,t,name); }
static CeText name(CeCatalog *c, const CeTree *t) {
    if (t->production == -1) return t->spelling;
    if (labeled(c,t,"NameWildcard")) return ce_cstr(&c->arena,"_");
    if (t->len == 1) return name(c,t->children[0]);
    if (labeled(c,t,"QualifiedDottedPath")) {
        CeText a = name(c,t->children[0]), b = name(c,t->children[1]);
        unsigned char *s = ce_alloc(&c->arena,a.len+b.len+2);
        memcpy(s,a.data,a.len); s[a.len] = '.'; memcpy(s+a.len+1,b.data,b.len);
        return (CeText){s,a.len+b.len+1};
    }
    return (CeText){NULL,0};
}

static CePresentation copy(CeCatalog *c, CePresentation p) {
    CePresentation result = p;
    result.definitions = ce_alloc(&c->arena,p.len*sizeof(*p.definitions));
    result.laws = ce_alloc(&c->arena,p.nlaws*sizeof(*p.laws));
    result.exports = ce_alloc(&c->arena,p.nexports*sizeof(*p.exports));
    if (p.len) memcpy(result.definitions,p.definitions,p.len*sizeof(*p.definitions));
    if (p.nlaws) memcpy(result.laws,p.laws,p.nlaws*sizeof(*p.laws));
    if (p.nexports) memcpy(result.exports,p.exports,p.nexports*sizeof(*p.exports));
    return result;
}

static CePresentation with_definition(CeCatalog *c, CePresentation p, CeTree *tree, uint64_t origin) {
    CeDefinition *defs = ce_alloc(&c->arena,(p.len+1)*sizeof(*defs));
    if (p.len) memcpy(defs,p.definitions,p.len*sizeof(*defs));
    defs[p.len++] = (CeDefinition){tree,origin};
    p.definitions = defs; return p;
}
static CePresentation with_law(CeCatalog *c, CePresentation p, CeTree *tree, uint64_t origin) {
    CeDefinition *laws = ce_alloc(&c->arena,(p.nlaws+1)*sizeof(*laws));
    if (p.nlaws) memcpy(laws,p.laws,p.nlaws*sizeof(*laws));
    laws[p.nlaws++] = (CeDefinition){tree,origin};
    p.laws = laws; return p;
}

static bool compose_laws(CeCatalog *c, CePresentation a, CePresentation b,
                          bool join, bool meet, const CeTree *expression,
                          CePresentation *out, CeError *error) {
    for (size_t i = 0; i < a.nlaws; ++i) {
        bool common = false;
        for (size_t j = 0; j < b.nlaws; ++j) if (a.laws[i].origin == b.laws[j].origin) {
            bool same = ce_tree_equal(c->syntax,a.laws[i].tree,b.laws[j].tree);
            if (join && c->composition == CE_COMPOSE_ORIGIN_COHERENT && !same)
                return ce_error(error,expression->offset,"origin-coherent join requires shared-origin laws to agree");
            common |= c->composition == CE_COMPOSE_ORIGIN_COHERENT || same;
        }
        if (join || common == meet) *out = with_law(c,*out,a.laws[i].tree,a.laws[i].origin);
    }
    if (join) for (size_t i = 0; i < b.nlaws; ++i) {
        bool common = false;
        for (size_t j = 0; j < out->nlaws; ++j)
            if (out->laws[j].origin == b.laws[i].origin &&
                (c->composition == CE_COMPOSE_ORIGIN_COHERENT ||
                 ce_tree_equal(c->syntax,out->laws[j].tree,b.laws[i].tree))) common = true;
        if (!common) *out = with_law(c,*out,b.laws[i].tree,b.laws[i].origin);
    }
    return true;
}
static uint64_t declaration_origin(CeCatalog *c, CeTree *tree) {
    for (size_t i = 0; i < c->norigins; ++i)
        if (c->origins[i].tree == tree) return c->origins[i].origin;
    c->origins = ce_resize(c->origins,c->norigins+1,sizeof(*c->origins));
    uint64_t origin = ++c->next_origin;
    c->origins[c->norigins++] = (CeDefinition){tree,origin};
    return origin;
}
static CePresentation with_export(CeCatalog *c, CePresentation p, CeTree *cat) {
    for (size_t i = 0; i < p.nexports; ++i) if (ce_tree_equal(c->syntax,p.exports[i],cat)) return p;
    CeTree **cats = ce_alloc(&c->arena,(p.nexports+1)*sizeof(*cats));
    if (p.nexports) memcpy(cats,p.exports,p.nexports*sizeof(*cats));
    cats[p.nexports++] = cat; p.exports = cats; return p;
}

static bool rule_label_equal(CeCatalog *c, const CeTree *a, const CeTree *b) {
    if (labeled(c,a,"Rule") && labeled(c,b,"Rule")) {
        if (!labeled(c,a->children[0],"Id") || !labeled(c,b->children[0],"Id")) return false;
        return ce_tree_equal(c->syntax,a->children[0],b->children[0]);
    }
    if ((labeled(c,a,"Token") || labeled(c,a,"PosToken")) &&
        (labeled(c,b,"Token") || labeled(c,b,"PosToken")))
        return ce_tree_equal(c->syntax,a->children[0],b->children[0]);
    return false;
}
static bool label_collision(CeCatalog *c, CePresentation p, const CeTree *tree, size_t except) {
    for (size_t i = 0; i < p.len; ++i)
        if (i != except && rule_label_equal(c,p.definitions[i].tree,tree)) return true;
    return false;
}

static CeTree *rename_cat(CeCatalog *c, const CeTree *tree, const CeTree *old, CeTree *replacement) {
    if (ce_tree_equal(c->syntax,tree,old)) return replacement;
    CeTree *t = ce_alloc(&c->arena,sizeof(*t)); *t = *tree;
    t->children = ce_alloc(&c->arena,t->len*sizeof(*t->children));
    for (size_t i = 0; i < t->len; ++i) t->children[i] = rename_cat(c,tree->children[i],old,replacement);
    return t;
}

static bool local_checks(CeCatalog *c, const CeTree *t, CeError *error) {
    if (labeled(c,t,"MkGrammar")) {
        const CeTree *defs = t->children[0];
        for (size_t i = 0; i < defs->len; ++i) {
            for (size_t j = 0; j < i; ++j) if (rule_label_equal(c,defs->children[i],defs->children[j]))
                return ce_error(error,defs->children[i]->offset,"duplicate constructor label in Terms");
        }
    }
    for (size_t i = 0; i < t->len; ++i) if (!local_checks(c,t->children[i],error)) return false;
    return true;
}

void ce_catalog_free(CeCatalog *c) {
    ce_lr_free(&c->syntax_lr);
    ce_arena_free(&c->arena); free(c->modules); free(c->origins); memset(c,0,sizeof(*c));
}

bool ce_module_read(CeCatalog *c, CeText identity, CeText source, CeModule **out, CeError *error) {
    for (size_t i = 0; i < c->len; ++i) if (ce_equal(c->modules[i]->path,identity)) {
        if (c->modules[i]->loading) return ce_error(error,0,"cyclic module import");
        if (!ce_equal(c->modules[i]->source,source)) return ce_error(error,0,"module identity refers to different source bytes");
        *out = c->modules[i]; return true;
    }
    CeModule *m = ce_alloc(&c->arena,sizeof(*m));
    m->path = ce_text(&c->arena,identity.data,identity.len); m->loading = true;
    m->source = ce_text(&c->arena,source.data,source.len);
    c->modules = ce_resize(c->modules,c->len+1,sizeof(*c->modules)); c->modules[c->len++] = m;
    CeTokens tokens = {0}; CeForest forest = {.root=-1};
    int entry = -1;
    for (size_t i = 0; i < c->syntax->nsymbols; ++i)
        if (c->syntax->symbols[i].kind == CE_NONTERMINAL && ce_is(c->syntax->symbols[i].name,"Module")) entry = (int)i;
    if (!c->syntax_prepared && entry >= 0) {
        CeError conflict = {{0},0}; c->syntax_prepared = true;
        c->deterministic_syntax = ce_lr_prepare(c->syntax,entry,&c->syntax_lr,&conflict) && !c->syntax_lr.ambiguities;
    }
    bool ok = entry >= 0 && ce_lex(c->syntax,m->source,&tokens,error) &&
        (c->deterministic_syntax ? ce_lr_parse(c->syntax,&c->syntax_lr,&tokens,&forest,error) :
                                  ce_parse(c->syntax,&tokens,entry,&forest,error)) &&
        ce_tree_from_forest(c->syntax,&forest,&tokens,&c->arena,&m->tree,error);
    ce_forest_free(&forest); ce_tokens_free(&tokens);
    if (!ok) return false;
    m->name = name(c,m->tree->children[1]);
    CeTree *programs = m->tree->children[2];
    m->declarations = ce_alloc(&c->arena,programs->len*sizeof(*m->declarations));
    for (size_t i = 0; i < programs->len; ++i) {
        CeTree *p = programs->children[i];
        if (!labeled(c,p,"ProgTheoryDecl")) continue;
        CeTree *decl = p->children[0];
        CeTree *params = decl->children[1];
        for (size_t j = 0; j < params->len; ++j) for (size_t k = 0; k < j; ++k)
            if (ce_equal(name(c,params->children[j]->children[0]),name(c,params->children[k]->children[0])))
                return ce_error(error,params->children[j]->offset,"duplicate theory parameter name");
        for (size_t j = 0; j < m->ndecls; ++j)
            if (ce_equal(name(c,decl->children[0]),name(c,m->declarations[j]->children[0]))) return ce_error(error,decl->offset,"duplicate theory name");
        m->declarations[m->ndecls++] = decl;
    }
    m->loading = false; *out = m; return true;
}

bool ce_module_load(CeCatalog *c, const char *path, CeModule **out, CeError *error) {
    /* Embedded modules resolve imports through the same catalog identities. */
    for (size_t i = 0; i < c->len; ++i) if (ce_is(c->modules[i]->path,path)) {
        if (c->modules[i]->loading) return ce_error(error,0,"cyclic module import");
        *out = c->modules[i]; return true;
    }
    char *identity = realpath(path,NULL);
    if (!identity) return ce_error(error,0,"cannot resolve module %s",path);
    for (size_t i = 0; i < c->len; ++i) if (ce_is(c->modules[i]->path,identity)) {
        free(identity);
        if (c->modules[i]->loading) return ce_error(error,0,"cyclic module import");
        *out = c->modules[i]; return true;
    }
    CeText source;
    bool ok = ce_read_file(&c->arena,path,&source,error) &&
        ce_module_read(c,ce_cstr(&c->arena,identity),source,out,error);
    free(identity); return ok;
}

static bool resolve(CeCatalog *c, CeModule *m, CeText path, CeModule **owner, CeTree **decl, CeError *error) {
    size_t dot = 0;
    while (dot < path.len && path.data[dot] != '.') ++dot;
    if (dot < path.len) {
        CeTree *imports = m->tree->children[0];
        CeText alias = {path.data,dot}, tail = {path.data+dot+1,path.len-dot-1};
        for (size_t i = 0; i < imports->len; ++i) {
            CeTree *imp = imports->children[i];
            if (!labeled(c,imp,"ImportModuleAs") || !ce_equal(name(c,imp->children[1]),alias)) continue;
            CeText filename;
            if (!ce_token_value(&c->arena,imp->children[0]->spelling,&filename,error)) return false;
            size_t slash = m->path.len;
            while (slash && m->path.data[slash-1] != '/') --slash;
            unsigned char *full = ce_alloc(&c->arena,slash+filename.len+1);
            memcpy(full,m->path.data,slash); memcpy(full+slash,filename.data,filename.len);
            CeModule *target;
            if (!ce_module_load(c,(const char *)full,&target,error)) return false;
            return resolve(c,target,tail,owner,decl,error);
        }
        return ce_error(error,0,"unknown import qualifier %.*s",(int)alias.len,alias.data);
    }
    for (size_t i = 0; i < m->ndecls; ++i) if (ce_equal(name(c,m->declarations[i]->children[0]),path)) {
        *owner = m; *decl = m->declarations[i]; return true;
    }
    return ce_error(error,0,"unknown theory %.*s",(int)path.len,path.data);
}

static bool evaluate(CeCatalog *c, CeModule *m, Environment env, const CeTree *expr,
                      CePresentation *out, CeError *error, size_t depth);

static bool instantiate(CeCatalog *c, CeModule *m, CeTree *decl, CePresentation *args, size_t nargs,
                         CePresentation *out, CeError *error, size_t depth) {
    CeTree *params = decl->children[1];
    if (params->len != nargs) return ce_error(error,decl->offset,"theory argument count mismatch: expected %zu, got %zu",params->len,nargs);
    Environment env = {ce_alloc(&c->arena,nargs*sizeof(Binding)),nargs};
    for (size_t i = 0; i < nargs; ++i) env.bindings[i] = (Binding){name(c,params->children[i]->children[0]),args[i]};
    return evaluate(c,m,env,decl->children[2],out,error,depth+1);
}

static bool free_theory(CeCatalog *c, CeModule *m, CeTree *decl, CePresentation *out, CeError *error, size_t depth) {
    if (depth > 256) return ce_error(error,decl->offset,"recursive free theory construction requires an explicit recursion profile");
    CeTree *params = decl->children[1];
    CePresentation *args = ce_alloc(&c->arena,params->len*sizeof(*args));
    for (size_t i = 0; i < params->len; ++i) {
        CeModule *owner; CeTree *parameter;
        if (!resolve(c,m,name(c,params->children[i]->children[1]),&owner,&parameter,error) ||
            !free_theory(c,owner,parameter,&args[i],error,depth+1)) return false;
    }
    return instantiate(c,m,decl,args,params->len,out,error,depth+1);
}

static bool replace(CeCatalog *c, CePresentation p, const CeTree *replacement, CePresentation *out, CeError *error) {
    const CeTree *perm = replacement->children[0]->children[0];
    const CeTree *old_label = replacement->children[1], *old_cat = replacement->children[2];
    CeTree *new_rule = replacement->children[3];
    if (!labeled(c,new_rule,"Rule")) return ce_error(error,replacement->offset,"replacement must be a syntax rule");
    size_t index = 0;
    while (index < p.len && !(labeled(c,p.definitions[index].tree,"Rule") &&
        ce_tree_equal(c->syntax,p.definitions[index].tree->children[0],old_label))) ++index;
    if (index == p.len) return ce_error(error,replacement->offset,"replacement source label is absent");
    CeTree *old_rule = p.definitions[index].tree;
    if (!ce_tree_equal(c->syntax,old_rule->children[1],old_cat)) return ce_error(error,replacement->offset,"replacement source category mismatch");
    if (!ce_tree_equal(c->syntax,new_rule->children[1],old_cat)) return ce_error(error,replacement->offset,"replacement result category mismatch; rename exports explicitly");
    if (label_collision(c,p,new_rule,index)) return ce_error(error,replacement->offset,"replacement constructor label already exists");
    CeTree *old_items = old_rule->children[2], *new_items = new_rule->children[2];
    CeTree **old_sorts = ce_alloc(&c->arena,old_items->len*sizeof(*old_sorts));
    CeTree **new_sorts = ce_alloc(&c->arena,new_items->len*sizeof(*new_sorts));
    size_t no = 0, nn = 0;
    for (size_t i = 0; i < old_items->len; ++i) if (!labeled(c,old_items->children[i],"Terminal")) old_sorts[no++] = old_items->children[i];
    for (size_t i = 0; i < new_items->len; ++i) if (!labeled(c,new_items->children[i],"Terminal")) new_sorts[nn++] = new_items->children[i];
    if (no != nn || perm->len != no) return ce_error(error,replacement->offset,"replacement permutation arity mismatch");
    unsigned char *seen = ce_alloc(&c->arena,no);
    for (size_t i = 0; i < no; ++i) {
        CeText spelling = perm->children[i]->spelling;
        char *end = NULL; unsigned long j = strtoul((const char *)spelling.data,&end,10);
        if (end != (char *)spelling.data+spelling.len || j >= no || seen[j]) return ce_error(error,replacement->offset,"replacement indices are not a permutation");
        seen[j] = 1;
        if (!ce_tree_equal(c->syntax,old_sorts[i],new_sorts[j])) return ce_error(error,replacement->offset,"replacement argument category mismatch");
    }
    *out = copy(c,p); out->definitions[index].tree = new_rule; return true;
}

static bool evaluate(CeCatalog *c, CeModule *m, Environment env, const CeTree *expr,
                      CePresentation *out, CeError *error, size_t depth) {
    if (depth > 256) return ce_error(error,expr->offset,"recursive theory evaluation requires an explicit recursion profile");
    if (labeled(c,expr,"TheoryInstEmpty")) { *out = (CePresentation){0}; return true; }
    if (labeled(c,expr,"TheoryInstRef")) {
        CeText n = name(c,expr->children[0]);
        for (size_t i = env.len; i; --i) if (ce_equal(env.bindings[i-1].name,n)) { *out = env.bindings[i-1].value; return true; }
        return ce_error(error,expr->offset,"unbound theory reference %.*s",(int)n.len,n.data);
    }
    if (labeled(c,expr,"TheoryInstCtor") || labeled(c,expr,"TheoryInstFree")) {
        CeModule *owner; CeTree *decl;
        if (!resolve(c,m,name(c,expr->children[0]),&owner,&decl,error)) return false;
        if (labeled(c,expr,"TheoryInstFree")) return free_theory(c,owner,decl,out,error,depth+1);
        CeTree *actuals = expr->children[1];
        CePresentation *args = ce_alloc(&c->arena,actuals->len*sizeof(*args));
        for (size_t i = 0; i < actuals->len; ++i)
            if (!evaluate(c,m,env,actuals->children[i],&args[i],error,depth+1)) return false;
        return instantiate(c,owner,decl,args,actuals->len,out,error,depth+1);
    }
    if (labeled(c,expr,"TheoryInstRec")) {
        CePresentation value;
        if (!evaluate(c,m,env,expr->children[1],&value,error,depth+1)) return false;
        Binding *bindings = ce_alloc(&c->arena,(env.len+1)*sizeof(*bindings));
        if (env.len) memcpy(bindings,env.bindings,env.len*sizeof(*bindings));
        bindings[env.len] = (Binding){name(c,expr->children[0]),value};
        return evaluate(c,m,(Environment){bindings,env.len+1},expr->children[2],out,error,depth+1);
    }
    if (labeled(c,expr,"TheoryInstDisj") || labeled(c,expr,"TheoryInstConj") || labeled(c,expr,"TheoryInstSubtract")) {
        CePresentation a, b;
        if (!evaluate(c,m,env,expr->children[0],&a,error,depth+1) || !evaluate(c,m,env,expr->children[1],&b,error,depth+1)) return false;
        *out = (CePresentation){0};
        bool join = labeled(c,expr,"TheoryInstDisj"), meet = labeled(c,expr,"TheoryInstConj");
        for (size_t i = 0; i < a.len; ++i) {
            bool common = false;
            for (size_t j = 0; j < b.len; ++j) if (a.definitions[i].origin == b.definitions[j].origin) {
                bool same_view = ce_tree_equal(c->syntax,a.definitions[i].tree,b.definitions[j].tree);
                if (join && c->composition == CE_COMPOSE_ORIGIN_COHERENT && !same_view)
                    return ce_error(error,expr->offset,"origin-coherent join requires shared-origin declarations to agree; use the origin-view profile to retain different views");
                common |= c->composition == CE_COMPOSE_ORIGIN_COHERENT || same_view;
            }
            if (join || common == meet) *out = with_definition(c,*out,a.definitions[i].tree,a.definitions[i].origin);
        }
        if (join) for (size_t i = 0; i < b.len; ++i) {
            bool common = false;
            for (size_t j = 0; j < out->len; ++j) if (out->definitions[j].origin == b.definitions[i].origin &&
                (c->composition == CE_COMPOSE_ORIGIN_COHERENT || ce_tree_equal(c->syntax,out->definitions[j].tree,b.definitions[i].tree))) common = true;
            if (common) continue;
            if (label_collision(c,*out,b.definitions[i].tree,SIZE_MAX)) return ce_error(error,expr->offset,"independent theory contributions collide on constructor label");
            *out = with_definition(c,*out,b.definitions[i].tree,b.definitions[i].origin);
        }
        if (!compose_laws(c,a,b,join,meet,expr,out,error)) return false;
        for (size_t i = 0; i < a.nexports; ++i) {
            bool common = false;
            for (size_t j = 0; j < b.nexports; ++j) if (ce_tree_equal(c->syntax,a.exports[i],b.exports[j])) common = true;
            if (join || common == meet) *out = with_export(c,*out,a.exports[i]);
        }
        if (join) for (size_t i = 0; i < b.nexports; ++i) *out = with_export(c,*out,b.exports[i]);
        return true;
    }
    if (labeled(c,expr,"TheoryInstAddTerms") || labeled(c,expr,"TheoryInstAddExports") ||
        labeled(c,expr,"TheoryInstAddReplacements") || labeled(c,expr,"TheoryInstAddEquations") || labeled(c,expr,"TheoryInstAddRewrites")) {
        if (!evaluate(c,m,env,expr->children[0],out,error,depth+1)) return false;
        if (labeled(c,expr,"TheoryInstAddTerms")) {
            CeTree *defs = expr->children[1]->children[0];
            for (size_t i = 0; i < defs->len; ++i) {
                if (label_collision(c,*out,defs->children[i],SIZE_MAX)) return ce_error(error,defs->children[i]->offset,"duplicate constructor label in composed Terms");
                *out = with_definition(c,*out,defs->children[i],declaration_origin(c,defs->children[i]));
            }
        } else if (labeled(c,expr,"TheoryInstAddExports")) {
            CeTree *exports = expr->children[1];
            for (size_t i = 0; i < exports->len; ++i) {
                CeTree *ex = exports->children[i];
                if (labeled(c,ex,"BaseExport")) *out = with_export(c,*out,ex->children[0]);
                else {
                    CeTree *old = ex->children[0], *new_cat = ex->children[1];
                    *out = copy(c,*out);
                    for (size_t j = 0; j < out->nexports; ++j) out->exports[j] = rename_cat(c,out->exports[j],old,new_cat);
                    for (size_t j = 0; j < out->len; ++j) out->definitions[j].tree = rename_cat(c,out->definitions[j].tree,old,new_cat);
                }
            }
        } else if (labeled(c,expr,"TheoryInstAddReplacements")) {
            CeTree *replacements = expr->children[1];
            for (size_t i = 0; i < replacements->len; ++i) {
                CePresentation p;
                if (!replace(c,*out,replacements->children[i],&p,error)) return false;
                *out = p;
            }
        } else {
            CeTree *laws = expr->children[1];
            for (size_t i = 0; i < laws->len; ++i)
                *out = with_law(c,*out,laws->children[i],declaration_origin(c,laws->children[i]));
        }
        /* Laws compose as source declarations. A separate, declared
           interpretation may realize them; extracting Terms does not. */
        return true;
    }
    return ce_error(error,expr->offset,"unsupported theory expression");
}

bool ce_module_check(CeCatalog *c, CeModule *m, CeError *error) {
    if (!local_checks(c,m->tree,error)) return false;
    CeTree *programs = m->tree->children[2];
    for (size_t i = 0; i < programs->len; ++i) {
        CeTree *p = programs->children[i];
        if (labeled(c,p,"ProgTheoryInst")) {
            CePresentation presentation;
            if (!evaluate(c,m,(Environment){0},p->children[0],&presentation,error,0)) return false;
        }
    }
    return true;
}

bool ce_module_theory(CeCatalog *c, CeModule *m, const char *theory, CePresentation *out, CeError *error) {
    CeModule *owner; CeTree *decl;
    if (!resolve(c,m,ce_cstr(&c->arena,theory),&owner,&decl,error)) return false;
    return free_theory(c,owner,decl,out,error,0);
}

bool ce_presentation_signature(CeCatalog *c, const CePresentation *p,
                               const CeGrammar *external, CeGrammar *out, CeError *error) {
    FILE *file = tmpfile();
    if (!file) return ce_error(error,0,"cannot allocate grammar rendering stream");
    bool ok = true; CeLazyLexer lexer = {0};
    for (size_t i = 0; i < p->len && ok; ++i) {
        CeTree *tree = p->definitions[i].tree;
        ok = ce_tree_print_shared(c->syntax,&lexer,tree,tree->symbol,file,error);
        fputs(";\n",file);
    }
    ce_lazy_lexer_free(&lexer);
    long size = ftell(file);
    if (size < 0) ok = ce_error(error,0,"cannot measure grammar rendering stream");
    if (ok) {
        CeText text = {ce_alloc(&c->arena,(size_t)size+1),(size_t)size};
        rewind(file);
        if (fread(text.data,1,text.len,file) != text.len) ok = ce_error(error,0,"cannot read grammar rendering stream");
        else ok = ce_grammar_read(out,text,error) && ce_grammar_finish_signature(out,external,error);
    }
    fclose(file); return ok;
}

bool ce_presentation_grammar(CeCatalog *c, const CePresentation *p, CeGrammar *out, CeError *error) {
    return ce_presentation_signature(c,p,NULL,out,error);
}

static CeAtom *law_symbol(CeArena *a, CeText text, size_t offset) {
    CeAtom *atom = ce_atom_new(a,CE_ATOM_SYMBOL,ce_text(a,text.data,text.len),0);
    atom->offset = offset; return atom;
}
/* The existing module parser owns syntax and composition. Quoted heads add
   atom constructors such as "+" without embedding a second textual parser. */
CeAtom *ce_law_atom(const CeGrammar *g, CeLazyLexer *lexer, CeArena *arena, const CeTree *t, CeError *error) {
    CeAtom *out;
    if (ce_tree_label(g,t,"ASTVar")) {
        FILE *file = tmpfile();
        if (!file) { ce_error(error,t->offset,"cannot render module name"); return NULL; }
        bool ok = ce_tree_print_shared(g,lexer,t->children[0],t->children[0]->symbol,file,error);
        long length = ftell(file); CeText text = {0};
        if (ok && length >= 0) {
            text = (CeText){ce_alloc(arena,(size_t)length+1),(size_t)length};
            rewind(file); ok = fread(text.data,1,text.len,file) == text.len;
            /* Dotted paths print token separators; names are structural data. */
            size_t n = 0;
            for (size_t i = 0; i < text.len; ++i)
                if (text.data[i] != ' ' && text.data[i] != '\n') text.data[n++] = text.data[i];
            text.len = n; text.data[n] = 0;
        } else ok = false;
        fclose(file); if (!ok) { ce_error(error,t->offset,"cannot read module name"); return NULL; }
        return law_symbol(arena,text,t->offset);
    }
    if (ce_tree_label(g,t,"ASTText")) {
        CeText text;
        if (!ce_token_value(arena,t->children[0]->spelling,&text,error)) return NULL;
        out = ce_atom_new(arena,CE_ATOM_TEXT,text,0);
    } else if (ce_tree_label(g,t,"ASTInteger")) {
        out = law_symbol(arena,t->children[0]->spelling,t->offset);
    } else if (ce_tree_label(g,t,"ASTSExp") || ce_tree_label(g,t,"ASTQuotedSExp")) {
        const CeTree *children = t->children[1]; CeText head;
        if (ce_tree_label(g,t,"ASTQuotedSExp")) {
            if (!ce_token_value(arena,t->children[0]->spelling,&head,error)) return NULL;
        } else {
            const CeTree *label = t->children[0];
            if (label->len != 1 || label->children[0]->production != -1) {
                ce_error(error,t->offset,"module data needs a named atom constructor"); return NULL;
            }
            head = label->children[0]->spelling;
        }
        out = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},children->len+1);
        out->items[0] = law_symbol(arena,head,t->offset);
        for (size_t i = 0; i < children->len; ++i) {
            out->items[i+1] = ce_law_atom(g,lexer,arena,children->children[i],error);
            if (!out->items[i+1]) return NULL;
        }
    } else {
        ce_error(error,t->offset,"module data cannot contain implicit substitution or freshness syntax"); return NULL;
    }
    out->offset = t->offset; return out;
}

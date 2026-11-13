#define _POSIX_C_SOURCE 200809L
#include "operation.h"
#include "table.h"

#include <stdlib.h>
#include <string.h>

/* ---- Operation contracts -----------------------------------------------------
   An operation theory declares one vocabulary and its operations as equations:

     (Vocabulary) == (Vocabulary "cesta-operation/1"
                        (Resources (Resource space "store") ...) (Charges ...) (Profiles ...))
     (work_take) == (Operation "cesta-operation/1" (Spelling "work-take") (Symbol work_take)
                       (Dispatch "special")
                       (Forms (Form (Arg "value" "data" "borrow") ...) ...)
                       (Footprint (Reads space) (Writes work))
                       (Context (Blocks "no") (Branches "yes") (Progress "bounded") (Charges ...))
                       (Result "fresh")
                       (Declared (Static "no") (TypePure "no") (Speculative "no") (Replay "refused"))
                       (Profiles ...))

   A fact no one has established is written "unknown" and is never read as a
   default: an unknown footprint is not an empty one. Reading checks what the
   Lean admission (OperationContracts.tableOk) decides again for the
   normalized form, and refuses with a byte offset. */

CeText ce_builtin_TheoryModuleSyntax(void);

typedef struct { CeText *items; size_t len; } Names;
typedef struct { CeText label, kind; } ChargeKind;
typedef struct { CeText evaluation, demand, ownership; } OpArg;
typedef struct { OpArg *args; size_t nargs; } OpForm;
/* A yes/no fact that may be unknown. */
typedef enum { NO, YES, UNKNOWN } Known;
typedef struct {
    CeText name, spelling, symbol, dispatch;
    OpForm *forms; size_t nforms;
    bool footprint;    /* false: the footprint is unknown */
    Names reads, writes;
    Known blocks, branches; CeText progress; Names charges;
    bool has_bound; uint64_t bound;
    CeText result;
    bool statik, type_pure, speculative; CeText replay; uint64_t *through; size_t nthrough;
    Names profiles;
    size_t offset;
} Operation;
typedef struct { CeText name, kind; size_t offset; } Resource;

struct CeOperations {
    CeArena arena;
    CeGrammar syntax;
    CeLazyLexer lexer;
    CeCatalog catalog;
    bool vocabulary;
    CeText vocabulary_name;
    size_t vocabulary_offset;
    Resource *resources; size_t nresources;
    Names charges, profiles;
    ChargeKind *charge_kinds; size_t ncharge_kinds;
    Operation *items; size_t len;
    CeTables shared;
};

static const char *const dispatches[] = {"grounded","control","special","unknown"};
static const char *const evaluations[] = {"value","atom","unknown"};
static const char *const demands[] = {"data","whnf","source-type","shape","value","structure","pattern","unknown"};
static const char *const ownerships[] = {"borrow","consume","unknown"};
static const char *const progresses[] = {"single","bounded","steps","suspends","unknown"};
static const char *const results[] = {"fresh","shared","borrowed","unknown"};
static const char *const replays[] = {"transparent","refused"};
/* The names of NativeCostLedger.Kind, checked again by kernel admission. */
static const char *const native_kinds[] = {
    "quantum","candidateRow","indexVisit","matchAttempt","matchNode","heapLookup","heapIndexVisit",
    "heapUpdate","receiptAppend","bindingClone","queueInsert","queueTake","queueVisit","queueMove",
    "capture","restore","frameAllocate","frameRetire","publish","coefficientAppend","coefficientMultiply",
    "costRead","analysisStep","workerDispatch","workerJoin","commitCheck","equationActivation","storageProbe",
    "coefficientHandle"
};
#define COUNT(a) (sizeof(a)/sizeof(*(a)))

static bool form(const CeAtom *a, const char *head) {
    return a->kind == CE_ATOM_LIST && a->len && a->items[0]->kind == CE_ATOM_SYMBOL && ce_is(a->items[0]->text,head);
}
static bool identifier(CeText t) {
    if (!t.len || !((t.data[0] >= 'a' && t.data[0] <= 'z') || (t.data[0] >= 'A' && t.data[0] <= 'Z') || t.data[0] == '_'))
        return false;
    for (size_t i = 1; i < t.len; ++i)
        if (!((t.data[i] >= 'a' && t.data[i] <= 'z') || (t.data[i] >= 'A' && t.data[i] <= 'Z') ||
              (t.data[i] >= '0' && t.data[i] <= '9') || t.data[i] == '_')) return false;
    return true;
}
/* A resource, charge or profile name: letters, digits, '-' and '_'. */
static bool word(CeText t) {
    if (!t.len) return false;
    for (size_t i = 0; i < t.len; ++i)
        if (!((t.data[i] >= 'a' && t.data[i] <= 'z') || (t.data[i] >= 'A' && t.data[i] <= 'Z') ||
              (t.data[i] >= '0' && t.data[i] <= '9') || t.data[i] == '_' || t.data[i] == '-')) return false;
    return true;
}
static bool one_of(CeText t, const char *const *names, size_t n) {
    for (size_t i = 0; i < n; ++i) if (ce_is(t,names[i])) return true;
    return false;
}
static bool member(CeText t, Names names) {
    for (size_t i = 0; i < names.len; ++i) if (ce_equal(names.items[i],t)) return true;
    return false;
}
static bool text_in(const CeAtom *a, const char *const *names, size_t n, CeText *out, const char *what, CeError *error) {
    if (a->kind != CE_ATOM_TEXT || !one_of(a->text,names,n))
        return ce_error(error,a->offset,"%s is not one of the admitted words",what);
    *out = a->text; return true;
}
static bool yes_no(const CeAtom *a, bool *out, const char *what, CeError *error) {
    if (a->kind != CE_ATOM_TEXT || !(ce_is(a->text,"yes") || ce_is(a->text,"no")))
        return ce_error(error,a->offset,"%s is \"yes\" or \"no\"",what);
    *out = ce_is(a->text,"yes"); return true;
}
static bool known(const CeAtom *a, Known *out, const char *what, CeError *error) {
    if (a->kind != CE_ATOM_TEXT || !(ce_is(a->text,"yes") || ce_is(a->text,"no") || ce_is(a->text,"unknown")))
        return ce_error(error,a->offset,"%s is \"yes\", \"no\" or \"unknown\"",what);
    *out = ce_is(a->text,"yes") ? YES : ce_is(a->text,"no") ? NO : UNKNOWN; return true;
}
/* (HEAD NAME ...): distinct names. */
static bool names(CeOperations *os, const CeAtom *a, const char *head, Names *out, CeError *error) {
    if (!form(a,head)) return ce_error(error,a->offset,"expected (%s NAME ...)",head);
    out->items = ce_alloc(&os->arena,(a->len)*sizeof(*out->items)); out->len = 0;
    for (size_t i = 1; i < a->len; ++i) {
        const CeAtom *x = a->items[i];
        if (x->kind != CE_ATOM_SYMBOL || !word(x->text)) return ce_error(error,x->offset,"%s names a word",head);
        if (member(x->text,*out)) return ce_error(error,x->offset,"%s names %.*s twice",head,(int)x->text.len,x->text.data);
        out->items[out->len++] = x->text;
    }
    return true;
}
static bool natural(const CeAtom *a, uint64_t *out, CeError *error) {
    if (a->kind != CE_ATOM_SYMBOL || !a->text.len || a->text.len > 9) return ce_error(error,a->offset,"expected a small decimal number");
    uint64_t v = 0;
    for (size_t i = 0; i < a->text.len; ++i) {
        if (a->text.data[i] < '0' || a->text.data[i] > '9') return ce_error(error,a->offset,"expected a decimal number");
        v = v * 10 + (uint64_t)(a->text.data[i] - '0');
    }
    if (a->text.len > 1 && a->text.data[0] == '0') return ce_error(error,a->offset,"a number has no leading zero");
    *out = v; return true;
}

static bool read_vocabulary(CeOperations *os, CeText name, const CeAtom *a, CeError *error) {
    if (os->vocabulary) return ce_error(error,a->offset,"an operation theory declares one vocabulary");
    if ((a->len != 5 && a->len != 6) || !form(a->items[2],"Resources"))
        return ce_error(error,a->offset,"vocabulary is (Vocabulary \"cesta-operation/1\" (Resources ...) (Charges ...) (Profiles ...))");
    const CeAtom *r = a->items[2];
    os->resources = ce_alloc(&os->arena,r->len*sizeof(*os->resources));
    for (size_t i = 1; i < r->len; ++i) {
        const CeAtom *x = r->items[i];
        if (!form(x,"Resource") || x->len != 3 || x->items[1]->kind != CE_ATOM_SYMBOL || !word(x->items[1]->text) ||
            x->items[2]->kind != CE_ATOM_TEXT || !(ce_is(x->items[2]->text,"store") || ce_is(x->items[2]->text,"scratch")))
            return ce_error(error,x->offset,"resource is (Resource NAME \"store\"|\"scratch\")");
        os->resources[os->nresources++] = (Resource){x->items[1]->text,x->items[2]->text,x->offset};
    }
    if (!names(os,a->items[3],"Charges",&os->charges,error) || !names(os,a->items[4],"Profiles",&os->profiles,error))
        return false;
    if (a->len == 6) {
        const CeAtom *bindings = a->items[5];
        if (!form(bindings,"ChargeKinds")) return ce_error(error,bindings->offset,"expected (ChargeKinds (LABEL KIND) ...)");
        os->charge_kinds = ce_alloc(&os->arena,bindings->len*sizeof(*os->charge_kinds));
        for (size_t i = 1; i < bindings->len; ++i) {
            const CeAtom *x = bindings->items[i];
            if (x->kind != CE_ATOM_LIST || x->len != 2 || x->items[0]->kind != CE_ATOM_SYMBOL ||
                !member(x->items[0]->text,os->charges))
                return ce_error(error,x->offset,"charge kind must name a declared charge label");
            CeText kind = {0};
            if (x->items[1]->kind == CE_ATOM_SYMBOL && one_of(x->items[1]->text,native_kinds,COUNT(native_kinds)))
                kind = x->items[1]->text;
            else if (x->items[1]->kind != CE_ATOM_TEXT || !ce_is(x->items[1]->text,"unknown"))
                return ce_error(error,x->offset,"charge kind is a native ledger kind or \"unknown\"");
            for (size_t j = 0; j < os->ncharge_kinds; ++j)
                if (ce_equal(os->charge_kinds[j].label,x->items[0]->text))
                    return ce_error(error,x->offset,"charge kind label declared twice");
            os->charge_kinds[os->ncharge_kinds++] = (ChargeKind){x->items[0]->text,kind};
        }
    }
    os->vocabulary = true; os->vocabulary_name = name; os->vocabulary_offset = a->offset;
    return true;
}

static bool read_form(CeOperations *os, const CeAtom *a, OpForm *out, CeError *error) {
    if (!form(a,"Form")) return ce_error(error,a->offset,"expected (Form (Arg EVALUATION DEMAND OWNERSHIP) ...)");
    out->args = ce_alloc(&os->arena,(a->len)*sizeof(*out->args)); out->nargs = 0;
    for (size_t i = 1; i < a->len; ++i) {
        const CeAtom *x = a->items[i]; OpArg arg;
        if (!form(x,"Arg") || x->len != 4) return ce_error(error,x->offset,"argument is (Arg EVALUATION DEMAND OWNERSHIP)");
        if (!text_in(x->items[1],evaluations,COUNT(evaluations),&arg.evaluation,"argument evaluation",error) ||
            !text_in(x->items[2],demands,COUNT(demands),&arg.demand,"argument demand",error) ||
            !text_in(x->items[3],ownerships,COUNT(ownerships),&arg.ownership,"argument ownership",error)) return false;
        out->args[out->nargs++] = arg;
    }
    return true;
}

static bool read_operation(CeOperations *os, CeText name, const CeAtom *a, CeError *error) {
    Operation op = {.name = name, .offset = a->offset};
    bool seen[10] = {0};
    static const char *const sections[] = {"Spelling","Symbol","Forms","Footprint","Context","Result","Declared","Profiles",
                                           "Dispatch","ProgressBound"};
    for (size_t i = 2; i < a->len; ++i) {
        const CeAtom *x = a->items[i]; size_t s = 0;
        while (s < COUNT(sections) && !form(x,sections[s])) ++s;
        if (s == COUNT(sections)) return ce_error(error,x->offset,"unknown operation section");
        if (seen[s]) return ce_error(error,x->offset,"operation declares %s twice",sections[s]);
        seen[s] = true;
        switch (s) {
        case 0:
            if (x->len != 2 || x->items[1]->kind != CE_ATOM_TEXT || !x->items[1]->text.len)
                return ce_error(error,x->offset,"spelling is (Spelling \"TEXT\")");
            op.spelling = x->items[1]->text; break;
        case 1:
            /* A quoted identifier spells a symbol the module language keeps as a keyword. */
            if (x->len != 2 || (x->items[1]->kind != CE_ATOM_SYMBOL && x->items[1]->kind != CE_ATOM_TEXT) ||
                !identifier(x->items[1]->text))
                return ce_error(error,x->offset,"symbol is (Symbol C-IDENTIFIER)");
            op.symbol = x->items[1]->text; break;
        case 2:
            /* (Forms) with no form leaves the arity unknown. */
            op.forms = ce_alloc(&os->arena,x->len*sizeof(*op.forms));
            for (size_t j = 1; j < x->len; ++j) {
                if (!read_form(os,x->items[j],&op.forms[op.nforms],error)) return false;
                for (size_t k = 0; k < op.nforms; ++k)
                    if (op.forms[k].nargs == op.forms[op.nforms].nargs)
                        return ce_error(error,x->items[j]->offset,"two forms take %zu arguments",op.forms[k].nargs);
                ++op.nforms;
            }
            break;
        case 3:
            if (x->len == 2 && x->items[1]->kind == CE_ATOM_TEXT && ce_is(x->items[1]->text,"unknown")) break;
            if (x->len != 3 || !names(os,x->items[1],"Reads",&op.reads,error) || !names(os,x->items[2],"Writes",&op.writes,error))
                return error->message[0] ? false :
                    ce_error(error,x->offset,"footprint is (Footprint (Reads ...) (Writes ...)) or (Footprint \"unknown\")");
            op.footprint = true;
            break;
        case 4:
            if (x->len != 5 || !form(x->items[1],"Blocks") || x->items[1]->len != 2 || !form(x->items[2],"Branches") ||
                x->items[2]->len != 2 || !form(x->items[3],"Progress") || x->items[3]->len != 2)
                return ce_error(error,x->offset,"context is (Context (Blocks YES-NO) (Branches YES-NO) (Progress P) (Charges ...))");
            if (!known(x->items[1]->items[1],&op.blocks,"Blocks",error) ||
                !known(x->items[2]->items[1],&op.branches,"Branches",error) ||
                !text_in(x->items[3]->items[1],progresses,COUNT(progresses),&op.progress,"progress",error) ||
                !names(os,x->items[4],"Charges",&op.charges,error)) return false;
            break;
        case 5:
            if (x->len != 2 || !text_in(x->items[1],results,COUNT(results),&op.result,"result",error))
                return error->message[0] ? false : ce_error(error,x->offset,"result is (Result \"fresh\"|\"shared\"|\"borrowed\")");
            break;
        case 6: {
            if (x->len != 5 || !form(x->items[1],"Static") || x->items[1]->len != 2 || !form(x->items[2],"TypePure") ||
                x->items[2]->len != 2 || !form(x->items[3],"Speculative") || x->items[3]->len != 2 ||
                !form(x->items[4],"Replay") || x->items[4]->len < 2 || x->items[4]->len > 3)
                return ce_error(error,x->offset,"declared is (Declared (Static ..) (TypePure ..) (Speculative ..) (Replay R [(Through N ...)]))");
            if (!yes_no(x->items[1]->items[1],&op.statik,"Static",error) ||
                !yes_no(x->items[2]->items[1],&op.type_pure,"TypePure",error) ||
                !yes_no(x->items[3]->items[1],&op.speculative,"Speculative",error) ||
                !text_in(x->items[4]->items[1],replays,COUNT(replays),&op.replay,"replay",error)) return false;
            if (x->items[4]->len == 3) {
                const CeAtom *t = x->items[4]->items[2];
                if (!form(t,"Through")) return ce_error(error,t->offset,"replay pass-through is (Through ARITY ...)");
                op.through = ce_alloc(&os->arena,t->len*sizeof(*op.through));
                for (size_t j = 1; j < t->len; ++j) if (!natural(t->items[j],&op.through[op.nthrough++],error)) return false;
            }
            break;
        }
        case 7:
            /* (Profiles) with no profile leaves them undeclared. */
            if (!names(os,x,"Profiles",&op.profiles,error)) return false;
            break;
        case 8:
            if (x->len != 2 || !text_in(x->items[1],dispatches,COUNT(dispatches),&op.dispatch,"dispatch",error))
                return error->message[0] ? false : ce_error(error,x->offset,"dispatch is (Dispatch \"grounded\"|\"control\"|\"special\"|\"unknown\")");
            break;
        case 9:
            if (x->len != 2 || !ce_table_nat(x->items[1],&op.bound) || !op.bound)
                return ce_error(error,x->offset,"progress bound requires a positive uint64 decimal number");
            op.has_bound = true;
            break;
        }
    }
    for (size_t s = 0; s < 9; ++s)
        if (!seen[s]) return ce_error(error,a->offset,"operation does not declare %s",sections[s]);
    os->items = ce_resize(os->items,os->len+1,sizeof(*os->items)); os->items[os->len++] = op;
    return true;
}

/* The checks that need the vocabulary. */
static bool known_pure(const Operation *op) { return op->footprint && !op->reads.len && !op->writes.len; }
/* A grounded value operation whose footprint is known to be empty. */
static bool pure_row(const Operation *op) { return ce_is(op->dispatch,"grounded") && known_pure(op); }
static bool check_operation(const Operation *op, CeError *error) {
    if (op->has_bound && !(ce_is(op->progress,"bounded") || (ce_is(op->progress,"single") && op->bound == 1)))
        return ce_error(error,op->offset,"progress bound applies only to bounded or single (bound 1) progress");
    for (size_t i = 0; i < op->nthrough; ++i) {
        bool form_exists = false;
        for (size_t f = 0; f < op->nforms; ++f) form_exists |= op->forms[f].nargs == op->through[i];
        if (!form_exists) return ce_error(error,op->offset,"replay passes through a form the operation does not have");
    }
    if (op->type_pure && (op->reads.len || op->writes.len || op->branches == YES || op->blocks == YES))
        return ce_error(error,op->offset,"a type-level operation declares no read or write, and neither branches nor blocks");
    if (pure_row(op) && !op->type_pure)
        return ce_error(error,op->offset,"a pure grounded operation has the type-level licence");
    if (op->writes.len && (op->speculative || pure_row(op)))
        return ce_error(error,op->offset,"an operation that writes is not admitted under speculation");
    return true;
}

CeOperations *ce_operations_new(CeError *error) {
    CeOperations *os = calloc(1,sizeof(*os));
    if (!os) { ce_error(error,0,"cannot allocate operations"); return NULL; }
    os->catalog.syntax = &os->syntax;
    if (!ce_grammar_read(&os->syntax,ce_builtin_TheoryModuleSyntax(),error) || !ce_grammar_finish(&os->syntax,error)) {
        ce_operations_free(os); return NULL;
    }
    return os;
}
void ce_operations_free(CeOperations *os) {
    if (!os) return;
    free(os->items); ce_catalog_free(&os->catalog); ce_grammar_free(&os->syntax);
    ce_lazy_lexer_free(&os->lexer); ce_arena_free(&os->arena); free(os);
}
static void shared_text_row(FILE *file, CeText first, CeText second) {
    fputs(" (Row ",file); ce_quote(file,first); fputc(' ',file); ce_quote(file,second); fputc(')',file);
}
static void shared_location(CeAtom *atom, size_t offset) {
    atom->offset = offset;
    for (size_t i = 0; atom->kind == CE_ATOM_LIST && i < atom->len; ++i) shared_location(atom->items[i],offset);
}
static bool shared_tables(CeOperations *os, CeError *error) {
    char *buffer = NULL; size_t len = 0; FILE *file = open_memstream(&buffer,&len);
    if (!file) return ce_error(error,0,"cannot allocate operation tables");
    fputs("(cesta-tables/1 (Table Resources (Columns (Column name Text) (Column kind (Enum store scratch))) (Key name) (Rows",file);
    for (size_t i = 0; i < os->nresources; ++i) {
        fputs(" (Row ",file); ce_quote(file,os->resources[i].name);
        fprintf(file," %.*s)",(int)os->resources[i].kind.len,os->resources[i].kind.data);
    }
    fputs(")) (Table Charges (Columns (Column name Text)) (Key name) (Rows",file);
    for (size_t i = 0; i < os->charges.len; ++i) {
        fputs(" (Row ",file); ce_quote(file,os->charges.items[i]); fputc(')',file);
    }
    fputs(")) (Table Profiles (Columns (Column name Text)) (Key name) (Rows",file);
    for (size_t i = 0; i < os->profiles.len; ++i) {
        fputs(" (Row ",file); ce_quote(file,os->profiles.items[i]); fputc(')',file);
    }
    fputs(")) (Table Operations (Columns (Column name Text) (Column symbol Identifier) (Column spelling Text)) "
          "(Key name) (Unique symbol) (Unique spelling) (Rows",file);
    for (size_t i = 0; i < os->len; ++i) {
        const Operation *op = &os->items[i]; fputs(" (Row ",file); ce_quote(file,op->name);
        fprintf(file," %.*s ",(int)op->symbol.len,op->symbol.data); ce_quote(file,op->spelling); fputc(')',file);
    }
    fputs("))",file);
    const char *names[] = {"Reads","Writes","OpCharges","OpProfiles"};
    const char *targets[] = {"Resources","Resources","Charges","Profiles"};
    for (size_t relation = 0; relation < 4; ++relation) {
        fprintf(file," (Table %s (Columns (Column operation (Ref Operations name)) (Column value (Ref %s name))) "
                     "(Key operation value) (Rows",names[relation],targets[relation]);
        for (size_t i = 0; i < os->len; ++i) {
            const Operation *op = &os->items[i];
            Names values = relation == 0 ? op->reads : relation == 1 ? op->writes : relation == 2 ? op->charges : op->profiles;
            for (size_t j = 0; j < values.len; ++j) shared_text_row(file,op->name,values.items[j]);
        }
        fputs("))",file);
    }
    fputs(")",file);
    bool ok = fclose(file) == 0; CeAtom *data = NULL;
    if (ok) ok = ce_atom_read(&os->arena,ce_text(&os->arena,buffer,len),&data,error);
    if (ok) {
        /* Diagnostics refer to the written declaration, not its derived wire. */
        shared_location(data,os->vocabulary_offset);
        for (size_t table = 0; table < data->len-1; ++table) {
            CeAtom *decl = data->items[table+1], *rows = decl->items[decl->len-1];
            if (table == 0) {
                for (size_t r = 1; r < rows->len; ++r) shared_location(rows->items[r],os->resources[r-1].offset);
            } else if (table == 3) {
                for (size_t r = 1; r < rows->len; ++r) shared_location(rows->items[r],os->items[r-1].offset);
            } else if (table >= 4) {
                size_t r = 1;
                for (size_t i = 0; i < os->len; ++i) {
                    const Operation *op = &os->items[i];
                    Names values = table == 4 ? op->reads : table == 5 ? op->writes : table == 6 ? op->charges : op->profiles;
                    for (size_t j = 0; j < values.len; ++j) shared_location(rows->items[r++],op->offset);
                }
            }
        }
        ok = ce_tables_read(&os->arena,data,&os->shared,error);
    }
    free(buffer); return ok;
}

bool ce_operations_load(CeOperations *os, const char *path, const char *theory, CeError *error) {
    CeModule *module; CePresentation presentation = {0};
    if (!ce_module_load(&os->catalog,path,&module,error) || !ce_module_check(&os->catalog,module,error) ||
        !ce_module_theory(&os->catalog,module,theory,&presentation,error)) return false;
    if (presentation.len) return ce_error(error,0,"an operation theory declares no Terms");
    for (size_t i = 0; i < presentation.nlaws; ++i) {
        const CeTree *law = presentation.laws[i].tree;
        if (!ce_tree_label(&os->syntax,law,"EquationImpl"))
            return ce_error(error,law->offset,"an operation theory's laws are equations naming operations");
        CeAtom *lhs = ce_law_atom(&os->syntax,&os->lexer,&os->arena,law->children[0],error);
        if (!lhs) return false;
        if (lhs->kind != CE_ATOM_LIST || lhs->len != 1 || lhs->items[0]->kind != CE_ATOM_SYMBOL ||
            !identifier(lhs->items[0]->text))
            return ce_error(error,law->offset,"an operation equation's left side is (NAME)");
        CeAtom *rhs = ce_law_atom(&os->syntax,&os->lexer,&os->arena,law->children[1],error);
        if (!rhs) return false;
        CeText name = ce_text(&os->arena,lhs->items[0]->text.data,lhs->items[0]->text.len);
        if (form(rhs,"Vocabulary") && rhs->len >= 2 && rhs->items[1]->kind == CE_ATOM_TEXT &&
            ce_is(rhs->items[1]->text,"cesta-operation/1")) {
            if (!read_vocabulary(os,name,rhs,error)) return false;
        } else if (form(rhs,"Operation") && rhs->len >= 2 && rhs->items[1]->kind == CE_ATOM_TEXT &&
                   ce_is(rhs->items[1]->text,"cesta-operation/1")) {
            if (!read_operation(os,name,rhs,error)) return false;
        } else return ce_error(error,rhs->offset,"an operation equation is (Vocabulary \"cesta-operation/1\" ...) "
                                                 "or (Operation \"cesta-operation/1\" ...)");
    }
    if (!os->vocabulary) return ce_error(error,0,"theory %s declares no vocabulary",theory);
    if (!shared_tables(os,error)) return false;
    for (size_t i = 0; i < os->len; ++i)
        if (!check_operation(&os->items[i],error)) return false;
    return true;
}

static void write_names(FILE *file, const char *head, Names n) {
    fprintf(file,"(%s",head);
    for (size_t i = 0; i < n.len; ++i) fprintf(file," %.*s",(int)n.items[i].len,n.items[i].data);
    fputc(')',file);
}
static void write_word(FILE *file, CeText t) { fputc(' ',file); ce_quote(file,t); }
void ce_operations_write(const CeOperations *os, FILE *file) {
    fprintf(file,"(cesta-operation-vocabulary/1 %.*s (Resources",(int)os->vocabulary_name.len,os->vocabulary_name.data);
    for (size_t i = 0; i < os->nresources; ++i) {
        fprintf(file," (%.*s",(int)os->resources[i].name.len,os->resources[i].name.data);
        write_word(file,os->resources[i].kind); fputc(')',file);
    }
    fputs(") ",file); write_names(file,"Charges",os->charges); fputc(' ',file);
    write_names(file,"Profiles",os->profiles); fputs(")\n",file);
    for (size_t i = 0; i < os->len; ++i) {
        const Operation *op = &os->items[i];
        fprintf(file,"(cesta-operation/1 %.*s ",(int)op->name.len,op->name.data);
        ce_quote(file,op->spelling);
        fprintf(file," %.*s (Dispatch",(int)op->symbol.len,op->symbol.data);
        write_word(file,op->dispatch); fputs(") (Forms",file);
        for (size_t f = 0; f < op->nforms; ++f) {
            fputs(" (Form",file);
            for (size_t a = 0; a < op->forms[f].nargs; ++a) {
                const OpArg *arg = &op->forms[f].args[a];
                fputs(" (Arg",file); write_word(file,arg->evaluation); write_word(file,arg->demand);
                write_word(file,arg->ownership); fputc(')',file);
            }
            fputc(')',file);
        }
        if (op->footprint) {
            fputs(") (Footprint ",file); write_names(file,"Reads",op->reads); fputc(' ',file);
            write_names(file,"Writes",op->writes);
        } else fputs(") (Footprint \"unknown\"",file);
        static const char *const words[] = {"no","yes","unknown"};
        fprintf(file,") (Context \"%s\" \"%s\"",words[op->blocks],words[op->branches]);
        write_word(file,op->progress); fputc(' ',file); write_names(file,"Charges",op->charges);
        fputs(") (Result",file); write_word(file,op->result);
        fprintf(file,") (Declared \"%s\" \"%s\" \"%s\" (Replay",op->statik ? "yes" : "no",op->type_pure ? "yes" : "no",
                op->speculative ? "yes" : "no");
        write_word(file,op->replay); fputs(") (Through",file);
        for (size_t t = 0; t < op->nthrough; ++t) fprintf(file," %llu",(unsigned long long)op->through[t]);
        fputs(")) ",file); write_names(file,"Profiles",op->profiles); fputs(")\n",file);
    }
}

/* ---- Derived rows -------------------------------------------------------------
   The lists and rows a runtime keeps, derived from the table as
   OperationContracts.lowerRows states them: the static, pure, type-level and
   speculative lists (the last two beyond the pure list, which the runtime adds
   to both), the query-effect rows of known footprints, and the operations
   whose footprint is unknown. */

static bool is_store(const CeOperations *os, CeText r) {
    for (size_t i = 0; i < os->nresources; ++i)
        if (ce_equal(os->resources[i].name,r)) return ce_is(os->resources[i].kind,"store");
    return false;
}
/* 2: writes a store; 1: reads a store; 0: neither. Known footprints only. */
static unsigned query_effect(const CeOperations *os, const Operation *op) {
    for (size_t i = 0; i < op->writes.len; ++i) if (is_store(os,op->writes.items[i])) return 2;
    for (size_t i = 0; i < op->reads.len; ++i) if (is_store(os,op->reads.items[i])) return 1;
    return 0;
}
typedef enum { ROW_STATIC, ROW_PURE, ROW_TYPE_PURE, ROW_SPECULATIVE, ROW_UNKNOWN } RowList;
static bool in_list(const Operation *op, RowList list) {
    switch (list) {
    case ROW_STATIC: return op->statik;
    case ROW_PURE: return pure_row(op);
    case ROW_TYPE_PURE: return op->type_pure && !pure_row(op);
    case ROW_SPECULATIVE: return op->speculative && !pure_row(op);
    case ROW_UNKNOWN: return !op->footprint;
    }
    return false;
}
static const char *const row_heads[] = {"Static","Pure","TypePure","Speculative","Unknown"};
static void write_rows(const CeOperations *os, FILE *file) {
    fputs("(cesta-operation-rows/1",file);
    for (int list = ROW_STATIC; list <= ROW_SPECULATIVE; ++list) {
        fprintf(file," (%s",row_heads[list]);
        for (size_t i = 0; i < os->len; ++i)
            if (in_list(&os->items[i],(RowList)list))
                fprintf(file," %.*s",(int)os->items[i].symbol.len,os->items[i].symbol.data);
        fputc(')',file);
    }
    fputs(" (Query",file);
    for (size_t i = 0; i < os->len; ++i) {
        const Operation *op = &os->items[i];
        unsigned e = op->footprint ? query_effect(os,op) : 0;
        if (e) fprintf(file," (%.*s %u)",(int)op->symbol.len,op->symbol.data,e);
    }
    fprintf(file,") (%s",row_heads[ROW_UNKNOWN]);
    for (size_t i = 0; i < os->len; ++i)
        if (in_list(&os->items[i],ROW_UNKNOWN))
            fprintf(file," %.*s",(int)os->items[i].symbol.len,os->items[i].symbol.data);
    fputs("))\n",file);
}
static void write_protocols(const CeOperations *os, FILE *file) {
    for (size_t i = 0; i < os->len; ++i) {
        const Operation *op = &os->items[i];
        fprintf(file,"(cesta-operation-protocol/1 %.*s (ChargeKinds",(int)op->name.len,op->name.data);
        for (size_t j = 0; j < op->charges.len; ++j) {
            CeText label = op->charges.items[j], kind = {0};
            for (size_t k = 0; k < os->ncharge_kinds; ++k)
                if (ce_equal(label,os->charge_kinds[k].label)) kind = os->charge_kinds[k].kind;
            fprintf(file," (%.*s ",(int)label.len,label.data);
            if (kind.len) fprintf(file,"%.*s",(int)kind.len,kind.data);
            else fputs("\"unknown\"",file);
            fputc(')',file);
        }
        fputs(") (Progress ",file); ce_quote(file,op->progress); fputs(" (Bound ",file);
        if (op->has_bound) fprintf(file,"%llu",(unsigned long long)op->bound);
        else if (ce_is(op->progress,"single")) fputc('1',file);
        else fputs("\"unknown\"",file);
        fputs(")) (Replay ",file); ce_quote(file,op->replay); fputs("))\n",file);
    }
}
/* The owner's execution-contract spec spells a query head (query-head NAME EFFECT). */
static void write_metta(const CeOperations *os, FILE *file) {
    fputs(";; Generated by cesta operations metta; edit the operation module instead.\n",file);
    for (size_t i = 0; i < os->len; ++i) {
        const Operation *op = &os->items[i];
        unsigned e = op->footprint ? query_effect(os,op) : 0;
        if (e) fprintf(file,"(query-head %.*s %s)\n",(int)op->spelling.len,op->spelling.data,
                       e == 2 ? "world-write" : "relational-query");
    }
}
static void write_list_macro(const CeOperations *os, FILE *file, const char *name, RowList list) {
    fprintf(file,"#define CESTA_OPERATION_%s(X)",name);
    for (size_t i = 0; i < os->len; ++i)
        if (in_list(&os->items[i],list))
            fprintf(file," \\\n    X(%.*s)",(int)os->items[i].symbol.len,os->items[i].symbol.data);
    fputs("\n\n",file);
}
static void write_header(const CeOperations *os, FILE *file) {
    fputs("/* Generated by cesta operations header; edit the operation module instead. */\n"
          "#ifndef CESTA_OPERATIONS_GENERATED_H\n#define CESTA_OPERATIONS_GENERATED_H\n\n",file);
    write_list_macro(os,file,"STATIC_FIELDS",ROW_STATIC);
    write_list_macro(os,file,"PURE_FIELDS",ROW_PURE);
    fputs("/* Beyond the pure list, which the runtime adds to both lists below. */\n",file);
    write_list_macro(os,file,"TYPE_PURE_FIELDS",ROW_TYPE_PURE);
    write_list_macro(os,file,"SPECULATIVE_FIELDS",ROW_SPECULATIVE);
    fputs("/* Query effects of known footprints: 1 reads a store, 2 writes one. */\n"
          "#define CESTA_OPERATION_QUERY_EFFECT_ROWS(X)",file);
    for (size_t i = 0; i < os->len; ++i) {
        const Operation *op = &os->items[i];
        unsigned e = op->footprint ? query_effect(os,op) : 0;
        if (e) fprintf(file," \\\n    X(%.*s, %uu)",(int)op->symbol.len,op->symbol.data,e);
    }
    fputs("\n\n/* Operations whose footprint no one has established: never read as pure. */\n",file);
    write_list_macro(os,file,"UNKNOWN_FOOTPRINT_FIELDS",ROW_UNKNOWN);
    fputs("#endif\n",file);
}

int ce_operation_command(int argc, char **argv) {
    const char *mode = argc > 2 ? argv[2] : "";
    bool header = !strcmp(mode,"header");
    if (!((argc == 5 && (!strcmp(mode,"check") || !strcmp(mode,"rows") || !strcmp(mode,"metta") || !strcmp(mode,"tables") ||
                        !strcmp(mode,"protocols"))) ||
          (header && argc == 7 && !strcmp(argv[5],"-o")))) {
        fputs("usage: cesta operations check|rows|metta|tables|protocols MODULE THEORY\n"
              "       cesta operations header MODULE THEORY -o OUTPUT.h\n",stderr);
        return 1;
    }
    CeError error = {{0},0}; CeOperations *os = ce_operations_new(&error); int result = 1;
    if (!os || !ce_operations_load(os,argv[3],argv[4],&error)) {
        fprintf(stderr,"%s: byte %zu: %s\n",argv[3],error.offset,error.message);
        ce_operations_free(os); return 1;
    }
    if (header) {
        FILE *out = fopen(argv[6],"w");
        if (!out) fprintf(stderr,"%s: cannot write\n",argv[6]);
        else { write_header(os,out); result = fclose(out) ? 1 : 0; }
    } else {
        if (!strcmp(mode,"check")) ce_operations_write(os,stdout);
        else if (!strcmp(mode,"rows")) write_rows(os,stdout);
        else if (!strcmp(mode,"tables")) ce_tables_write(&os->shared,stdout);
        else if (!strcmp(mode,"protocols")) write_protocols(os,stdout);
        else write_metta(os,stdout);
        result = 0;
    }
    ce_operations_free(os); return result;
}

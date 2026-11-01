#define _POSIX_C_SOURCE 200809L
#include "checked.h"
#include "diagnose.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    CK_DEAD, CK_UNIT, CK_BOOL, CK_U8, CK_U16, CK_U32, CK_U64, CK_SIZE,
    CK_READ, CK_WRITE, CK_OWN, CK_ARRAY
} CkType;

typedef struct {
    CeText name;
    CkType type;
    const CeAtom *site;
    bool mutable;
} CkVariable;

typedef struct {
    CkType type;
    size_t root;
    size_t *ancestors, nancestors;
} CkCell;

typedef struct { CkCell *cells; bool reachable; } CkState;
typedef struct { CeText name; CkType type; } CkParameter;

typedef struct {
    CeText name, native;
    CkParameter *params; size_t nparams;
    CeText *effects; size_t neffects;
    CkType result; int from;
    const CeAtom *site, *body;
    CeText obligation, origin, owner_allocate, owner_release;
    bool foreign, imported;
} CkFunction;

typedef struct { size_t byte, offset, presumed; } CkMark;
enum { CK_HELP_READ = 1, CK_HELP_WRITE = 2, CK_HELP_ADD = 4,
       CK_HELP_SUB = 8, CK_HELP_MUL = 16, CK_HELP_SHIFT = 32, CK_HELP_FAIL = 64,
       CK_HELP_DIV = 128 };

typedef struct Checked Checked;
struct Checked {
    CeArena *arena;
    CeError *error;
    CeText source, name, failure, allocate, release;
    const char *identity;
    CkFunction *functions; size_t nfunctions, function_capacity;
    CeText *dependencies; size_t ndependencies, dependency_capacity;
    const Checked *parent;
    size_t capacity;
    unsigned word_bits;
    FILE *output, *plan;
    bool lines, needs_memory, needs_allocation;
    unsigned helpers;
    CeText *guards; size_t nguards, guard_capacity;
    CkMark *marks; size_t nmarks, mark_capacity;
};

typedef struct {
    Checked *module;
    CkFunction *function;
    CkVariable *variables; size_t nvariables;
    size_t *visible, nvisible;
    CkState state;
    unsigned indent;
    const CkState *loop_entry;
    size_t loop_visible;
    unsigned loops;
} CkBody;

typedef struct { CkType type; char *code; } CkExpression;
enum { CK_NORMAL = 1, CK_RETURN = 2, CK_BREAK = 4, CK_CONTINUE = 8 };

static bool form(const CeAtom *a, const char *name) {
    return a && a->kind == CE_ATOM_LIST && a->len &&
        a->items[0]->kind == CE_ATOM_SYMBOL && ce_is(a->items[0]->text,name);
}

bool ce_checked_document(const CeAtom *a) {
    return form(a,"cesta") && a->len >= 2 && a->items[1]->kind == CE_ATOM_SYMBOL &&
        ce_is(a->items[1]->text,"checked-c/1");
}

static bool bad(Checked *m, const CeAtom *a, const char *format, ...) {
    char text[512]; va_list args;
    va_start(args,format); vsnprintf(text,sizeof text,format,args); va_end(args);
    return ce_error(m->error,a ? a->offset : 0,"%s",text);
}

static char *print_string(Checked *m, const char *format, ...) {
    va_list args, copy;
    va_start(args,format); va_copy(copy,args);
    int length = vsnprintf(NULL,0,format,copy); va_end(copy);
    if (length < 0) { va_end(args); return NULL; }
    char *result = ce_alloc(m->arena,(size_t)length+1);
    vsnprintf(result,(size_t)length+1,format,args); va_end(args);
    return result;
}

static char *text_string(Checked *m, CeText t) {
    char *result = ce_alloc(m->arena,t.len+1);
    memcpy(result,t.data,t.len); return result;
}

static bool word_in(CeText name, const char *words) {
    for (const char *at = words; *at;) {
        while (*at == ' ') ++at;
        const char *end = strchr(at,' ');
        if (!end) end = at+strlen(at);
        if ((size_t)(end-at) == name.len && !memcmp(at,name.data,name.len)) return true;
        at = end;
    }
    return false;
}

static bool identifier(Checked *m, const CeAtom *a, CeText *out) {
    if (!a || a->kind != CE_ATOM_SYMBOL || !a->text.len)
        return bad(m,a,"expected a checked identifier");
    for (size_t i = 0; i < a->text.len; ++i) {
        unsigned c = a->text.data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              c == '_' || (i && c >= '0' && c <= '9')))
            return bad(m,a,"checked identifiers use C identifier spelling");
    }
    if (a->text.data[0] == '_' ||
        (a->text.len >= 6 && !memcmp(a->text.data,"cesta_",6)) ||
        word_in(a->text,
        "auto break case char const continue default do double else enum extern "
        "float for goto if inline int long register restrict return short signed "
        "sizeof static struct switch typedef union unsigned void volatile while "
        "_Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary _Noreturn "
        "_Static_assert _Thread_local bool true false size_t ptrdiff_t uint8_t "
        "uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t uintptr_t "
        "intptr_t SIZE_MAX PTRDIFF_MAX UINT8_MAX UINT16_MAX UINT32_MAX UINT64_MAX "
        "CHAR_BIT NULL offsetof alignas alignof constexpr nullptr static_assert thread_local typeof typeof_unqual "
        "CESTA_CHECKED_BYTE_VIEWS_DEFINED CESTA_CHECKED_HINTS_DEFINED "
        "CESTA_CHECKED_OPTIMIZE CESTA_CHECKED_COLD CESTA_CHECKED_ALLOC_SIZE"))
        return bad(m,a,"identifier is reserved by the checked C interface");
    *out = a->text; return true;
}

static CkType type_of(const CeAtom *a) {
    if (!a || a->kind != CE_ATOM_SYMBOL) return CK_DEAD;
    static const char *names[] = {"", "unit", "bool", "u8", "u16", "u32", "u64",
                                  "usize", "read", "write", "own"};
    for (size_t i = 1; i < sizeof(names)/sizeof(*names); ++i)
        if (ce_is(a->text,names[i])) return (CkType)i;
    return CK_DEAD;
}

static const char *type_name(CkType t) {
    static const char *names[] = {"dead","unit","bool","u8","u16","u32","u64",
                                  "usize","read","write","own","array"};
    return names[t];
}

static const char *c_type(CkType t) {
    static const char *names[] = {"void","void","bool","uint8_t","uint16_t","uint32_t",
        "uint64_t","size_t","cesta_read_bytes","cesta_write_bytes","cesta_owned_bytes",
        "cesta_write_bytes"};
    return names[t];
}

static bool scalar(CkType t) { return t >= CK_BOOL && t <= CK_SIZE; }
static bool integer(CkType t) { return t >= CK_U8 && t <= CK_SIZE; }
static bool span(CkType t) { return t >= CK_READ && t <= CK_ARRAY; }
static bool loan(CkType t) { return t == CK_READ || t == CK_WRITE; }
static bool owner(CkType t) { return t == CK_OWN || t == CK_ARRAY; }

static unsigned width(Checked *m, CkType t) {
    if (t == CK_SIZE) return m->word_bits;
    static const unsigned widths[] = {0,0,1,8,16,32,64};
    return t <= CK_U64 ? widths[t] : 0;
}

static uint64_t maximum(Checked *m, CkType t) {
    unsigned bits = width(m,t);
    return bits == 64 ? UINT64_MAX : (UINT64_C(1)<<bits)-1;
}

static bool natural(Checked *m, const CeAtom *a, uint64_t limit, uint64_t *value) {
    if (!a || a->kind != CE_ATOM_SYMBOL || !a->text.len ||
        (a->text.len > 1 && a->text.data[0] == '0'))
        return bad(m,a,"expected a canonical unsigned decimal integer");
    uint64_t n = 0;
    for (size_t i = 0; i < a->text.len; ++i) {
        unsigned c = a->text.data[i];
        if (c < '0' || c > '9' || n > limit/10 || (n == limit/10 && c-'0' > limit%10))
            return bad(m,a,"unsigned integer is outside its declared type");
        n = n*10+c-'0';
    }
    *value = n; return true;
}

static size_t count_atoms(const CeAtom *a) {
    size_t count = 1;
    if (a->kind == CE_ATOM_LIST)
        for (size_t i = 0; i < a->len; ++i) count += count_atoms(a->items[i]);
    return count;
}

static void indent(CkBody *b) {
    for (unsigned i = 0; i < b->indent; ++i) fputs("    ",b->module->output);
}

static void c_string(FILE *f, CeText t) {
    fputc('"',f);
    for (size_t i = 0; i < t.len; ++i) {
        unsigned c = t.data[i];
        if (c == '"' || c == '\\') { fputc('\\',f); fputc((int)c,f); }
        else if (c < 32 || c >= 127) fprintf(f,"\\%03o",c);
        else fputc((int)c,f);
    }
    fputc('"',f);
}

static void source_line(CkBody *b, const CeAtom *a) {
    Checked *m = b->module;
    if (!m->lines || !m->identity || !a) return;
    size_t line = 1;
    for (size_t i = 0; i < a->offset && i < m->source.len; ++i)
        if (m->source.data[i] == '\n') ++line;
    fprintf(m->output,"#line %zu ",line);
    c_string(m->output,(CeText){(unsigned char *)m->identity,strlen(m->identity)});
    fputc('\n',m->output);
    if (m->nmarks == m->mark_capacity) {
        m->mark_capacity = m->mark_capacity ? m->mark_capacity*2 : 32;
        m->marks = ce_resize(m->marks,m->mark_capacity,sizeof(*m->marks));
    }
    long byte = ftell(m->output);
    if (byte >= 0) m->marks[m->nmarks++] = (CkMark){(size_t)byte,a->offset,line};
}

static void add_guard(Checked *m, CeText name) {
    for (size_t i = 0; i < m->nguards; ++i) if (ce_equal(m->guards[i],name)) return;
    if (m->nguards == m->guard_capacity) {
        size_t capacity = m->guard_capacity ? m->guard_capacity*2 : 16;
        CeText *guards = ce_alloc(m->arena,capacity*sizeof(*guards));
        if (m->nguards) memcpy(guards,m->guards,m->nguards*sizeof(*guards));
        m->guards = guards; m->guard_capacity = capacity;
    }
    m->guards[m->nguards++] = name;
}

static CkFunction *add_function(Checked *m) {
    if (m->nfunctions == m->function_capacity) {
        size_t capacity = m->function_capacity ? m->function_capacity*2 : 8;
        CkFunction *functions = ce_alloc(m->arena,capacity*sizeof(*functions));
        if (m->nfunctions) memcpy(functions,m->functions,m->nfunctions*sizeof(*functions));
        m->functions = functions; m->function_capacity = capacity;
    }
    return &m->functions[m->nfunctions++];
}

static bool generate(Checked *m, const CeAtom *doc, bool header, CeNativeOutput *out);
static bool import_module(Checked *m, const CeAtom *site);

static bool parse_signature(Checked *m, const CeAtom *a, CkFunction *f) {
    f->site = a; f->from = -1; f->foreign = form(a,"foreign");
    size_t first = f->foreign ? 3 : 2;
    if (a->len <= first || !identifier(m,a->items[1],&f->name)) return false;
    if (f->foreign && !identifier(m,a->items[2],&f->native)) return false;
    bool params = false, result = false, effects = false, obligation = false;
    CeText result_source = {0};
    for (size_t i = first; i < a->len; ++i) {
        const CeAtom *field = a->items[i];
        if (form(field,"params") && !params) {
            params = true; f->nparams = field->len-1;
            f->params = ce_alloc(m->arena,f->nparams*sizeof(*f->params));
            for (size_t k = 0; k < f->nparams; ++k) {
                const CeAtom *p = field->items[k+1];
                if (p->kind != CE_ATOM_LIST || p->len != 2 ||
                    !identifier(m,p->items[0],&f->params[k].name)) return false;
                CkType t = type_of(p->items[1]);
                if (t == CK_DEAD || t == CK_UNIT) return bad(m,p,"invalid parameter type");
                f->params[k].type = t;
                for (size_t j = 0; j < k; ++j)
                    if (ce_equal(f->params[j].name,f->params[k].name))
                        return bad(m,p,"duplicate parameter");
            }
        } else if (form(field,"returns") && !result) {
            result = true;
            if (field->len != 2 && field->len != 4) return bad(m,field,"returns has a type and optional from parameter");
            f->result = type_of(field->items[1]);
            if (f->result == CK_DEAD) return bad(m,field,"unknown return type");
            if (field->len == 4) {
                if (!ce_is(field->items[2]->text,"from") ||
                    !identifier(m,field->items[3],&result_source)) return false;
            }
        } else if (form(field,"effects") && !effects) {
            effects = true; f->neffects = field->len-1;
            f->effects = ce_alloc(m->arena,f->neffects*sizeof(*f->effects));
            for (size_t k = 0; k < f->neffects; ++k) {
                if (!identifier(m,field->items[k+1],&f->effects[k])) return false;
                for (size_t j = 0; j < k; ++j)
                    if (ce_equal(f->effects[j],f->effects[k])) return bad(m,field,"duplicate effect");
            }
        } else if (form(field,"body") && !f->foreign && !f->body) f->body = field;
        else if (form(field,"obligation") && f->foreign && !obligation) {
            obligation = true;
            if (field->len != 2 || field->items[1]->kind != CE_ATOM_TEXT || !field->items[1]->text.len)
                return bad(m,field,"foreign interfaces name a non-empty native obligation");
            f->obligation = field->items[1]->text;
        } else return bad(m,field,"unknown or repeated function field");
    }
    if (!params || !result || !effects || (!f->foreign && !f->body) || (f->foreign && !obligation))
        return bad(m,a,"function requires params, returns, effects and a body or foreign obligation");
    if (loan(f->result)) {
        for (size_t i = 0; i < f->nparams; ++i)
            if (ce_equal(result_source,f->params[i].name)) f->from = (int)i;
        if (f->from < 0 || !loan(f->params[f->from].type) ||
            (f->result == CK_WRITE && f->params[f->from].type != CK_WRITE))
            return bad(m,a,"a returned view borrows from a compatible view parameter");
    } else if (result_source.len) return bad(m,a,"only a returned view has a from parameter");
    return true;
}

static bool parse_module(Checked *m, const CeAtom *doc) {
    if (!ce_checked_document(doc)) return bad(m,doc,"expected a cesta checked-c/1 document");
    m->word_bits = 64;
    const CeAtom **imports = ce_alloc(m->arena,doc->len*sizeof(*imports)); size_t nimports = 0;
    bool bits = false;
    for (size_t i = 2; i < doc->len; ++i) {
        const CeAtom *a = doc->items[i];
        if (form(a,"module") && !m->name.len) {
            if (a->len != 2 || !identifier(m,a->items[1],&m->name)) return false;
        } else if (form(a,"word-bits") && !bits) {
            uint64_t n;
            if (a->len != 2 || !natural(m,a->items[1],64,&n)) return false;
            if (n != 32 && n != 64) return bad(m,a,"word-bits is 32 or 64");
            m->word_bits = (unsigned)n; bits = true;
        } else if (form(a,"failure") && !m->failure.len) {
            if (a->len != 2 || !identifier(m,a->items[1],&m->failure)) return false;
        } else if (form(a,"allocator") && !m->allocate.len) {
            if (a->len != 3 || !identifier(m,a->items[1],&m->allocate) ||
                !identifier(m,a->items[2],&m->release)) return false;
        } else if (form(a,"fn") || form(a,"foreign")) {
            CkFunction *f = add_function(m);
            if (!parse_signature(m,a,f)) return false;
            for (size_t k = 0; k+1 < m->nfunctions; ++k)
                if (ce_equal(f->name,m->functions[k].name)) return bad(m,a,"duplicate checked function");
        } else if (form(a,"import")) {
            CeText prefix;
            if (a->len != 4 || a->items[1]->kind != CE_ATOM_TEXT || !a->items[1]->text.len ||
                memchr(a->items[1]->text.data,0,a->items[1]->text.len) || !ce_is(a->items[2]->text,"as") ||
                !identifier(m,a->items[3],&prefix)) return bad(m,a,"import has a module path and an explicit as prefix");
            for (size_t k = 0; k < nimports; ++k)
                if (ce_equal(prefix,imports[k]->items[3]->text)) return bad(m,a,"duplicate checked import prefix");
            imports[nimports++] = a;
        } else return bad(m,a,"unknown or repeated checked module declaration");
    }
    if (!m->name.len || !m->failure.len) return bad(m,doc,"checked module requires module and failure declarations");
    for (size_t i = 0; i < nimports; ++i) if (!import_module(m,imports[i])) return false;
    m->capacity = count_atoms(doc)+1;
    add_guard(m,m->failure);
    if (m->allocate.len) {
        if (ce_equal(m->allocate,m->release) || ce_equal(m->failure,m->allocate) ||
            ce_equal(m->failure,m->release)) return bad(m,doc,"runtime interfaces need distinct native identifiers");
        add_guard(m,m->allocate); add_guard(m,m->release);
    }
    for (size_t i = 0; i < m->nfunctions; ++i) {
        CkFunction *f = &m->functions[i];
        if (f->imported) continue;
        bool owns = f->result == CK_OWN;
        for (size_t k = 0; k < f->nparams; ++k) owns |= f->params[k].type == CK_OWN;
        if (owns && !m->allocate.len) return bad(m,f->site,"own interfaces require an allocator pair");
        if (owns) m->needs_allocation = true;
    }
    return true;
}

static const char *function_name(Checked *m, const CkFunction *f) {
    return f->foreign || f->imported ? text_string(m,f->native) :
        print_string(m,"%.*s_%.*s",(int)m->name.len,m->name.data,(int)f->name.len,f->name.data);
}

static void add_dependency(Checked *m, CeText path) {
    for (size_t i = 0; i < m->ndependencies; ++i)
        if (ce_same_file((const char *)m->dependencies[i].data,(const char *)path.data)) return;
    if (m->ndependencies == m->dependency_capacity) {
        size_t capacity = m->dependency_capacity ? m->dependency_capacity*2 : 8;
        CeText *dependencies = ce_alloc(m->arena,capacity*sizeof(*dependencies));
        if (m->ndependencies) memcpy(dependencies,m->dependencies,m->ndependencies*sizeof(*dependencies));
        m->dependencies = dependencies; m->dependency_capacity = capacity;
    }
    m->dependencies[m->ndependencies++] = path;
}

static bool import_module(Checked *m, const CeAtom *site) {
    CeText written = site->items[1]->text, prefix = site->items[3]->text;
    const char *slash = m->identity ? strrchr(m->identity,'/') : NULL;
    size_t base = slash && written.data[0] != '/' ? (size_t)(slash-m->identity)+1 : 0;
    char *path = ce_alloc(m->arena,base+written.len+1);
    if (base) memcpy(path,m->identity,base);
    memcpy(path+base,written.data,written.len);
    for (const Checked *parent = m; parent; parent = parent->parent)
        if (parent->identity && ce_same_file(parent->identity,path))
            return bad(m,site,"checked import cycle through %s",path);
    CeError error = {{0},0}; CeText source; CeAtom *doc; CeNativeOutput output = {0};
    if (!ce_read_file(m->arena,path,&source,&error) || !ce_atom_read(m->arena,source,&doc,&error))
        return bad(m,site,"checked import %s: %s",path,error.message);
    Checked child = {.arena=m->arena,.error=&error,.source=source,.identity=path,.parent=m,.plan=m->plan};
    bool ok = generate(&child,doc,false,&output); free(child.marks);
    if (!ok) {
        size_t line = 1, column = 1;
        for (size_t i = 0; i < error.offset && i < source.len; ++i) {
            if (source.data[i] == '\n') { ++line; column = 1; } else ++column;
        }
        return bad(m,site,"checked import %s:%zu:%zu: %s",path,line,column,
                   error.message[0] ? error.message : "malformed checked declaration");
    }
    if (m->word_bits != child.word_bits) return bad(m,site,"checked import word-bits differs from the caller");
    add_dependency(m,ce_cstr(m->arena,path));
    for (size_t i = 0; i < output.ndependencies; ++i) add_dependency(m,output.dependencies[i]);
    for (size_t i = 0; i < child.nfunctions; ++i) {
        CkFunction function = child.functions[i];
        if (function.foreign || function.imported) continue;
        function.native = ce_cstr(m->arena,function_name(&child,&function));
        function.name = ce_cstr(m->arena,print_string(m,"%.*s_%.*s",(int)prefix.len,prefix.data,
                                                     (int)function.name.len,function.name.data));
        for (size_t k = 0; k < m->nfunctions; ++k)
            if (ce_equal(function.name,m->functions[k].name)) return bad(m,site,"checked import collides with an existing function name");
        function.imported = true; function.site = site;
        function.origin = ce_cstr(m->arena,path);
        function.owner_allocate = child.allocate; function.owner_release = child.release;
        function.obligation = ce_cstr(m->arena,print_string(m,"Checked module interface: %.*s",(int)function.native.len,function.native.data));
        *add_function(m) = function;
    }
    return true;
}

static const char *variable_name(CkBody *b, size_t id) {
    return text_string(b->module,b->variables[id].name);
}

static CkState copy_state(CkBody *b, const CkState *state) {
    CkState copy = *state;
    copy.cells = ce_alloc(b->module->arena,b->module->capacity*sizeof(*copy.cells));
    memcpy(copy.cells,state->cells,b->module->capacity*sizeof(*copy.cells));
    return copy;
}

static int find_variable(CkBody *b, const CeAtom *a) {
    if (a->kind != CE_ATOM_SYMBOL) { bad(b->module,a,"expected a variable"); return -1; }
    for (size_t n = b->nvisible; n > 0; --n) {
        size_t id = b->visible[n-1];
        if (ce_equal(b->variables[id].name,a->text)) {
            if (b->state.cells[id].type == CK_DEAD) {
                bad(b->module,a,"value was moved, dropped or its borrow ended"); return -1;
            }
            return (int)id;
        }
    }
    bad(b->module,a,"unknown checked variable"); return -1;
}

static int bind_variable(CkBody *b, const CeAtom *site, CeText name, CkType type, bool mutable) {
    Checked *m = b->module;
    for (size_t i = 0; i < m->nfunctions; ++i)
        if (ce_is(name,function_name(m,&m->functions[i]))) {
            bad(m,site,"variable shadows an emitted function identifier"); return -1;
        }
    if (ce_equal(name,m->failure) || ce_equal(name,m->allocate) || ce_equal(name,m->release)) {
        bad(m,site,"variable shadows a native runtime interface"); return -1;
    }
    for (size_t n = 0; n < b->nvisible; ++n)
        if (ce_equal(b->variables[b->visible[n]].name,name)) {
            bad(b->module,site,"checked variable shadows an active binding"); return -1;
        }
    if (b->nvariables >= b->module->capacity) { bad(b->module,site,"binding inventory overflow"); return -1; }
    size_t id = b->nvariables++;
    b->variables[id] = (CkVariable){name,type,site,mutable};
    add_guard(m,name);
    b->visible[b->nvisible++] = id;
    b->state.cells[id] = (CkCell){.type=type,.root=id};
    return (int)id;
}

static bool ancestor(const CkCell *cell, size_t id) {
    for (size_t i = 0; i < cell->nancestors; ++i) if (cell->ancestors[i] == id) return true;
    return false;
}

static bool span_access(CkBody *b, const CeAtom *site, size_t id, bool writing) {
    CkCell *cell = &b->state.cells[id];
    if (!span(cell->type) || (writing && cell->type == CK_READ))
        return bad(b->module,site,"access requires a %s byte view",writing ? "writable" : "live");
    for (size_t i = 0; i < b->nvariables; ++i) {
        const CkCell *other = &b->state.cells[i];
        if (i == id || !loan(other->type) || other->root != cell->root || ancestor(cell,i)) continue;
        if (writing || other->type == CK_WRITE)
            return bad(b->module,site,"access conflicts with a live %s borrow",type_name(other->type));
    }
    return true;
}

static bool unborrowed(CkBody *b, const CeAtom *site, size_t id) {
    size_t root = b->state.cells[id].root;
    for (size_t i = 0; i < b->nvariables; ++i)
        if (loan(b->state.cells[i].type) && b->state.cells[i].root == root)
            return bad(b->module,site,"owner still has a live borrow");
    return true;
}

static bool end_loan(CkBody *b, const CeAtom *site, size_t id) {
    if (!loan(b->state.cells[id].type)) return bad(b->module,site,"end requires a live borrow");
    for (size_t i = 0; i < b->nvariables; ++i)
        if (loan(b->state.cells[i].type) && ancestor(&b->state.cells[i],id))
            return bad(b->module,site,"borrow still has a live reborrow");
    b->state.cells[id] = (CkCell){0};
    return true;
}

static void event(CkBody *b, const char *action, size_t a, size_t c, CkType type) {
    if (b->module->plan)
        fprintf(b->module->plan,"      (%s %zu %zu %s)\n",action,a,c,type_name(type));
}

static bool expression(CkBody *b, const CeAtom *a, CkExpression *out);
static bool call(CkBody *b, const CeAtom *a, CkExpression *out, int *borrow_source);
static bool block(CkBody *b, const CeAtom *a, unsigned *flow);

static bool same_cell(const CkCell *a, const CkCell *c) {
    if (a->type != c->type) return false;
    if (a->type == CK_DEAD) return true;
    return a->root == c->root && a->nancestors == c->nancestors &&
        (!a->nancestors || !memcmp(a->ancestors,c->ancestors,a->nancestors*sizeof(*a->ancestors)));
}

static bool same_visible(CkBody *b, const CkState *a, const CkState *c, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        size_t id = b->visible[i];
        if (!same_cell(&a->cells[id],&c->cells[id])) return false;
    }
    return true;
}

static bool has_effect(CkFunction *f, CeText name) {
    for (size_t i = 0; i < f->neffects; ++i) if (ce_equal(f->effects[i],name)) return true;
    return false;
}

static bool require_effect(CkBody *b, const CeAtom *a, const char *name) {
    return has_effect(b->function,ce_cstr(b->module->arena,name)) ||
        bad(b->module,a,"function requires declared effect %s",name);
}

static bool typed(CkBody *b, const CeAtom *a, CkExpression expr, CkType expected) {
    return expr.type == expected ||
        bad(b->module,a,"expected %s, found %s",type_name(expected),type_name(expr.type));
}

static char *coerce_view(CkBody *b, size_t id, CkType type, bool foreign) {
    const char *name = variable_name(b,id);
    if (foreign) return print_string(b->module,"%s.data, %s.len",name,name);
    return print_string(b->module,"(%s){%s.data, %s.len}",c_type(type),name,name);
}

static int borrow_binding(CkBody *b, const CeAtom *site, CeText name,
                          size_t source, CkType type, bool copying) {
    if (!span_access(b,site,source,type == CK_WRITE)) return -1;
    CkCell origin = b->state.cells[source];
    int id = bind_variable(b,site,name,type,false);
    if (id < 0) return -1;
    size_t extra = copying ? 0 : 1;
    size_t *ancestors = ce_alloc(b->module->arena,(origin.nancestors+extra)*sizeof(*ancestors));
    if (extra) ancestors[0] = source;
    if (origin.nancestors) memcpy(ancestors+extra,origin.ancestors,origin.nancestors*sizeof(*ancestors));
    b->state.cells[id] = (CkCell){type,origin.root,ancestors,origin.nancestors+extra};
    event(b,copying ? "copy" : "borrow",(size_t)id,source,type);
    return id;
}

static bool call(CkBody *b, const CeAtom *a, CkExpression *out, int *borrow_source) {
    Checked *m = b->module;
    if (!form(a,"call") || a->len < 2 || a->items[1]->kind != CE_ATOM_SYMBOL)
        return bad(m,a,"expected a function call");
    CkFunction *f = NULL;
    for (size_t i = 0; i < m->nfunctions; ++i)
        if (ce_equal(a->items[1]->text,m->functions[i].name)) f = &m->functions[i];
    if (!f) return bad(m,a->items[1],"unknown checked function");
    if (a->len != f->nparams+2) return bad(m,a,"call argument count disagrees with function signature");
    bool transfers = f->result == CK_OWN;
    for (size_t i = 0; i < f->nparams; ++i) transfers |= f->params[i].type == CK_OWN;
    if (f->imported && transfers && (!ce_equal(m->allocate,f->owner_allocate) || !ce_equal(m->release,f->owner_release)))
        return bad(m,a,"owned checked calls require the same allocator and release pair in both modules");
    for (size_t i = 0; i < f->neffects; ++i)
        if (!has_effect(b->function,f->effects[i]))
            return bad(m,a,"call has an effect absent from the caller's signature");
    int *arguments = ce_alloc(m->arena,f->nparams*sizeof(*arguments));
    char **codes = ce_alloc(m->arena,f->nparams*sizeof(*codes));
    for (size_t i = 0; i < f->nparams; ++i) arguments[i] = -1;
    for (size_t i = 0; i < f->nparams; ++i) {
        CkType type = f->params[i].type;
        const CeAtom *arg = a->items[i+2];
        if (span(type)) {
            int id = find_variable(b,arg);
            if (id < 0) return false;
            CkCell value = b->state.cells[id];
            if (type == CK_OWN) {
                if (value.type != CK_OWN || !unborrowed(b,arg,(size_t)id))
                    return bad(m,arg,"own parameter consumes an unborrowed allocated owner");
                codes[i] = print_string(m,"%s",variable_name(b,(size_t)id));
            } else {
                if (!span_access(b,arg,(size_t)id,type == CK_WRITE)) return false;
                codes[i] = coerce_view(b,(size_t)id,type,f->foreign);
            }
            for (size_t j = 0; j < i; ++j)
                if (arguments[j] >= 0 && b->state.cells[arguments[j]].root == value.root &&
                    (type != CK_READ || f->params[j].type != CK_READ))
                    return bad(m,arg,"call arguments alias an exclusive or consumed owner");
            arguments[i] = id;
        } else {
            CkExpression value;
            if (!expression(b,arg,&value) || !typed(b,arg,value,type)) return false;
            codes[i] = value.code;
        }
    }
    char *code = NULL; size_t length = 0; FILE *file = open_memstream(&code,&length);
    if (!file) return bad(m,a,"cannot allocate call output");
    fprintf(file,"%s(",function_name(m,f));
    for (size_t i = 0; i < f->nparams; ++i) fprintf(file,"%s%s",i ? ", " : "",codes[i]);
    fputc(')',file);
    if (fclose(file)) { free(code); return bad(m,a,"cannot finish call output"); }
    out->type = f->result; out->code = text_string(m,(CeText){(unsigned char *)code,length}); free(code);
    *borrow_source = f->from >= 0 ? arguments[f->from] : -1;
    if (m->plan) {
        fprintf(m->plan,"      (invoke %.*s (",(int)f->name.len,f->name.data);
        for (size_t i = 0; i < f->nparams; ++i)
            if (arguments[i] >= 0) fprintf(m->plan," (%d %s)",arguments[i],type_name(f->params[i].type));
        fputs("))\n",m->plan);
    }
    for (size_t i = 0; i < f->nparams; ++i)
        if (f->params[i].type == CK_OWN) b->state.cells[arguments[i]] = (CkCell){0};
    return true;
}

static bool expression(CkBody *b, const CeAtom *a, CkExpression *out) {
    Checked *m = b->module;
    if (a->kind == CE_ATOM_SYMBOL) {
        if (ce_is(a->text,"true") || ce_is(a->text,"false")) {
            *out = (CkExpression){CK_BOOL,text_string(m,a->text)}; return true;
        }
        int id = find_variable(b,a);
        if (id < 0) return false;
        CkType type = b->state.cells[id].type;
        if (!scalar(type)) return bad(m,a,"byte views are not scalar expressions");
        *out = (CkExpression){type,text_string(m,a->text)}; return true;
    }
    if (a->kind != CE_ATOM_LIST || !a->len || a->items[0]->kind != CE_ATOM_SYMBOL)
        return bad(m,a,"invalid checked expression");
    CkType literal = type_of(a->items[0]);
    if (integer(literal) && a->len == 2) {
        uint64_t n;
        if (!natural(m,a->items[1],maximum(m,literal),&n)) return false;
        *out = (CkExpression){literal,print_string(m,"((%s)UINT64_C(%" PRIu64 "))",c_type(literal),n)};
        return true;
    }
    if (form(a,"len") && a->len == 2) {
        int id = find_variable(b,a->items[1]);
        if (id < 0 || !span_access(b,a,(size_t)id,false)) return false;
        event(b,"inspect",(size_t)id,0,CK_READ);
        *out = (CkExpression){CK_SIZE,print_string(m,"%s.len",variable_name(b,(size_t)id))}; return true;
    }
    if (form(a,"at") && a->len == 3) {
        int id = find_variable(b,a->items[1]); CkExpression index;
        if (id < 0 || !span_access(b,a,(size_t)id,false) || !expression(b,a->items[2],&index) ||
            !typed(b,a->items[2],index,CK_SIZE)) return false;
        m->needs_memory = true;
        m->helpers |= CK_HELP_READ|CK_HELP_FAIL;
        event(b,"read",(size_t)id,0,CK_READ);
        *out = (CkExpression){CK_U8,print_string(m,"cesta_%.*s_read(%s.data, %s.len, %s)",
            (int)m->name.len,m->name.data,variable_name(b,(size_t)id),variable_name(b,(size_t)id),index.code)};
        return true;
    }
    if ((form(a,"as") || form(a,"truncate")) && a->len == 3) {
        CkType to = type_of(a->items[1]); CkExpression value;
        if (!integer(to) || !expression(b,a->items[2],&value) || !integer(value.type))
            return bad(m,a,"integer conversion requires integer types");
        if (form(a,"as") && width(m,to) < width(m,value.type))
            return bad(m,a,"narrowing requires an explicit truncate operation");
        *out = (CkExpression){to,print_string(m,"((%s)(%s))",c_type(to),value.code)}; return true;
    }
    if (form(a,"not") && a->len == 2) {
        CkExpression value;
        if (!expression(b,a->items[1],&value) || !typed(b,a,value,CK_BOOL)) return false;
        *out = (CkExpression){CK_BOOL,print_string(m,"(!(%s))",value.code)}; return true;
    }
    if (a->len != 3) return bad(m,a,"unknown checked expression or wrong arity");
    CkExpression left, right;
    if (!expression(b,a->items[1],&left) || !expression(b,a->items[2],&right)) return false;
    const char *name = (const char *)a->items[0]->text.data;
    const char *operator = NULL;
    if (!strcmp(name,"and") || !strcmp(name,"or")) {
        if (!typed(b,a,left,CK_BOOL) || !typed(b,a,right,CK_BOOL)) return false;
        operator = !strcmp(name,"and") ? "&&" : "||";
        *out = (CkExpression){CK_BOOL,print_string(m,"(%s) %s (%s)",left.code,operator,right.code)}; return true;
    }
    const char *comparisons[] = {"eq","ne","lt","le","gt","ge"};
    const char *operators[] = {"==","!=","<","<=",">",">="};
    for (size_t i = 0; i < sizeof(comparisons)/sizeof(*comparisons); ++i)
        if (!strcmp(name,comparisons[i])) operator = operators[i];
    if (operator) {
        if (left.type != right.type || !scalar(left.type)) return bad(m,a,"comparison operands have different types");
        *out = (CkExpression){CK_BOOL,print_string(m,"(%s) %s (%s)",left.code,operator,right.code)}; return true;
    }
    if (!integer(left.type)) return bad(m,a,"integer operation requires unsigned operands");
    if (!strcmp(name,"shl") || !strcmp(name,"shr")) {
        if (!typed(b,a,right,CK_SIZE)) return false;
        m->helpers |= CK_HELP_SHIFT|CK_HELP_FAIL;
        *out = (CkExpression){left.type,print_string(m,"((%s)cesta_%.*s_shift((uint64_t)(%s), %s, %u, %s))",
            c_type(left.type),(int)m->name.len,m->name.data,left.code,right.code,width(m,left.type),
            !strcmp(name,"shl") ? "true" : "false")}; return true;
    }
    if (left.type != right.type) return bad(m,a,"integer operands have different types");
    if (!strcmp(name,"div") || !strcmp(name,"rem")) {
        m->helpers |= CK_HELP_DIV|CK_HELP_FAIL;
        *out = (CkExpression){left.type,print_string(m,"((%s)cesta_%.*s_div((uint64_t)(%s), (uint64_t)(%s), %s))",
            c_type(left.type),(int)m->name.len,m->name.data,left.code,right.code,
            !strcmp(name,"rem") ? "true" : "false")};
        return true;
    }
    if (!strcmp(name,"bit-and")) operator = "&";
    else if (!strcmp(name,"bit-or")) operator = "|";
    else if (!strcmp(name,"bit-xor")) operator = "^";
    else if (!strcmp(name,"wrap-add")) operator = "+";
    else if (!strcmp(name,"wrap-sub")) operator = "-";
    else if (!strcmp(name,"wrap-mul")) operator = "*";
    if (operator) {
        *out = (CkExpression){left.type,print_string(m,"((%s)(((uint64_t)(%s)) %s ((uint64_t)(%s))))",
            c_type(left.type),left.code,operator,right.code)}; return true;
    }
    if (!strcmp(name,"add") || !strcmp(name,"sub") || !strcmp(name,"mul")) {
        m->helpers |= CK_HELP_FAIL|(!strcmp(name,"add") ? CK_HELP_ADD :
                                  !strcmp(name,"sub") ? CK_HELP_SUB : CK_HELP_MUL);
        *out = (CkExpression){left.type,print_string(m,"((%s)cesta_%.*s_%s((uint64_t)(%s), (uint64_t)(%s), UINT64_C(%" PRIu64 ")))",
            c_type(left.type),(int)m->name.len,m->name.data,name,left.code,right.code,maximum(m,left.type))}; return true;
    }
    return bad(m,a,"unknown checked scalar operation");
}

static bool cleanup(CkBody *b, size_t mark, const CeAtom *site) {
    Checked *m = b->module;
    for (size_t n = b->nvisible; n > mark; --n) {
        size_t id = b->visible[n-1]; CkCell cell = b->state.cells[id];
        if (loan(cell.type)) {
            if (!end_loan(b,site,id)) return false;
            event(b,"end",id,0,cell.type);
        } else if (owner(cell.type)) {
            if (!unborrowed(b,site,id)) return false;
            if (cell.type == CK_OWN) {
                if (!m->release.len) return bad(m,site,"owned allocation requires an allocator pair");
                if (!require_effect(b,site,"heap")) return false;
                indent(b); fprintf(m->output,"if (%s.data) %.*s(%s.data);\n",
                    variable_name(b,id),(int)m->release.len,m->release.data,variable_name(b,id));
            }
            event(b,"drop",id,0,cell.type); b->state.cells[id] = (CkCell){0};
        } else b->state.cells[id] = (CkCell){0};
    }
    return true;
}

static bool return_statement(CkBody *b, const CeAtom *a) {
    Checked *m = b->module; CkType expected = b->function->result;
    if ((expected == CK_UNIT && a->len != 1) || (expected != CK_UNIT && a->len != 2))
        return bad(m,a,"return value disagrees with function result");
    char *code = NULL; int source = -1;
    if (expected != CK_UNIT) {
        const CeAtom *value = a->items[1]; CkExpression expression_value;
        if (form(value,"call")) {
            if (!call(b,value,&expression_value,&source) || !typed(b,value,expression_value,expected)) return false;
            code = expression_value.code;
        } else if (span(expected)) {
            source = find_variable(b,value);
            if (source < 0) return false;
            if (expected == CK_OWN) {
                if (b->state.cells[source].type != CK_OWN || !unborrowed(b,value,(size_t)source))
                    return bad(m,value,"only an unborrowed allocated owner can be returned as own");
                code = print_string(m,"%s",variable_name(b,(size_t)source));
                b->state.cells[source] = (CkCell){0}; event(b,"take",(size_t)source,0,CK_OWN);
            } else {
                if (!span_access(b,value,(size_t)source,expected == CK_WRITE)) return false;
                code = coerce_view(b,(size_t)source,expected,false);
            }
        } else {
            if (!expression(b,value,&expression_value) || !typed(b,value,expression_value,expected)) return false;
            code = expression_value.code;
        }
        if (loan(expected)) {
            size_t from = (size_t)b->function->from;
            if (source < 0 || b->state.cells[source].root != from)
                return bad(m,value,"returned view escapes the lifetime named by from");
            event(b,"borrow-result",(size_t)source,from,expected);
        }
        add_guard(m,ce_cstr(m->arena,"cesta_result"));
        indent(b); fprintf(m->output,"%s cesta_result = %s;\n",c_type(expected),code);
    }
    if (!cleanup(b,0,a)) return false;
    indent(b); fprintf(m->output,"return%s;\n",expected == CK_UNIT ? "" : " cesta_result");
    if (m->plan) fputs("      (return)\n",m->plan);
    b->state.reachable = false; return true;
}

static bool declare_value(CkBody *b, const CeAtom *a) {
    Checked *m = b->module; CeText name;
    if (a->len != 4 || !identifier(m,a->items[1],&name))
        return bad(m,a,"let/var has a name, type and initializer");
    CkType type = type_of(a->items[2]);
    if (type == CK_DEAD || type == CK_UNIT) return bad(m,a->items[2],"invalid variable type");
    CkExpression value; int origin = -1;
    bool called = form(a->items[3],"call");
    CkCell moved = {0}; int moved_from = -1;
    if (called) {
        if (!call(b,a->items[3],&value,&origin) || !typed(b,a,value,type)) return false;
    } else if (span(type)) {
        origin = find_variable(b,a->items[3]);
        if (origin < 0) return false;
        if (type == CK_OWN) {
            if (b->state.cells[origin].type != CK_OWN || !unborrowed(b,a,(size_t)origin))
                return bad(m,a,"own initializer moves an unborrowed allocated owner");
            moved = b->state.cells[origin]; moved_from = origin;
            value = (CkExpression){CK_OWN,print_string(m,"%s",variable_name(b,(size_t)origin))};
        } else {
            if (!span_access(b,a,(size_t)origin,type == CK_WRITE)) return false;
            value = (CkExpression){type,coerce_view(b,(size_t)origin,type,false)};
        }
    } else if (!expression(b,a->items[3],&value) || !typed(b,a,value,type)) return false;
    int id = loan(type) ? borrow_binding(b,a,name,(size_t)origin,type,false) :
        bind_variable(b,a,name,type,form(a,"var"));
    if (id < 0) return false;
    if (type == CK_OWN) {
        if (!m->allocate.len) return bad(m,a,"own type requires an allocator pair");
        m->needs_allocation = true;
        if (moved_from >= 0) {
            b->state.cells[id] = moved; b->state.cells[moved_from] = (CkCell){0};
            event(b,"move",(size_t)id,(size_t)moved_from,type);
        } else event(b,"own-result",(size_t)id,0,type);
    }
    indent(b); fprintf(m->output,"%s%s %.*s = %s;\n",
        scalar(type) && !form(a,"var") ? "const " : "",c_type(type),(int)name.len,name.data,value.code);
    indent(b); fprintf(m->output,"(void)%.*s;\n",(int)name.len,name.data);
    return true;
}

static bool statement(CkBody *b, const CeAtom *a, unsigned *flow) {
    Checked *m = b->module;
    *flow = CK_NORMAL;
    if (!a || a->kind != CE_ATOM_LIST || !a->len) return bad(m,a,"expected a checked statement");
    source_line(b,a);
    if (form(a,"require") && a->len == 2) {
        CkExpression condition;
        if (!expression(b,a->items[1],&condition) || !typed(b,a,condition,CK_BOOL)) return false;
        m->helpers |= CK_HELP_FAIL;
        indent(b); fprintf(m->output,"if (!(%s)) cesta_%.*s_fail();\n",
            condition.code,(int)m->name.len,m->name.data);
        return true;
    }
    if (form(a,"let") || form(a,"var")) return declare_value(b,a);
    if (form(a,"set") && a->len == 3) {
        int id = find_variable(b,a->items[1]); CkExpression value;
        if (id < 0 || !scalar(b->state.cells[id].type) || !b->variables[id].mutable)
            return bad(m,a,"set requires a mutable scalar variable");
        if (!expression(b,a->items[2],&value) || !typed(b,a,value,b->state.cells[id].type)) return false;
        indent(b); fprintf(m->output,"%s = %s;\n",variable_name(b,(size_t)id),value.code); return true;
    }
    if (form(a,"array") && a->len == 3) {
        CeText name; uint64_t count;
        if (!identifier(m,a->items[1],&name) ||
            !natural(m,a->items[2],maximum(m,CK_SIZE)>>1,&count)) return false;
        int id = bind_variable(b,a,name,CK_ARRAY,false);
        if (id < 0) return false;
        add_guard(m,ce_cstr(m->arena,print_string(m,"cesta_%d_storage",id)));
        indent(b); fprintf(m->output,"uint8_t cesta_%d_storage[%" PRIu64 "] = {0};\n",id,count ? count : 1);
        indent(b); fprintf(m->output,"cesta_write_bytes %.*s = {cesta_%d_storage, (size_t)UINT64_C(%" PRIu64 ")};\n",
            (int)name.len,name.data,id,count);
        indent(b); fprintf(m->output,"(void)%.*s;\n",(int)name.len,name.data);
        event(b,"array",(size_t)id,0,CK_ARRAY); return true;
    }
    if (form(a,"borrow") && a->len == 4) {
        CeText name; CkType type = type_of(a->items[2]);
        if (!identifier(m,a->items[1],&name) || !loan(type)) return bad(m,a,"borrow requires read or write");
        int origin = find_variable(b,a->items[3]);
        if (origin < 0) return false;
        int id = borrow_binding(b,a,name,(size_t)origin,type,false);
        if (id < 0) return false;
        add_guard(m,ce_cstr(m->arena,print_string(m,"cesta_%d_start",id)));
        add_guard(m,ce_cstr(m->arena,print_string(m,"cesta_%d_count",id)));
        indent(b); fprintf(m->output,"%s %.*s = %s;\n",c_type(type),(int)name.len,name.data,
            coerce_view(b,(size_t)origin,type,false));
        indent(b); fprintf(m->output,"(void)%.*s;\n",(int)name.len,name.data); return true;
    }
    if (form(a,"slice") && a->len == 6) {
        m->helpers |= CK_HELP_FAIL;
        CeText name; CkType type = type_of(a->items[2]); CkExpression start, count;
        if (!identifier(m,a->items[1],&name) || !loan(type)) return bad(m,a,"slice requires read or write");
        int origin = find_variable(b,a->items[3]);
        if (origin < 0 || !expression(b,a->items[4],&start) || !typed(b,a,start,CK_SIZE) ||
            !expression(b,a->items[5],&count) || !typed(b,a,count,CK_SIZE)) return false;
        int id = borrow_binding(b,a,name,(size_t)origin,type,false);
        if (id < 0) return false;
        indent(b); fprintf(m->output,"const size_t cesta_%d_start = %s;\n",id,start.code);
        indent(b); fprintf(m->output,"const size_t cesta_%d_count = %s;\n",id,count.code);
        const char *base = variable_name(b,(size_t)origin);
        indent(b); fprintf(m->output,"if (cesta_%d_start > %s.len || cesta_%d_count > %s.len - cesta_%d_start)\n",id,base,id,base,id);
        indent(b); fprintf(m->output,"    cesta_%.*s_fail();\n",(int)m->name.len,m->name.data);
        indent(b); fprintf(m->output,"%s %.*s = {%s.data, cesta_%d_count};\n",c_type(type),(int)name.len,name.data,base,id);
        indent(b); fprintf(m->output,"if (cesta_%d_start) %.*s.data += cesta_%d_start;\n",id,(int)name.len,name.data,id);
        indent(b); fprintf(m->output,"(void)%.*s;\n",(int)name.len,name.data); return true;
    }
    if ((form(a,"copy") || form(a,"move")) && a->len == 3) {
        CeText name;
        if (!identifier(m,a->items[1],&name)) return false;
        int source = find_variable(b,a->items[2]);
        if (source < 0) return false;
        CkCell cell = b->state.cells[source];
        int id;
        if (form(a,"copy")) {
            if (cell.type == CK_READ) id = borrow_binding(b,a,name,(size_t)source,CK_READ,true);
            else if (scalar(cell.type)) id = bind_variable(b,a,name,cell.type,false);
            else return bad(m,a,"copy cannot duplicate an owner or exclusive borrow");
        } else {
            if (owner(cell.type)) { if (!unborrowed(b,a,(size_t)source)) return false; }
            else if (loan(cell.type)) {
                for (size_t i = 0; i < b->nvariables; ++i)
                    if (loan(b->state.cells[i].type) && ancestor(&b->state.cells[i],(size_t)source))
                        return bad(m,a,"move cannot invalidate a live reborrow");
            } else return bad(m,a,"move requires an owner or borrowed view");
            id = bind_variable(b,a,name,cell.type,false);
            if (id >= 0) {
                b->state.cells[id] = cell; b->state.cells[source] = (CkCell){0};
                event(b,"move",(size_t)id,(size_t)source,cell.type);
            }
        }
        if (id < 0) return false;
        indent(b); fprintf(m->output,"%s %.*s = %s;\n",c_type(cell.type),(int)name.len,name.data,variable_name(b,(size_t)source));
        indent(b); fprintf(m->output,"(void)%.*s;\n",(int)name.len,name.data); return true;
    }
    if (form(a,"end") && a->len == 2) {
        int id = find_variable(b,a->items[1]);
        if (id < 0) return false;
        CkType type = b->state.cells[id].type;
        if (!end_loan(b,a,(size_t)id)) return false;
        event(b,"end",(size_t)id,0,type); return true;
    }
    if (form(a,"drop") && a->len == 2) {
        int id = find_variable(b,a->items[1]);
        if (id < 0 || !owner(b->state.cells[id].type)) return bad(m,a,"drop requires an owner");
        if (!unborrowed(b,a,(size_t)id)) return false;
        CkType type = b->state.cells[id].type;
        if (type == CK_OWN) {
            if (!m->release.len) return bad(m,a,"dropping own requires an allocator pair");
            if (!require_effect(b,a,"heap")) return false;
            indent(b); fprintf(m->output,"if (%s.data) %.*s(%s.data);\n",variable_name(b,(size_t)id),
                (int)m->release.len,m->release.data,variable_name(b,(size_t)id));
        }
        event(b,"drop",(size_t)id,0,type); b->state.cells[id] = (CkCell){0}; return true;
    }
    if (form(a,"put") && a->len == 4) {
        m->helpers |= CK_HELP_WRITE|CK_HELP_FAIL;
        int id = find_variable(b,a->items[1]); CkExpression index, value;
        if (id < 0 || !span_access(b,a,(size_t)id,true) ||
            !expression(b,a->items[2],&index) || !typed(b,a,index,CK_SIZE) ||
            !expression(b,a->items[3],&value) || !typed(b,a,value,CK_U8)) return false;
        event(b,"write",(size_t)id,0,CK_WRITE);
        indent(b); fprintf(m->output,"cesta_%.*s_write(%s.data, %s.len, %s, %s);\n",
            (int)m->name.len,m->name.data,variable_name(b,(size_t)id),variable_name(b,(size_t)id),index.code,value.code);
        return true;
    }
    if (form(a,"call")) {
        CkExpression value; int from;
        if (!call(b,a,&value,&from)) return false;
        if (value.type == CK_OWN) return bad(m,a,"an owned return must be bound or returned");
        indent(b); fprintf(m->output,"%s%s;\n",value.type == CK_UNIT ? "" : "(void)",value.code); return true;
    }
    if (form(a,"return")) {
        if (!return_statement(b,a)) return false;
        *flow = CK_RETURN; return true;
    }
    if (form(a,"break") || form(a,"continue")) {
        if (a->len != 1 || !b->loops) return bad(m,a,"break/continue requires a loop");
        if (!cleanup(b,b->loop_visible,a)) return false;
        if (!same_visible(b,&b->state,b->loop_entry,b->loop_visible))
            return bad(m,a,"loop exit changes an outer ownership or borrow state");
        indent(b); fprintf(m->output,"%s;\n",form(a,"break") ? "break" : "continue");
        if (m->plan) fprintf(m->plan,"      (%s)\n",form(a,"break") ? "break" : "continue");
        *flow = form(a,"break") ? CK_BREAK : CK_CONTINUE;
        b->state.reachable = false; return true;
    }
    if (form(a,"if") && a->len == 4 && form(a->items[2],"then") && form(a->items[3],"else")) {
        CkExpression condition;
        if (!expression(b,a->items[1],&condition) || !typed(b,a,condition,CK_BOOL)) return false;
        CkState initial = copy_state(b,&b->state); size_t mark = b->nvisible;
        if (m->plan) fputs("      (branch\n",m->plan);
        indent(b); fprintf(m->output,"if (%s) {\n",condition.code); ++b->indent;
        unsigned left_flow, right_flow;
        if (m->plan) fputs("        (then\n",m->plan);
        if (!block(b,a->items[2],&left_flow)) return false;
        CkState left = copy_state(b,&b->state);
        b->state = copy_state(b,&initial); b->nvisible = mark;
        if (m->plan) fputs("        )\n        (else\n",m->plan);
        --b->indent; indent(b); fputs("} else {\n",m->output); ++b->indent;
        if (!block(b,a->items[3],&right_flow)) return false;
        --b->indent; indent(b); fputs("}\n",m->output);
        if (m->plan) fputs("        )\n      )\n",m->plan);
        if ((left_flow&CK_NORMAL) && (right_flow&CK_NORMAL) &&
            !same_visible(b,&left,&b->state,mark))
            return bad(m,a,"branches leave different ownership or borrow states");
        if (left_flow&CK_NORMAL) b->state = left;
        b->state.reachable = ((left_flow|right_flow)&CK_NORMAL) != 0;
        *flow = left_flow|right_flow; return true;
    }
    if ((form(a,"while") && a->len == 3 && form(a->items[2],"body")) ||
        (form(a,"for") && a->len == 4 && form(a->items[3],"body"))) {
        bool counted = form(a,"for");
        size_t mark = b->nvisible;
        CkExpression condition = {0}; int view = -1, index = -1;
        if (counted) {
            CeText name;
            view = find_variable(b,a->items[2]);
            if (view < 0 || !span_access(b,a,(size_t)view,false) || !identifier(m,a->items[1],&name)) return false;
            index = bind_variable(b,a,name,CK_SIZE,false);
            if (index < 0) return false;
            event(b,"inspect",(size_t)view,0,CK_READ);
        } else if (!expression(b,a->items[1],&condition) || !typed(b,a,condition,CK_BOOL)) return false;
        CkState initial = copy_state(b,&b->state);
        const CkState *previous_entry = b->loop_entry;
        size_t previous_visible = b->loop_visible;
        b->loop_entry = &initial; b->loop_visible = mark; ++b->loops;
        indent(b);
        if (counted) fprintf(m->output,"for (size_t %s = 0; %s < %s.len; ++%s) {\n",
            variable_name(b,(size_t)index),variable_name(b,(size_t)index),
            variable_name(b,(size_t)view),variable_name(b,(size_t)index));
        else fprintf(m->output,"while (%s) {\n",condition.code);
        ++b->indent; unsigned body_flow;
        if (m->plan) fputs("      (loop\n",m->plan);
        if (!block(b,a->items[counted ? 3 : 2],&body_flow)) return false;
        if ((body_flow&CK_NORMAL) && !same_visible(b,&initial,&b->state,mark))
            return bad(m,a,"loop body does not preserve its ownership and borrow invariant");
        --b->indent; indent(b); fputs("}\n",m->output);
        if (m->plan) fputs("      )\n",m->plan);
        --b->loops; b->loop_entry = previous_entry; b->loop_visible = previous_visible;
        b->state = initial;
        if (counted) b->state.cells[index] = (CkCell){0};
        b->nvisible = mark; b->state.reachable = true;
        *flow = CK_NORMAL|(body_flow&CK_RETURN); return true;
    }
    if (form(a,"allocate") && a->len == 5 && form(a->items[3],"then") && form(a->items[4],"else")) {
        if (!m->allocate.len || !require_effect(b,a,"heap"))
            return bad(m,a,"allocate requires an allocator pair and the heap effect");
        CeText name; CkExpression count;
        if (!identifier(m,a->items[1],&name) || !expression(b,a->items[2],&count) || !typed(b,a,count,CK_SIZE)) return false;
        CkState initial = copy_state(b,&b->state); size_t mark = b->nvisible;
        int id = bind_variable(b,a,name,CK_OWN,false);
        if (id < 0) return false;
        static const char *const temporaries[] = {"count","data","index"};
        for (size_t k = 0; k < sizeof temporaries/sizeof *temporaries; ++k)
            add_guard(m,ce_cstr(m->arena,print_string(m,"cesta_%d_%s",id,temporaries[k])));
        m->needs_allocation = true;
        indent(b); fputs("{\n",m->output); ++b->indent;
        indent(b); fprintf(m->output,"const size_t cesta_%d_count = %s;\n",id,count.code);
        indent(b); fprintf(m->output,"uint8_t *cesta_%d_data = NULL;\n",id);
        indent(b); fprintf(m->output,"if (cesta_%d_count && cesta_%d_count <= PTRDIFF_MAX) cesta_%d_data = %.*s(cesta_%d_count);\n",
            id,id,id,(int)m->allocate.len,m->allocate.data,id);
        indent(b); fprintf(m->output,"if (cesta_%d_data || !cesta_%d_count) {\n",id,id);
        ++b->indent; unsigned good_flow, bad_flow;
        indent(b); fprintf(m->output,"cesta_owned_bytes %.*s = {cesta_%d_data, cesta_%d_count};\n",
            (int)name.len,name.data,id,id);
        indent(b); fprintf(m->output,"for (size_t cesta_%d_index = 0; cesta_%d_index < cesta_%d_count; ++cesta_%d_index)\n",
            id,id,id,id);
        indent(b); fprintf(m->output,"    cesta_%d_data[cesta_%d_index] = 0;\n",id,id);
        if (m->plan) { fputs("      (branch\n        (then\n",m->plan); event(b,"own",(size_t)id,0,CK_OWN); }
        if (!block(b,a->items[3],&good_flow)) return false;
        if (good_flow&CK_NORMAL) { if (!cleanup(b,mark,a)) return false; }
        CkState good = copy_state(b,&b->state); b->nvisible = mark;
        b->state = copy_state(b,&initial);
        --b->indent; indent(b); fputs("} else {\n",m->output); ++b->indent;
        if (m->plan) fputs("        )\n        (else\n",m->plan);
        if (!block(b,a->items[4],&bad_flow)) return false;
        --b->indent; indent(b); fputs("}\n",m->output);
        --b->indent; indent(b); fputs("}\n",m->output);
        if (m->plan) fputs("        )\n      )\n",m->plan);
        if ((good_flow&CK_NORMAL) && (bad_flow&CK_NORMAL) &&
            !same_visible(b,&good,&b->state,mark)) return bad(m,a,"allocation branches leave different ownership states");
        if (good_flow&CK_NORMAL) b->state = good;
        b->state.reachable = ((good_flow|bad_flow)&CK_NORMAL) != 0;
        *flow = good_flow|bad_flow; return true;
    }
    if (form(a,"block")) {
        indent(b); fputs("{\n",m->output); ++b->indent;
        if (!block(b,a,flow)) return false;
        --b->indent; indent(b); fputs("}\n",m->output); return true;
    }
    return bad(m,a,"unknown checked statement or wrong arity");
}

static bool block(CkBody *b, const CeAtom *a, unsigned *flow) {
    size_t mark = b->nvisible; unsigned exits = 0;
    b->state.reachable = true;
    for (size_t i = 1; i < a->len; ++i) {
        if (!b->state.reachable) return bad(b->module,a->items[i],"unreachable checked statement");
        unsigned next;
        if (!statement(b,a->items[i],&next)) return false;
        exits |= next&~CK_NORMAL;
    }
    if (b->state.reachable) {
        if (!cleanup(b,mark,a)) return false;
        exits |= CK_NORMAL;
    }
    b->nvisible = mark; *flow = exits; return true;
}

static void signature(Checked *m, FILE *file, const CkFunction *f, bool names) {
    fprintf(file,"%s %s(",c_type(f->result),function_name(m,f));
    if (!f->nparams) fputs("void",file);
    for (size_t i = 0; i < f->nparams; ++i) {
        if (i) fputs(", ",file);
        CkType type = f->params[i].type;
        if (f->foreign && (type == CK_READ || type == CK_WRITE)) {
            fprintf(file,"%suint8_t *, size_t",type == CK_READ ? "const " : "");
        } else {
            fputs(c_type(type),file);
            if (names) fprintf(file," %.*s",(int)f->params[i].name.len,f->params[i].name.data);
        }
    }
    fputc(')',file);
}

static bool check_global_names(Checked *m) {
    for (size_t i = 0; i < m->nfunctions; ++i) {
        const char *name = function_name(m,&m->functions[i]);
        if (ce_is(m->failure,name) || ce_is(m->allocate,name) || ce_is(m->release,name))
            return bad(m,m->functions[i].site,"function collides with a native runtime interface");
        for (size_t k = 0; k < i; ++k)
            if (!strcmp(name,function_name(m,&m->functions[k]))) {
                CkFunction *first = &m->functions[i], *other = &m->functions[k];
                if (!first->imported || !other->imported ||
                    !ce_same_file((const char *)first->origin.data,(const char *)other->origin.data))
                    return bad(m,first->site,"functions emit the same C identifier");
            }
        add_guard(m,ce_cstr(m->arena,name));
    }
    return true;
}

static void plan_signature(Checked *m, const CkFunction *f) {
    FILE *file = m->plan;
    bool external = f->foreign || f->imported;
    fprintf(file,"  (%s %.*s (params",external ? "foreign" : "function",(int)f->name.len,f->name.data);
    for (size_t i = 0; i < f->nparams; ++i) fprintf(file," %s",type_name(f->params[i].type));
    fprintf(file,") (returns %s %d) (effects",type_name(f->result),f->from);
    for (size_t i = 0; i < f->neffects; ++i) fprintf(file," %.*s",(int)f->effects[i].len,f->effects[i].data);
    fputc(')',file);
    CeText allocate = f->imported ? f->owner_allocate : m->allocate;
    CeText release = f->imported ? f->owner_release : m->release;
    fputs(" (allocator",file);
    if (allocate.len) fprintf(file," %.*s %.*s",(int)allocate.len,allocate.data,(int)release.len,release.data);
    fputc(')',file);
    if (external) {
        fputs(" (obligation ",file);
        CeAtom text = {.kind=CE_ATOM_TEXT,.text=f->obligation};
        ce_atom_write(&text,file); fputs("))\n",file);
    } else fputs("\n    (checks\n",file);
}

static bool function_body(Checked *m, CkFunction *f) {
    CkBody b = {.module=m,.function=f,.indent=1};
    b.variables = ce_alloc(m->arena,m->capacity*sizeof(*b.variables));
    b.visible = ce_alloc(m->arena,m->capacity*sizeof(*b.visible));
    b.state.cells = ce_alloc(m->arena,m->capacity*sizeof(*b.state.cells)); b.state.reachable = true;
    if (m->plan) plan_signature(m,f);
    source_line(&b,f->site); signature(m,m->output,f,true); fputs("\n{\n",m->output);
    for (size_t i = 0; i < f->nparams; ++i) {
        int id = bind_variable(&b,f->site,f->params[i].name,f->params[i].type,false);
        if (id < 0) return false;
        event(&b,"param",(size_t)id,0,f->params[i].type);
        indent(&b); fprintf(m->output,"(void)%s;\n",variable_name(&b,(size_t)id));
        if (span(f->params[i].type)) {
            m->helpers |= CK_HELP_FAIL;
            indent(&b); fprintf(m->output,"if ((%s.len && !%s.data) || %s.len > (size_t)PTRDIFF_MAX)\n",
                variable_name(&b,(size_t)id),variable_name(&b,(size_t)id),variable_name(&b,(size_t)id));
            indent(&b); fprintf(m->output,"    cesta_%.*s_fail();\n",(int)m->name.len,m->name.data);
        }
    }
    unsigned flow;
    if (!block(&b,f->body,&flow)) return false;
    if (flow&CK_NORMAL) {
        if (f->result != CK_UNIT) return bad(m,f->body,"a value-returning function can fall through");
        if (!cleanup(&b,0,f->body)) return false;
        indent(&b); fputs("return;\n",m->output);
        if (m->plan) fputs("      (return)\n",m->plan);
    }
    fputs("}\n\n",m->output);
    if (m->plan) fputs("    ))\n",m->plan);
    return true;
}

static void prelude(Checked *m, FILE *file, bool helpers) {
    fputs("#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <limits.h>\n\n",file);
    fputs("#ifndef CESTA_CHECKED_HINTS_DEFINED\n#define CESTA_CHECKED_HINTS_DEFINED\n"
          "#ifndef CESTA_CHECKED_OPTIMIZE\n#define CESTA_CHECKED_OPTIMIZE 0\n#endif\n"
          "#if CESTA_CHECKED_OPTIMIZE != 0 && CESTA_CHECKED_OPTIMIZE != 1\n"
          "#error \"CESTA_CHECKED_OPTIMIZE must be 0 or 1\"\n#endif\n"
          "#if CESTA_CHECKED_OPTIMIZE && (defined(__GNUC__) || defined(__clang__))\n"
          "#define CESTA_CHECKED_COLD __attribute__((cold, noinline))\n"
          "#define CESTA_CHECKED_ALLOC_SIZE __attribute__((alloc_size(1)))\n"
          "#else\n#define CESTA_CHECKED_COLD\n#define CESTA_CHECKED_ALLOC_SIZE\n#endif\n#endif\n\n",file);
    fprintf(file,"_Static_assert(CHAR_BIT == 8 && sizeof(size_t) * CHAR_BIT == %u &&\n"
                 "               PTRDIFF_MAX == SIZE_MAX / 2,\n"
                 "               \"checked module word-bits disagrees with the C target\");\n\n",m->word_bits);
    static const char *const helper_names[] = {"read","write","add","sub","mul","shift","fail","div"};
    for (size_t i = 0; i < sizeof helper_names/sizeof *helper_names; ++i)
        if (m->helpers&(1u<<i)) add_guard(m,ce_cstr(m->arena,print_string(m,"cesta_%.*s_%s",(int)m->name.len,m->name.data,helper_names[i])));
    for (size_t i = 0; i < m->nguards; ++i) {
        fprintf(file,"#ifdef %.*s\n#error \"checked identifier is a preprocessor macro\"\n#endif\n",
            (int)m->guards[i].len,m->guards[i].data);
    }
    fputs("\n#ifndef CESTA_CHECKED_BYTE_VIEWS_DEFINED\n#define CESTA_CHECKED_BYTE_VIEWS_DEFINED\n"
          "typedef struct { const uint8_t *data; size_t len; } cesta_read_bytes;\n"
          "typedef struct { uint8_t *data; size_t len; } cesta_write_bytes;\n"
          "typedef struct { uint8_t *data; size_t len; } cesta_owned_bytes;\n#endif\n\n",file);
    fprintf(file,"void %.*s(void);\n",(int)m->failure.len,m->failure.data);
    if (m->allocate.len) fprintf(file,"CESTA_CHECKED_ALLOC_SIZE void *%.*s(size_t);\nvoid %.*s(void *);\n",
        (int)m->allocate.len,m->allocate.data,(int)m->release.len,m->release.data);
    for (size_t i = 0; i < m->nfunctions; ++i) { signature(m,file,&m->functions[i],false); fputs(";\n",file); }
    fputc('\n',file);
    if (!helpers) return;
    const char *name = text_string(m,m->name);
    if (m->helpers&CK_HELP_FAIL)
        fprintf(file,"static CESTA_CHECKED_COLD _Noreturn void cesta_%s_fail(void)\n{\n    %.*s();\n    for (;;) {}\n}\n\n",
            name,(int)m->failure.len,m->failure.data);
    if (m->helpers&CK_HELP_READ)
        fprintf(file,"static inline uint8_t cesta_%s_read(const uint8_t *data, size_t len, size_t index)\n"
                     "{\n    if (index >= len) cesta_%s_fail();\n    return data[index];\n}\n\n",name,name);
    if (m->helpers&CK_HELP_WRITE)
        fprintf(file,"static inline void cesta_%s_write(uint8_t *data, size_t len, size_t index, uint8_t value)\n"
                     "{\n    if (index >= len) cesta_%s_fail();\n    data[index] = value;\n}\n\n",name,name);
    if (m->helpers&CK_HELP_ADD)
        fprintf(file,"static inline uint64_t cesta_%s_add(uint64_t a, uint64_t b, uint64_t limit)\n"
                     "{\n    if (b > limit || a > limit - b) cesta_%s_fail();\n    return a + b;\n}\n\n",name,name);
    if (m->helpers&CK_HELP_SUB)
        fprintf(file,"static inline uint64_t cesta_%s_sub(uint64_t a, uint64_t b, uint64_t limit)\n"
                     "{\n    if (a > limit || a < b) cesta_%s_fail();\n    return a - b;\n}\n\n",name,name);
    if (m->helpers&CK_HELP_MUL)
        fprintf(file,"static inline uint64_t cesta_%s_mul(uint64_t a, uint64_t b, uint64_t limit)\n"
                     "{\n    if (a > limit || b > limit || (b && a > limit / b)) cesta_%s_fail();\n    return a * b;\n}\n\n",name,name);
    if (m->helpers&CK_HELP_SHIFT)
        fprintf(file,"static inline uint64_t cesta_%s_shift(uint64_t value, size_t count, unsigned width, bool left)\n"
                     "{\n    if (count >= width) cesta_%s_fail();\n    return left ? value << count : value >> count;\n}\n\n",name,name);
    if (m->helpers&CK_HELP_DIV)
        fprintf(file,"static inline uint64_t cesta_%s_div(uint64_t value, uint64_t divisor, bool remainder)\n"
                     "{\n    if (!divisor) cesta_%s_fail();\n    return remainder ? value %% divisor : value / divisor;\n}\n\n",name,name);
}

static void source_map(Checked *m, CeNativeOutput *out, size_t prefix) {
    if (!m->lines) return;
    size_t lines = 1;
    for (size_t i = 0; i < out->text.len; ++i) if (out->text.data[i] == '\n') ++lines;
    CeMapToken *tokens = ce_alloc(m->arena,lines*sizeof(*tokens)); size_t ntokens = 0;
    CeMapDirective *directives = ce_alloc(m->arena,(m->nmarks+1)*sizeof(*directives));
    size_t ndirectives = 0, next = 0, offset = 0, line = 1, begin = 0;
    for (size_t i = 0; i <= out->text.len; ++i) {
        if (i != out->text.len && out->text.data[i] != '\n') continue;
        while (next < m->nmarks && m->marks[next].byte+prefix <= begin) {
            offset = m->marks[next].offset;
            directives[ndirectives++] = (CeMapDirective){line,m->marks[next].presumed};
            ++next;
        }
        if (i > begin) tokens[ntokens++] = (CeMapToken){line,1,i-begin,offset};
        begin = i+1; ++line;
    }
    out->tokens = tokens; out->ntokens = ntokens;
    out->directives = directives; out->ndirectives = ndirectives;
}

static bool generate(Checked *m, const CeAtom *doc, bool header, CeNativeOutput *out) {
    if (!parse_module(m,doc) || !check_global_names(m)) return false;
    char *body = NULL, *code = NULL; size_t body_length = 0, code_length = 0;
    FILE *functions = open_memstream(&body,&body_length);
    if (!functions) return bad(m,doc,"cannot allocate checked function output");
    m->output = functions;
    if (m->plan) {
        fprintf(m->plan,"(CheckedPermissions \"cesta-permissions/1\" (module %.*s) (word-bits %u)\n",
            (int)m->name.len,m->name.data,m->word_bits);
        for (size_t i = 0; i < m->nfunctions; ++i)
            if (m->functions[i].foreign || m->functions[i].imported) plan_signature(m,&m->functions[i]);
    }
    bool ok = true;
    for (size_t i = 0; ok && i < m->nfunctions; ++i)
        if (!m->functions[i].foreign && !m->functions[i].imported) ok = function_body(m,&m->functions[i]);
    if (m->plan) fputs(")\n",m->plan);
    if (fclose(functions)) ok = bad(m,doc,"cannot finish checked function output");
    if (!ok) { free(body); return false; }
    FILE *file = open_memstream(&code,&code_length);
    if (!file) { free(body); return bad(m,doc,"cannot allocate checked source output"); }
    prelude(m,file,!header); long prefix = ftell(file);
    if (!header && body_length) fwrite(body,1,body_length,file);
    free(body);
    if (fclose(file)) { free(code); return bad(m,doc,"cannot finish checked source output"); }
    out->text = ce_text(m->arena,code,code_length); free(code);
    out->dependencies = m->dependencies; out->ndependencies = m->ndependencies;
    if (!header && prefix >= 0) source_map(m,out,(size_t)prefix);
    return true;
}

bool ce_checked_text(CeArena *arena, CeText source, const char *identity,
                     bool lines, FILE *plan, CeNativeOutput *out, CeError *error) {
    *out = (CeNativeOutput){0};
    CeAtom *doc;
    if (!ce_atom_read(arena,source,&doc,error)) return false;
    Checked m = {.arena=arena,.error=error,.source=source,.identity=identity,.lines=lines,.plan=plan};
    bool ok = generate(&m,doc,false,out);
    free(m.marks);
    if (!ok && !error->message[0]) ce_error(error,doc->offset,"malformed checked declaration");
    return ok;
}

int ce_checked_command(int argc, char **argv) {
    const char *usage = "usage: cesta checked check|plan|header FILE.cesta [-o OUTPUT]\n";
    if (argc < 4) { fputs(usage,stderr); return 2; }
    bool check = !strcmp(argv[2],"check"), plan = !strcmp(argv[2],"plan"), header = !strcmp(argv[2],"header");
    if (!check && !plan && !header) { fputs(usage,stderr); return 2; }
    const char *path = argv[3], *output = NULL;
    for (int i = 4; i < argc; ++i) {
        if (strcmp(argv[i],"-o") || output || i+1 == argc) { fputs(usage,stderr); return 2; }
        output = argv[++i];
    }
    if ((check && output) || (output && ce_same_file(path,output))) {
        fputs("cesta checked: output must differ from the checked source\n",stderr); return 2;
    }
    CeArena arena = {0}; CeError error = {{0},0}; CeText source = {0}; CeAtom *doc = NULL;
    bool ok = ce_read_file(&arena,path,&source,&error) && ce_atom_read(&arena,source,&doc,&error);
    char *record = NULL; size_t length = 0;
    FILE *recording = plan ? open_memstream(&record,&length) : NULL;
    if (plan && !recording) ok = ce_error(&error,0,"cannot allocate permission plan");
    Checked m = {.arena=&arena,.error=&error,.source=source,.identity=path,.plan=recording};
    CeNativeOutput result = {0};
    if (ok) ok = generate(&m,doc,header,&result);
    if (recording && fclose(recording)) ok = ce_error(&error,0,"cannot finish permission plan");
    for (size_t i = 0; ok && output && i < result.ndependencies; ++i)
        if (ce_same_file(output,(const char *)result.dependencies[i].data))
            ok = ce_error(&error,0,"output would overwrite a checked source dependency");
    if (!ok && !error.message[0]) ce_error(&error,doc ? doc->offset : 0,"malformed checked declaration");
    if (ok && check) printf("checked: %zu function interfaces admitted\n",m.nfunctions);
    else if (ok) {
        CeText text = plan ? (CeText){(unsigned char *)record,length} : result.text;
        FILE *file = output ? fopen(output,"wb") : stdout;
        if (!file) ok = ce_error(&error,0,"cannot open checked output");
        else {
            ok = fwrite(text.data,1,text.len,file) == text.len;
            if (output && fclose(file)) ok = false;
            if (!ok) ce_error(&error,0,"cannot write checked output");
        }
    }
    if (!ok) {
        CeRouteStyle style = {.color=ce_route_color(STDERR_FILENO,0,NULL)};
        ce_report_error(stderr,path,source,error.offset,error.message,&style);
    }
    free(m.marks); free(record); ce_arena_free(&arena);
    return ok ? 0 : 1;
}

#define _XOPEN_SOURCE 700
#include "protocol.h"
#include "table.h"

#include <stdlib.h>
#include <string.h>

CeText ce_builtin_TheoryModuleSyntax(void);

typedef struct { CeText *items; size_t len; } Names;
enum { PROTOCOL_OUTCOMES = 7 };
typedef struct {
    CeText id, cname, kind;
    Names requires, uses, proves, invalidates;
} Hook;
typedef struct {
    CeText id, acquire, release, transfer;
    Names acquire_requires, transfer_requires;
} Resource;
typedef struct { CeText kind, id, cname; } Validator;
typedef struct {
    CeText name, context;
    Hook *hooks; size_t nhooks;
    Resource *resources; size_t nresources;
    Names facts, entry_facts, entry_owned;
    Validator *validators; size_t nvalidators;
    Names reasons;
    Names limits;
    Names exit_facts[PROTOCOL_OUTCOMES];
    const CeAtom *checks, *exits;
    bool checks_complete;
    CeAtom *written, *body, *target, *instrumented;
} Protocol;
typedef struct {
    CeArena arena;
    CeGrammar syntax;
    CeLazyLexer lexer;
    CeCatalog catalog;
    Protocol *items; size_t len;
} Protocols;
typedef struct { bool *facts, *owned; bool effect; } State;

static const char *const outcomes[PROTOCOL_OUTCOMES] = {
    "declined", "answered", "done", "raised", "suspended", "resource_failure", "engine_failure"
};
#define COUNT(a) (sizeof(a) / sizeof(*(a)))

static bool form(const CeAtom *a, const char *head) {
    return a->kind == CE_ATOM_LIST && a->len &&
        a->items[0]->kind == CE_ATOM_SYMBOL && ce_is(a->items[0]->text,head);
}
static bool shape(const CeAtom *a, const char *head, size_t len, CeError *e) {
    return (form(a,head) && a->len == len) || ce_error(e,a->offset,"expected %s with %zu operands",head,len-1);
}
static bool name(const CeAtom *a, CeText *out, CeError *e) {
    if (a->kind != CE_ATOM_SYMBOL || !ce_table_c_name(a->text))
        return ce_error(e,a->offset,"protocol names must be C identifiers, excluding keywords");
    *out = a->text; return true;
}
/* These C11 declarations belong to the headers used by generated diagnostics
 * and counters. Native code must also respect its implementation's prelude. */
static bool callable_name(CeText name, size_t offset, CeError *e) {
    static const char reserved[] =
        "FILE fpos_t size_t wchar_t stdin stdout stderr div_t ldiv_t lldiv_t "
        "remove rename tmpfile tmpnam fclose fflush fopen freopen setbuf setvbuf "
        "fprintf fscanf printf scanf snprintf sprintf sscanf vfprintf vfscanf "
        "vprintf vscanf vsnprintf vsprintf vsscanf fgetc fgets fputc fputs getc "
        "getchar putc putchar puts ungetc fread fwrite fgetpos fseek fsetpos ftell "
        "rewind clearerr feof ferror perror atof atoi atol atoll strtod strtof "
        "strtold strtol strtoll strtoul strtoull rand srand aligned_alloc calloc "
        "free malloc realloc abort atexit at_quick_exit exit _Exit quick_exit "
        "getenv system bsearch qsort abs labs llabs div ldiv lldiv mblen mbtowc "
        "wctomb mbstowcs wcstombs intptr_t uintptr_t intmax_t uintmax_t "
        "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t "
        "int_least8_t int_least16_t int_least32_t int_least64_t "
        "uint_least8_t uint_least16_t uint_least32_t uint_least64_t "
        "int_fast8_t int_fast16_t int_fast32_t int_fast64_t "
        "uint_fast8_t uint_fast16_t uint_fast32_t uint_fast64_t UINT64_MAX";
    for (const char *word = reserved; *word;) {
        const char *end = strchr(word,' '); size_t len = end ? (size_t)(end-word) : strlen(word);
        if (name.len == len && !memcmp(name.data,word,len))
            return ce_error(e,offset,"native callable conflicts with a generated standard-header declaration");
        word += len; if (*word) ++word;
    }
    return true;
}
static size_t find(Names names, CeText text) {
    for (size_t i = 0; i < names.len; ++i) if (ce_equal(names.items[i],text)) return i;
    return SIZE_MAX;
}
static void add(Protocols *ps, Names *names, CeText text) {
    if (find(*names,text) != SIZE_MAX) return;
    CeText *items = ce_alloc(&ps->arena,(names->len+1)*sizeof(*items));
    if (names->len) memcpy(items,names->items,names->len*sizeof(*items));
    items[names->len++] = text; names->items = items;
}
static bool names(Protocols *ps, const CeAtom *a, const char *head, Names *out, CeError *e) {
    if (!form(a,head)) return ce_error(e,a->offset,"expected (%s ...)",head);
    out->items = ce_alloc(&ps->arena,a->len*sizeof(*out->items));
    for (size_t i = 1; i < a->len; ++i) {
        CeText text;
        if (!name(a->items[i],&text,e)) return false;
        if (find(*out,text) != SIZE_MAX) return ce_error(e,a->items[i]->offset,"duplicate %s name",head);
        out->items[out->len++] = text;
    }
    return true;
}
static size_t hook_index(const Protocol *p, CeText id) {
    for (size_t i = 0; i < p->nhooks; ++i) if (ce_equal(p->hooks[i].id,id)) return i;
    return SIZE_MAX;
}
static size_t resource_index(const Protocol *p, CeText id) {
    for (size_t i = 0; i < p->nresources; ++i) if (ce_equal(p->resources[i].id,id)) return i;
    return SIZE_MAX;
}
static bool distinct_cname(Protocols *ps, Names *seen, CeText value, size_t offset, CeError *e) {
    if (find(*seen,value) != SIZE_MAX)
        return ce_error(e,offset,"native name %.*s is used more than once",(int)value.len,value.data);
    add(ps,seen,value); return true;
}
static CeText suffixed(Protocols *ps, CeText prefix, const char *suffix) {
    size_t n = strlen(suffix); unsigned char *s = ce_alloc(&ps->arena,prefix.len+n+1);
    memcpy(s,prefix.data,prefix.len); memcpy(s+prefix.len,suffix,n);
    return (CeText){s,prefix.len+n};
}
static CeText guard_name(Protocols *ps, CeText name) {
    static const char prefix[] = "CESTA_PROTOCOL_", suffix[] = "_DEFINED";
    size_t first = sizeof(prefix)-1, last = sizeof(suffix)-1;
    unsigned char *text = ce_alloc(&ps->arena,first+name.len+last+1);
    memcpy(text,prefix,first); memcpy(text+first,name.data,name.len);
    memcpy(text+first+name.len,suffix,last);
    return (CeText){text,first+name.len+last};
}
static bool read_protocol(Protocols *ps, CeText id, CeAtom *a, Protocol *p, CeError *e) {
    if (!form(a,"Protocol") || a->len < 7 || a->len > 9 || a->items[1]->kind != CE_ATOM_TEXT ||
        !ce_is(a->items[1]->text,"cesta-protocol/1"))
        return ce_error(e,a->offset,"expected (Protocol \"cesta-protocol/1\" (Context ...) (Hooks ...) (Resources ...) (Entry ...) (Body ...))");
    p->name = id; p->written = a;
    if (!callable_name(id,a->offset,e)) return false;
    if (!shape(a->items[2],"Context",2,e) || !name(a->items[2]->items[1],&p->context,e)) return false;
    const CeAtom *hs = a->items[3], *rs = a->items[4], *entry = a->items[5];
    if (!form(hs,"Hooks") || !form(rs,"Resources")) return ce_error(e,a->offset,"protocol declares Hooks and Resources");
    p->nhooks = hs->len-1; p->hooks = ce_alloc(&ps->arena,p->nhooks*sizeof(*p->hooks));
    p->nresources = rs->len-1; p->resources = ce_alloc(&ps->arena,p->nresources*sizeof(*p->resources));
    Names cnames = {0};
    add(ps,&cnames,p->name); add(ps,&cnames,p->context);
    add(ps,&cnames,ce_cstr(&ps->arena,"ctx"));
    add(ps,&cnames,ce_cstr(&ps->arena,"cesta_remaining"));
    if (ce_equal(p->name,p->context)) return ce_error(e,a->offset,"function name conflicts with context type");
    if (!distinct_cname(ps,&cnames,guard_name(ps,id),a->offset,e)) return false;
    add(ps,&cnames,suffixed(ps,id,"_outcome"));
    for (size_t i = 0; i < COUNT(outcomes); ++i) {
        CeText stem = suffixed(ps,id,"_"); add(ps,&cnames,suffixed(ps,stem,outcomes[i]));
    }
    for (size_t i = 0; i < p->nhooks; ++i) {
        const CeAtom *h = hs->items[i+1]; Hook *hook = &p->hooks[i];
        if (!form(h,"Hook") || (h->len != 7 && h->len != 8))
            return ce_error(e,h->offset,"Hook takes names, kind, Requires, Uses, Proves and optional Invalidates");
        if (!name(h->items[1],&hook->id,e) ||
            !name(h->items[2],&hook->cname,e) || !callable_name(hook->cname,h->offset,e)) return false;
        for (size_t j = 0; j < i; ++j) if (ce_equal(p->hooks[j].id,hook->id))
            return ce_error(e,h->offset,"duplicate hook ID");
        if (h->items[3]->kind != CE_ATOM_SYMBOL ||
            !(ce_is(h->items[3]->text,"guard") || ce_is(h->items[3]->text,"prepare") || ce_is(h->items[3]->text,"effect")))
            return ce_error(e,h->offset,"hook kind is guard, prepare or effect");
        hook->kind = h->items[3]->text;
        if (!names(ps,h->items[4],"Requires",&hook->requires,e) ||
            !names(ps,h->items[5],"Uses",&hook->uses,e) || !names(ps,h->items[6],"Proves",&hook->proves,e) ||
            !distinct_cname(ps,&cnames,hook->cname,h->offset,e)) return false;
        if (h->len == 8 && !names(ps,h->items[7],"Invalidates",&hook->invalidates,e)) return false;
        for (size_t j = 0; j < hook->requires.len; ++j) add(ps,&p->facts,hook->requires.items[j]);
        for (size_t j = 0; j < hook->proves.len; ++j) add(ps,&p->facts,hook->proves.items[j]);
        for (size_t j = 0; j < hook->invalidates.len; ++j) add(ps,&p->facts,hook->invalidates.items[j]);
    }
    for (size_t i = 0; i < p->nresources; ++i) {
        const CeAtom *r = rs->items[i+1]; Resource *resource = &p->resources[i];
        if (!form(r,"Resource") || (r->len != 5 && r->len != 7))
            return ce_error(e,r->offset,"Resource takes four names and optional AcquireRequires/TransferRequires lists");
        if (!name(r->items[1],&resource->id,e) ||
            !name(r->items[2],&resource->acquire,e) || !name(r->items[3],&resource->release,e) ||
            !name(r->items[4],&resource->transfer,e) ||
            !callable_name(resource->acquire,r->offset,e) || !callable_name(resource->release,r->offset,e) ||
            !callable_name(resource->transfer,r->offset,e)) return false;
        if (r->len == 7) {
            if (!names(ps,r->items[5],"AcquireRequires",&resource->acquire_requires,e) ||
                !names(ps,r->items[6],"TransferRequires",&resource->transfer_requires,e)) return false;
            for (size_t j = 0; j < resource->acquire_requires.len; ++j) add(ps,&p->facts,resource->acquire_requires.items[j]);
            for (size_t j = 0; j < resource->transfer_requires.len; ++j) add(ps,&p->facts,resource->transfer_requires.items[j]);
        }
        for (size_t j = 0; j < i; ++j) if (ce_equal(p->resources[j].id,resource->id))
            return ce_error(e,r->offset,"duplicate resource ID");
        if (!distinct_cname(ps,&cnames,resource->acquire,r->offset,e) ||
            !distinct_cname(ps,&cnames,resource->release,r->offset,e) ||
            !distinct_cname(ps,&cnames,resource->transfer,r->offset,e)) return false;
    }
    if (!shape(entry,"Entry",3,e) || !names(ps,entry->items[1],"Facts",&p->entry_facts,e) ||
        !names(ps,entry->items[2],"Owns",&p->entry_owned,e) || !shape(a->items[6],"Body",2,e)) return false;
    for (size_t i = 0; i < p->entry_facts.len; ++i) add(ps,&p->facts,p->entry_facts.items[i]);
    for (size_t i = 0; i < p->entry_owned.len; ++i) if (resource_index(p,p->entry_owned.items[i]) == SIZE_MAX)
        return ce_error(e,entry->offset,"entry owns an undeclared resource");
    for (size_t i = 0; i < p->nhooks; ++i)
        for (size_t j = 0; j < p->hooks[i].uses.len; ++j)
            if (resource_index(p,p->hooks[i].uses.items[j]) == SIZE_MAX)
                return ce_error(e,hs->items[i+1]->offset,"hook uses an undeclared resource");
    for (size_t i = 7; i < a->len; ++i) {
        const CeAtom *clause = a->items[i];
        if (form(clause,"Checks") && !p->checks) p->checks = clause;
        else if (form(clause,"ExitRequires") && !p->exits) {
            p->exits = clause;
            bool seen[PROTOCOL_OUTCOMES] = {0};
            for (size_t j = 1; j < clause->len; ++j) {
                const CeAtom *exit = clause->items[j];
                if (exit->kind != CE_ATOM_LIST || !exit->len || exit->items[0]->kind != CE_ATOM_SYMBOL)
                    return ce_error(e,exit->offset,"exit requirement names an outcome followed by facts");
                size_t which = 0;
                while (which < COUNT(outcomes) && !ce_is(exit->items[0]->text,outcomes[which])) ++which;
                if (which == COUNT(outcomes)) return ce_error(e,exit->offset,"unknown exit outcome");
                if (seen[which]) return ce_error(e,exit->offset,"duplicate exit outcome");
                seen[which] = true;
                if (!names(ps,exit,outcomes[which],&p->exit_facts[which],e)) return false;
                for (size_t k = 0; k < p->exit_facts[which].len; ++k)
                    add(ps,&p->facts,p->exit_facts[which].items[k]);
            }
        } else return ce_error(e,clause->offset,"unknown or duplicate protocol clause; expected Checks or ExitRequires");
    }
    if (p->checks) {
        const CeAtom *checks = p->checks;
        if (!form(checks,"Checks")) return ce_error(e,checks->offset,"expected (Checks (Fact ID PREDICATE) (Own ID PREDICATE) ...)");
        p->nvalidators = checks->len-1;
        p->validators = ce_alloc(&ps->arena,p->nvalidators*sizeof(*p->validators));
        for (size_t i = 0; i < p->nvalidators; ++i) {
            const CeAtom *v = checks->items[i+1]; Validator *out = &p->validators[i];
            if ((!form(v,"Fact") && !form(v,"Own")) || v->len != 3 ||
                !name(v->items[1],&out->id,e) || !name(v->items[2],&out->cname,e))
                return ce_error(e,v->offset,"validator is (Fact ID PREDICATE) or (Own ID PREDICATE)");
            out->kind = ce_cstr(&ps->arena,form(v,"Fact") ? "fact" : "own");
            if ((form(v,"Fact") ? find(p->facts,out->id) : resource_index(p,out->id)) == SIZE_MAX)
                return ce_error(e,v->offset,"validator refers to an undeclared fact or resource");
            for (size_t j = 0; j < i; ++j)
                if (ce_equal(out->kind,p->validators[j].kind) && ce_equal(out->id,p->validators[j].id))
                    return ce_error(e,v->offset,"duplicate validator for a fact or resource");
            if (!callable_name(out->cname,v->offset,e) || !distinct_cname(ps,&cnames,out->cname,v->offset,e)) return false;
        }
    }
    p->body = a->items[6]->items[1]; return true;
}

static State state_copy(Protocols *ps, const Protocol *p, State s) {
    State t = {.effect=s.effect};
    t.facts = ce_alloc(&ps->arena,p->facts.len*sizeof(bool));
    t.owned = ce_alloc(&ps->arena,p->nresources*sizeof(bool));
    if (p->facts.len) memcpy(t.facts,s.facts,p->facts.len*sizeof(bool));
    if (p->nresources) memcpy(t.owned,s.owned,p->nresources*sizeof(bool));
    return t;
}
static CeAtom *node(Protocols *ps, const char *head, CeText operand, size_t n) {
    CeAtom *a = ce_atom_new(&ps->arena,CE_ATOM_LIST,(CeText){0},n);
    a->items[0] = ce_atom_new(&ps->arena,CE_ATOM_SYMBOL,ce_cstr(&ps->arena,head),0);
    if (n > 1) a->items[1] = ce_atom_new(&ps->arena,CE_ATOM_SYMBOL,operand,0);
    return a;
}
static bool loop_limit(Protocols *ps, Protocol *p, const CeAtom *a, CeAtom **out, CeError *e) {
    if (form(a,"Limit") && a->len == 2) {
        CeText field;
        if (!name(a->items[1],&field,e)) return false;
        add(ps,&p->limits,field);
        *out = node(ps,"Field",field,2); return true;
    }
    if (a->kind != CE_ATOM_SYMBOL || !a->text.len ||
        (a->text.len > 1 && a->text.data[0] == '0'))
        return ce_error(e,a->offset,"Loop bound is a canonical uint64 literal or (Limit FIELD)");
    uint64_t value = 0;
    for (size_t i = 0; i < a->text.len; ++i) {
        unsigned c = a->text.data[i];
        if (c < '0' || c > '9' || value > (UINT64_MAX-(c-'0'))/10)
            return ce_error(e,a->offset,"Loop bound does not fit uint64_t");
        value = value*10+(c-'0');
    }
    *out = node(ps,"Literal",a->text,2); return true;
}
/* Check every branch; metadata facts are monotone and owners are linear. */
static bool check_body(Protocols *ps, Protocol *p, const CeAtom *a, State s,
                       const State *loop, CeAtom **target, CeError *e) {
    if (form(a,"Continue")) {
        if (a->len != 1 || !loop) return ce_error(e,a->offset,"Continue requires an enclosing Loop");
        for (size_t i = 0; i < p->nresources; ++i) if (s.owned[i] != loop->owned[i])
            return ce_error(e,a->offset,"Continue changes the loop's owned resources");
        for (size_t i = 0; i < p->facts.len; ++i) if (loop->facts[i] && !s.facts[i])
            return ce_error(e,a->offset,"Continue loses a loop-entry fact");
        *target = node(ps,"Continue",(CeText){0},1); return true;
    }
    if (form(a,"Loop")) {
        if (a->len != 4) return ce_error(e,a->offset,"Loop takes a bound, body and exhausted branch");
        *target = node(ps,"While",(CeText){0},4);
        if (!loop_limit(ps,p,a->items[1],&(*target)->items[1],e)) return false;
        /* Every iteration may have committed. Even zero-bound exits use this
         * conservative history, so exhaustion cannot become a silent decline. */
        State entry = state_copy(ps,p,s); entry.effect = true;
        return check_body(ps,p,a->items[2],entry,&entry,&(*target)->items[2],e) &&
               check_body(ps,p,a->items[3],entry,loop,&(*target)->items[3],e);
    }
    if (a->kind != CE_ATOM_LIST || a->len < 2 || a->items[1]->kind != CE_ATOM_SYMBOL)
        return ce_error(e,a->offset,"expected a protocol statement");
    CeText id = a->items[1]->text;
    if (form(a,"Return")) {
        if (a->len != 2) return ce_error(e,a->offset,"Return takes one outcome");
        size_t which = 0;
        while (which < COUNT(outcomes) && !ce_is(id,outcomes[which])) ++which;
        if (which == COUNT(outcomes)) return ce_error(e,a->offset,"unknown protocol outcome");
        for (size_t i = 0; i < p->exit_facts[which].len; ++i)
            if (!s.facts[find(p->facts,p->exit_facts[which].items[i])])
                return ce_error(e,a->offset,"return requires an unestablished exit fact");
        for (size_t i = 0; i < p->nresources; ++i) if (s.owned[i])
            return ce_error(e,a->offset,"return leaves resource %.*s owned",(int)p->resources[i].id.len,p->resources[i].id.data);
        if (s.effect && ce_is(id,"declined")) return ce_error(e,a->offset,"cannot decline after an irreversible effect");
        *target = node(ps,"Return",id,2); return true;
    }
    if (form(a,"Call")) {
        size_t h = hook_index(p,id);
        if (a->len != 4 || h == SIZE_MAX) return ce_error(e,a->offset,"Call takes a declared hook and two branches");
        const Hook *hook = &p->hooks[h];
        for (size_t i = 0; i < hook->requires.len; ++i) if (!s.facts[find(p->facts,hook->requires.items[i])])
            return ce_error(e,a->offset,"hook %.*s requires an unestablished fact",(int)id.len,id.data);
        for (size_t i = 0; i < hook->uses.len; ++i) if (!s.owned[resource_index(p,hook->uses.items[i])])
            return ce_error(e,a->offset,"hook %.*s uses an unowned resource",(int)id.len,id.data);
        s.effect |= ce_is(hook->kind,"effect");
        State yes = state_copy(ps,p,s), no = state_copy(ps,p,s);
        for (size_t i = 0; i < hook->invalidates.len; ++i)
            yes.facts[find(p->facts,hook->invalidates.items[i])] =
                no.facts[find(p->facts,hook->invalidates.items[i])] = false;
        for (size_t i = 0; i < hook->proves.len; ++i) yes.facts[find(p->facts,hook->proves.items[i])] = true;
        *target = node(ps,"IfCall",hook->cname,4);
        return check_body(ps,p,a->items[2],yes,loop,&(*target)->items[2],e) &&
               check_body(ps,p,a->items[3],no,loop,&(*target)->items[3],e);
    }
    size_t r = resource_index(p,id);
    if (r == SIZE_MAX) return ce_error(e,a->offset,"unknown statement or undeclared resource");
    const Resource *resource = &p->resources[r];
    if (form(a,"Acquire")) {
        if (a->len != 4 || s.owned[r]) return ce_error(e,a->offset,"Acquire needs an unowned resource and two branches");
        for (size_t i = 0; i < resource->acquire_requires.len; ++i)
            if (!s.facts[find(p->facts,resource->acquire_requires.items[i])])
                return ce_error(e,a->offset,"resource acquisition requires an unestablished fact");
        State yes = state_copy(ps,p,s), no = state_copy(ps,p,s); yes.owned[r] = true;
        *target = node(ps,"IfCall",resource->acquire,4);
        return check_body(ps,p,a->items[2],yes,loop,&(*target)->items[2],e) &&
               check_body(ps,p,a->items[3],no,loop,&(*target)->items[3],e);
    }
    if (form(a,"Release") || form(a,"Transfer")) {
        if (a->len != 3 || !s.owned[r]) return ce_error(e,a->offset,"Release/Transfer needs an owned resource and a continuation");
        if (form(a,"Transfer")) for (size_t i = 0; i < resource->transfer_requires.len; ++i)
            if (!s.facts[find(p->facts,resource->transfer_requires.items[i])])
                return ce_error(e,a->offset,"resource transfer requires an unestablished fact");
        State next = state_copy(ps,p,s); next.owned[r] = false;
        next.effect |= form(a,"Transfer");
        *target = node(ps,"VoidCall",form(a,"Release") ? resource->release : resource->transfer,3);
        return check_body(ps,p,a->items[2],next,loop,&(*target)->items[2],e);
    }
    return ce_error(e,a->offset,"unknown protocol statement");
}

static CeAtom *symbol(Protocols *ps, const char *text) {
    return ce_atom_new(&ps->arena,CE_ATOM_SYMBOL,ce_cstr(&ps->arena,text),0);
}
static CeAtom *validate(Protocols *ps, Protocol *p, const char *kind, CeText id,
                        bool expected, CeAtom *next) {
    for (size_t i = 0; i < p->nvalidators; ++i) {
        const Validator *v = &p->validators[i];
        if (!ce_is(v->kind,kind) || !ce_equal(v->id,id)) continue;
        CeAtom *a = node(ps,"Validate",v->kind,6);
        a->items[2] = ce_atom_new(&ps->arena,CE_ATOM_SYMBOL,id,0);
        a->items[3] = ce_atom_new(&ps->arena,CE_ATOM_SYMBOL,v->cname,0);
        a->items[4] = symbol(ps,expected ? "true" : "false");
        a->items[5] = next; return a;
    }
    p->checks_complete = false; return next;
}
static CeAtom *validate_names(Protocols *ps, Protocol *p, const char *kind, Names ids, CeAtom *next) {
    for (size_t i = ids.len; i > 0; --i) next = validate(ps,p,kind,ids.items[i-1],true,next);
    return next;
}
static CeAtom *reply_key(Protocols *ps, CeText cname, bool reply) {
    CeAtom *key = node(ps,"afterBool",cname,3);
    key->items[2] = symbol(ps,reply ? "true" : "false"); return key;
}
static CeText reason_name(Protocols *ps, const Protocol *p, const CeAtom *key) {
    if (form(key,"entry")) return suffixed(ps,p->name,"_declined_at_entry");
    CeText stem = suffixed(ps,p->name,"_declined_after_");
    CeText call = key->items[1]->text;
    unsigned char *joined = ce_alloc(&ps->arena,stem.len+call.len+1);
    memcpy(joined,stem.data,stem.len); memcpy(joined+stem.len,call.data,call.len);
    stem = (CeText){joined,stem.len+call.len};
    return suffixed(ps,stem,ce_is(key->items[2]->text,"true") ? "_true" : "_false");
}
/* Validation observes state without replaying a guard or preparation hook.
 * The deciding Boolean call is retained through void cleanup for diagnostics. */
static CeAtom *instrument_body(Protocols *ps, Protocol *p, const CeAtom *body, CeAtom *key,
                                State state, const State *loop) {
    if (form(body,"Continue")) {
        CeAtom *next = node(ps,"Continue",(CeText){0},1);
        for (size_t i = p->nresources; i > 0; --i)
            next = validate(ps,p,"own",p->resources[i-1].id,loop->owned[i-1],next);
        for (size_t i = p->facts.len; i > 0; --i) if (loop->facts[i-1])
            next = validate(ps,p,"fact",p->facts.items[i-1],true,next);
        return next;
    }
    if (form(body,"Loop")) {
        CeAtom *next = node(ps,"While",(CeText){0},4);
        const CeAtom *bound = body->items[1];
        next->items[1] = form(bound,"Limit") ? node(ps,"Field",bound->items[1]->text,2) :
                                              node(ps,"Literal",bound->text,2);
        State entry = state_copy(ps,p,state); entry.effect = true;
        next->items[2] = instrument_body(ps,p,body->items[2],key,entry,&entry);
        next->items[3] = instrument_body(ps,p,body->items[3],key,entry,loop);
        return next;
    }
    if (form(body,"Return")) {
        CeAtom *next = node(ps,"Return",body->items[1]->text,2);
        if (ce_is(body->items[1]->text,"declined")) {
            CeAtom *tick = node(ps,"Tick",(CeText){0},3);
            tick->items[1] = key; tick->items[2] = next; next = tick;
            add(ps,&p->reasons,reason_name(ps,p,key));
        }
        for (size_t i = p->nresources; i > 0; --i)
            next = validate(ps,p,"own",p->resources[i-1].id,false,next);
        for (size_t i = 0; i < COUNT(outcomes); ++i)
            if (ce_is(body->items[1]->text,outcomes[i]))
                next = validate_names(ps,p,"fact",p->exit_facts[i],next);
        return next;
    }
    if (form(body,"Call")) {
        const Hook *h = &p->hooks[hook_index(p,body->items[1]->text)];
        State yes = state_copy(ps,p,state), no = state_copy(ps,p,state);
        yes.effect |= ce_is(h->kind,"effect"); no.effect = yes.effect;
        for (size_t i = 0; i < h->invalidates.len; ++i)
            yes.facts[find(p->facts,h->invalidates.items[i])] =
                no.facts[find(p->facts,h->invalidates.items[i])] = false;
        for (size_t i = 0; i < h->proves.len; ++i) yes.facts[find(p->facts,h->proves.items[i])] = true;
        CeAtom *next = node(ps,"IfCall",h->cname,4);
        next->items[2] = instrument_body(ps,p,body->items[2],reply_key(ps,h->cname,true),yes,loop);
        next->items[3] = instrument_body(ps,p,body->items[3],reply_key(ps,h->cname,false),no,loop);
        next = validate_names(ps,p,"own",h->uses,next);
        return validate_names(ps,p,"fact",h->requires,next);
    }
    const Resource *r = &p->resources[resource_index(p,body->items[1]->text)];
    if (form(body,"Acquire")) {
        State yes = state_copy(ps,p,state); yes.owned[resource_index(p,r->id)] = true;
        CeAtom *next = node(ps,"IfCall",r->acquire,4);
        next->items[2] = validate(ps,p,"own",r->id,true,
            instrument_body(ps,p,body->items[2],reply_key(ps,r->acquire,true),yes,loop));
        next->items[3] = validate(ps,p,"own",r->id,false,
            instrument_body(ps,p,body->items[3],reply_key(ps,r->acquire,false),state,loop));
        next = validate(ps,p,"own",r->id,false,next);
        return validate_names(ps,p,"fact",r->acquire_requires,next);
    }
    bool transfer = form(body,"Transfer");
    State after = state_copy(ps,p,state); after.owned[resource_index(p,r->id)] = false;
    after.effect |= transfer;
    CeAtom *next = node(ps,"VoidCall",transfer ? r->transfer : r->release,3);
    next->items[2] = validate(ps,p,"own",r->id,false,instrument_body(ps,p,body->items[2],key,after,loop));
    if (transfer) next = validate_names(ps,p,"fact",r->transfer_requires,next);
    return validate(ps,p,"own",r->id,true,next);
}
static void instrument(Protocols *ps, Protocol *p, State state) {
    CeAtom *entry = ce_atom_new(&ps->arena,CE_ATOM_LIST,(CeText){0},1);
    entry->items[0] = symbol(ps,"entry"); p->checks_complete = true;
    CeAtom *target = instrument_body(ps,p,p->body,entry,state,NULL);
    for (size_t i = p->nresources; i > 0; --i)
        target = validate(ps,p,"own",p->resources[i-1].id,
            find(p->entry_owned,p->resources[i-1].id) != SIZE_MAX,target);
    p->instrumented = validate_names(ps,p,"fact",p->entry_facts,target);
}
static const char *const instrumentation_suffixes[] = {
    "_decline_reason", "_decline_count", "_counters", "_count_decline", "_counts", "_reason",
    "_COUNTED", "_CHECKED", "_COUNT_DECLINE", "_CHECK_FAILURE"
};
static bool load(Protocols *ps, const char *path, const char *theory, CeError *e) {
    ps->catalog.syntax = &ps->syntax;
    if (!ce_grammar_read(&ps->syntax,ce_builtin_TheoryModuleSyntax(),e) || !ce_grammar_finish(&ps->syntax,e)) return false;
    CeModule *module; CePresentation presentation = {0};
    if (!ce_module_load(&ps->catalog,path,&module,e) || !ce_module_check(&ps->catalog,module,e) ||
        !ce_module_theory(&ps->catalog,module,theory,&presentation,e)) return false;
    if (presentation.len || !presentation.nlaws) return ce_error(e,0,"a protocol theory contains equations and no Terms");
    ps->len = presentation.nlaws; ps->items = ce_alloc(&ps->arena,ps->len*sizeof(*ps->items));
    for (size_t i = 0; i < ps->len; ++i) {
        const CeTree *law = presentation.laws[i].tree;
        if (!ce_tree_label(&ps->syntax,law,"EquationImpl")) return ce_error(e,law->offset,"expected a protocol equation");
        CeAtom *lhs = ce_law_atom(&ps->syntax,&ps->lexer,&ps->arena,law->children[0],e);
        CeAtom *rhs = ce_law_atom(&ps->syntax,&ps->lexer,&ps->arena,law->children[1],e); CeText id;
        if (!lhs || !rhs) return false;
        if (lhs->kind != CE_ATOM_LIST || lhs->len != 1 || !name(lhs->items[0],&id,e))
            return ce_error(e,law->offset,"protocol equation left side must be (NAME)");
        for (size_t j = 0; j < i; ++j) if (ce_equal(ps->items[j].name,id)) return ce_error(e,law->offset,"duplicate protocol name");
        Protocol *p = &ps->items[i];
        if (!read_protocol(ps,id,rhs,p,e)) return false;
        State entry = {.facts=ce_alloc(&ps->arena,p->facts.len*sizeof(bool)),
                       .owned=ce_alloc(&ps->arena,p->nresources*sizeof(bool))};
        for (size_t j = 0; j < p->entry_facts.len; ++j) entry.facts[find(p->facts,p->entry_facts.items[j])] = true;
        for (size_t j = 0; j < p->entry_owned.len; ++j) entry.owned[resource_index(p,p->entry_owned.items[j])] = true;
        if (!check_body(ps,p,p->body,entry,NULL,&p->target,e)) return false;
        instrument(ps,p,entry);
    }
    Names exported = {0};
    for (size_t i = 0; i < ps->len; ++i) {
        const Protocol *p = &ps->items[i];
        if (!distinct_cname(ps,&exported,p->name,p->written->offset,e) ||
            !distinct_cname(ps,&exported,guard_name(ps,p->name),p->written->offset,e) ||
            !distinct_cname(ps,&exported,suffixed(ps,p->name,"_outcome"),p->written->offset,e)) return false;
        for (size_t j = 0; j < COUNT(outcomes); ++j)
            if (!distinct_cname(ps,&exported,suffixed(ps,suffixed(ps,p->name,"_"),outcomes[j]),p->written->offset,e)) return false;
        for (size_t j = 0; j < COUNT(instrumentation_suffixes); ++j)
            if (!distinct_cname(ps,&exported,suffixed(ps,p->name,instrumentation_suffixes[j]),p->written->offset,e)) return false;
        for (size_t j = 0; j < p->reasons.len; ++j)
            if (!distinct_cname(ps,&exported,p->reasons.items[j],p->written->offset,e)) return false;
    }
    for (size_t i = 0; i < ps->len; ++i) {
        const Protocol *p = &ps->items[i];
        if (find(exported,p->context) != SIZE_MAX || ce_is(p->name,"ctx") || ce_is(p->context,"ctx") ||
            ce_is(p->name,"cesta_remaining") || ce_is(p->context,"cesta_remaining"))
            return ce_error(e,p->written->offset,"context or protocol name conflicts with an exported declaration or local parameter");
        for (size_t j = 0; j < p->nhooks; ++j) if (find(exported,p->hooks[j].cname) != SIZE_MAX)
            return ce_error(e,p->written->offset,"hook conflicts with an exported declaration");
        for (size_t j = 0; j < p->nresources; ++j) {
            const Resource *r = &p->resources[j];
            if (find(exported,r->acquire) != SIZE_MAX || find(exported,r->release) != SIZE_MAX || find(exported,r->transfer) != SIZE_MAX)
                return ce_error(e,p->written->offset,"resource hook conflicts with an exported declaration");
        }
        for (size_t j = 0; j < p->nvalidators; ++j) if (find(exported,p->validators[j].cname) != SIZE_MAX)
            return ce_error(e,p->written->offset,"validator conflicts with an exported declaration");
    }
    return true;
}

static void write_text(FILE *f, CeText s) { fwrite(s.data,1,s.len,f); }
static void indent(FILE *f, size_t level) { for (size_t i = 0; i < level; ++i) fputs("    ",f); }
static bool linear(const CeAtom *target) {
    return form(target,"Return") || form(target,"Continue") ||
        ((form(target,"VoidCall") || form(target,"Tick")) && linear(target->items[2])) ||
        (form(target,"Validate") && linear(target->items[5]));
}
static void print_c(Protocols *ps, FILE *f, const Protocol *p, const CeAtom *target, size_t level) {
    CeText name = p->name;
    if (form(target,"Validate")) {
        fprintf(f,"#if %.*s_CHECKED\n",(int)name.len,name.data);
        indent(f,level); fputs(ce_is(target->items[4]->text,"true") ? "if (!" : "if (",f);
        write_text(f,target->items[3]->text); fputs("(ctx)) {\n",f);
        indent(f,level+1); write_text(f,name); fputs("_CHECK_FAILURE(ctx, \"",f);
        write_text(f,target->items[1]->text); fputs("\", \"",f); write_text(f,target->items[2]->text);
        fputs("\", ",f); fputs(ce_is(target->items[4]->text,"true") ? "1" : "0",f); fputs(");\n",f);
        indent(f,level+1); fputs("for (;;) {}\n",f); indent(f,level); fputs("}\n#endif\n",f);
        print_c(ps,f,p,target->items[5],level); return;
    }
    indent(f,level);
    if (form(target,"Continue")) {
        fputs("continue;\n",f);
    } else if (form(target,"While")) {
        const CeAtom *bound = target->items[1];
        fputs("{\n",f);
        if (form(bound,"Field")) {
            indent(f,level+1); fputs("_Static_assert(_Generic(&(ctx->",f); write_text(f,bound->items[1]->text);
            fputs("), uint64_t *: 1, const uint64_t *: 1, default: 0), \"protocol limit field must be nonvolatile uint64_t\");\n",f);
        }
        indent(f,level+1); fputs("for (uint64_t cesta_remaining = ",f);
        if (form(bound,"Field")) { fputs("ctx->",f); write_text(f,bound->items[1]->text); }
        else { fputs("UINT64_C(",f); write_text(f,bound->items[1]->text); fputc(')',f); }
        fputs("; cesta_remaining != 0; --cesta_remaining) {\n",f);
        print_c(ps,f,p,target->items[2],level+2);
        indent(f,level+1); fputs("}\n",f); print_c(ps,f,p,target->items[3],level+1);
        indent(f,level); fputs("}\n",f);
    } else if (form(target,"Return")) {
        fputs("return ",f); write_text(f,name); fputc('_',f); write_text(f,target->items[1]->text); fputs(";\n",f);
    } else if (form(target,"Tick")) {
        write_text(f,name); fputs("_COUNT_DECLINE(ctx, ",f); write_text(f,reason_name(ps,p,target->items[1]));
        fputs(");\n",f); print_c(ps,f,p,target->items[2],level);
    } else if (form(target,"VoidCall")) {
        write_text(f,target->items[1]->text); fputs("(ctx);\n",f); print_c(ps,f,p,target->items[2],level);
    } else {
        /* A straight branch always returns. Print it as an early exit so the
         * main route remains readable without changing a call or its order. */
        bool negative = linear(target->items[3]) && !linear(target->items[2]);
        const CeAtom *first = target->items[negative ? 3 : 2];
        const CeAtom *second = target->items[negative ? 2 : 3];
        fputs(negative ? "if (!" : "if (",f);
        write_text(f,target->items[1]->text); fputs("(ctx)) {\n",f);
        print_c(ps,f,p,first,level+1); indent(f,level);
        if (linear(first)) {
            fputs("}\n",f); print_c(ps,f,p,second,level);
        } else {
            fputs("} else {\n",f); print_c(ps,f,p,second,level+1);
            indent(f,level); fputs("}\n",f);
        }
    }
}
static void guard_macro(FILE *f, CeText symbol) {
    fputs(" || \\\n    defined(",f); write_text(f,symbol); fputc(')',f);
}
static void instrumentation_header(const Protocol *p, FILE *f) {
    int n = (int)p->name.len; const unsigned char *s = p->name.data;
    fprintf(f,"\n#ifndef %.*s_CHECKED\n#define %.*s_CHECKED 0\n#endif\n",n,s,n,s);
    fprintf(f,"#ifdef %.*s_COUNT_DECLINE\n#define %.*s_COUNTED 1\n#else\n#define %.*s_COUNTED 0\n#define %.*s_COUNT_DECLINE(ctx, reason) ((void)0)\n#endif\n",n,s,n,s,n,s,n,s);
    fprintf(f,"#if %.*s_CHECKED\n",n,s);
    if (p->checks_complete) {
        fprintf(f,"#ifndef %.*s_CHECK_FAILURE\n#include <stdio.h>\n#include <stdlib.h>\n",n,s);
        fprintf(f,"#define %.*s_CHECK_FAILURE(ctx, kind, id, expected) \\\n    (fprintf(stderr, \"protocol %.*s: %%s %%s must be %%s\\n\", \\\n             kind, id, (expected) ? \"true\" : \"false\"), abort())\n#endif\n",n,s,n,s);
    }
    else fputs("#error \"checked protocol requires validators for every used fact and resource\"\n",f);
    fputs("#endif\n#include <stdint.h>\n",f);
    fprintf(f,"\ntypedef enum %.*s_decline_reason {\n",n,s);
    for (size_t i = 0; i < p->reasons.len; ++i) {
        fputs("    ",f); write_text(f,p->reasons.items[i]); fputs(",\n",f);
    }
    fprintf(f,"    %.*s_decline_count\n} %.*s_decline_reason;\n",n,s,n,s);
    fprintf(f,"\ntypedef struct %.*s_counters {\n    uint64_t declines[%.*s_decline_count ? %.*s_decline_count : 1];\n} %.*s_counters;\n",n,s,n,s,n,s,n,s);
    if (p->reasons.len) {
        fprintf(f,"\n#if %.*s_COUNTED\nstatic inline void\n%.*s_count_decline(%.*s_counters *%.*s_counts, %.*s_decline_reason %.*s_reason)\n{\n",n,s,n,s,n,s,n,s,n,s,n,s);
        fprintf(f,"    if (%.*s_counts && (unsigned)%.*s_reason < (unsigned)%.*s_decline_count &&\n        %.*s_counts->declines[%.*s_reason] != UINT64_MAX)\n        ++%.*s_counts->declines[%.*s_reason];\n",n,s,n,s,n,s,n,s,n,s,n,s,n,s);
        fputs("}\n#endif\n",f);
    }
}
static void header(Protocols *ps, FILE *f) {
    fputs("/* Generated from cesta-protocol/1. Declare the context and native hooks first. */\n",f);
    for (size_t i = 0; i < ps->len; ++i) {
        const Protocol *p = &ps->items[i];
        fprintf(f,"\n#ifdef CESTA_PROTOCOL_%.*s_DEFINED\n#error \"duplicate protocol definition\"\n#endif\n#define CESTA_PROTOCOL_%.*s_DEFINED\n\n",
                (int)p->name.len,p->name.data,(int)p->name.len,p->name.data);
        fputs("#if defined(",f); write_text(f,p->name); fputc(')',f);
        guard_macro(f,p->context); guard_macro(f,ce_cstr(&ps->arena,"ctx"));
        guard_macro(f,ce_cstr(&ps->arena,"cesta_remaining"));
        for (size_t j = 0; j < p->limits.len; ++j) guard_macro(f,p->limits.items[j]);
        guard_macro(f,suffixed(ps,p->name,"_outcome"));
        for (size_t j = 0; j < COUNT(outcomes); ++j)
            guard_macro(f,suffixed(ps,suffixed(ps,p->name,"_"),outcomes[j]));
        for (size_t j = 0; j < p->nhooks; ++j) guard_macro(f,p->hooks[j].cname);
        for (size_t j = 0; j < p->nresources; ++j) {
            guard_macro(f,p->resources[j].acquire); guard_macro(f,p->resources[j].release); guard_macro(f,p->resources[j].transfer);
        }
        for (size_t j = 0; j < p->nvalidators; ++j) guard_macro(f,p->validators[j].cname);
        /* The last three names are the documented configuration macros. */
        for (size_t j = 0; j+3 < COUNT(instrumentation_suffixes); ++j)
            guard_macro(f,suffixed(ps,p->name,instrumentation_suffixes[j]));
        for (size_t j = 0; j < p->reasons.len; ++j) guard_macro(f,p->reasons.items[j]);
        fputs("\n#error \"protocol identifier conflicts with a macro\"\n#endif\n",f);
        instrumentation_header(p,f);
        fprintf(f,"\ntypedef enum %.*s_outcome {\n",(int)p->name.len,p->name.data);
        for (size_t j = 0; j < COUNT(outcomes); ++j)
            fprintf(f,"    %.*s_%s%s\n",(int)p->name.len,p->name.data,outcomes[j],j+1 < COUNT(outcomes) ? "," : "");
        fprintf(f,"} %.*s_outcome;\n\nstatic inline %.*s_outcome\n%.*s(%.*s *ctx)\n{\n    (void)ctx;\n",
                (int)p->name.len,p->name.data,(int)p->name.len,p->name.data,
                (int)p->name.len,p->name.data,(int)p->context.len,p->context.data);
        print_c(ps,f,p,p->instrumented,1); fputs("}\n",f);
    }
}
static void plans(const Protocols *ps, FILE *f) {
    for (size_t i = 0; i < ps->len; ++i) {
        const Protocol *p = &ps->items[i];
        fprintf(f,"(cesta-protocol-plan/1 %.*s",(int)p->name.len,p->name.data);
        for (size_t j = 2; j < 7; ++j) { fputc(' ',f); ce_atom_write(p->written->items[j],f); }
        fputs(" (Target ",f); ce_atom_write(p->target,f); fputs(") (Instrumentation ",f);
        if (p->checks) ce_atom_write(p->checks,f);
        else fputs("(Checks)",f);
        if (p->checks_complete) { fputs(" (Target ",f); ce_atom_write(p->instrumented,f); fputc(')',f); }
        else fputs(" (Unavailable)",f);
        fputs(")",f);
        if (p->exits) { fputc(' ',f); ce_atom_write(p->exits,f); }
        fputs(")\n",f);
    }
}
bool ce_protocol_expand(CeArena *arena, const char *path, const char *theory,
                        CeText *text, CeText **dependencies, size_t *ndependencies,
                        FILE *plan, CeError *error) {
    Protocols ps = {0};
    bool ok = load(&ps,path,theory,error);
    char *buffer = NULL; size_t length = 0;
    if (ok) {
        FILE *file = open_memstream(&buffer,&length);
        if (!file) ok = ce_error(error,0,"cannot allocate protocol header");
        else { header(&ps,file); if (fclose(file)) ok = ce_error(error,0,"cannot finish protocol header"); }
    }
    if (ok) {
        *text = ce_text(arena,buffer,length);
        *ndependencies = ps.catalog.len;
        *dependencies = ce_alloc(arena,ps.catalog.len*sizeof(**dependencies));
        for (size_t i = 0; i < ps.catalog.len; ++i) {
            CeText name = ce_module_path(ps.catalog.modules[i]);
            (*dependencies)[i] = ce_text(arena,name.data,name.len);
        }
        if (plan) { plans(&ps,plan); if (ferror(plan)) ok = ce_error(error,0,"cannot write protocol plan"); }
    }
    free(buffer); ce_catalog_free(&ps.catalog); ce_grammar_free(&ps.syntax);
    ce_lazy_lexer_free(&ps.lexer); ce_arena_free(&ps.arena);
    return ok;
}
static void make_path(FILE *f, CeText path) {
    for (size_t i = 0; i < path.len; ++i) {
        unsigned char c = path.data[i];
        if (c == '$') fputc('$',f);
        else if (c == ' ' || c == '\t' || c == '#' || c == ':' || c == '\\') fputc('\\',f);
        fputc(c,f);
    }
}
static bool output_safe(const Protocols *ps, const char *path, CeError *e) {
    for (size_t i = 0; i < ps->catalog.len; ++i) {
        CeText input = ce_module_path(ps->catalog.modules[i]);
        if (ce_same_file(path,(const char *)input.data)) return ce_error(e,0,"output would overwrite a source module");
    }
    return true;
}
static bool write_file(const char *path, const char *text, size_t len, CeError *e) {
    FILE *f = path ? fopen(path,"wb") : stdout;
    if (!f) return ce_error(e,0,"cannot open protocol output %s",path);
    bool ok = fwrite(text,1,len,f) == len;
    if (path) ok = fclose(f) == 0 && ok;
    else ok = fflush(f) == 0 && ok;
    return ok || ce_error(e,0,"cannot write protocol output");
}
static bool verify_file(Protocols *ps, const char *path, const char *text, size_t len, CeError *e) {
    CeText file;
    if (!ce_read_file(&ps->arena,path,&file,e)) return false;
    return (file.len == len && !memcmp(file.data,text,len)) || ce_error(e,0,"recorded output differs from the checked protocol: %s",path);
}

int ce_protocol_command(int argc, char **argv) {
    const char *usage = "usage: cesta protocol check|plan|export|verify MODULE THEORY [-o HEADER] [--plan FILE] [--depfile FILE]\n";
    if (argc < 5) { fputs(usage,stderr); return 1; }
    bool checking = !strcmp(argv[2],"check"), plan = !strcmp(argv[2],"plan"),
         exporting = !strcmp(argv[2],"export"), verify = !strcmp(argv[2],"verify");
    if (!checking && !plan && !exporting && !verify) { fputs(usage,stderr); return 1; }
    const char *out = NULL, *record = NULL, *dep = NULL;
    for (int i = 5; i < argc; ++i) {
        const char **slot = !strcmp(argv[i],"-o") ? &out : !strcmp(argv[i],"--plan") ? &record :
                            !strcmp(argv[i],"--depfile") ? &dep : NULL;
        if (!slot || *slot || i+1 == argc) { fputs(usage,stderr); return 1; }
        *slot = argv[++i];
    }
    if ((checking && (out || record || dep)) || (plan && (record || dep)) ||
        (dep && (!out || !exporting)) || (verify && !out)) { fputs(usage,stderr); return 1; }
    Protocols ps = {0}; CeError error = {0};
    bool ok = load(&ps,argv[3],argv[4],&error);
    const char *paths[] = {out,record,dep};
    for (size_t i = 0; ok && i < COUNT(paths); ++i) if (paths[i]) {
        ok = output_safe(&ps,paths[i],&error);
        for (size_t j = 0; ok && j < i; ++j) if (paths[j] && ce_same_file(paths[i],paths[j]))
            ok = ce_error(&error,0,"protocol outputs must have distinct paths");
    }
    char *c = NULL, *p = NULL; size_t clen = 0, plen = 0;
    if (ok && checking) printf("protocols: %zu checked\n",ps.len);
    if (ok && !checking) {
        FILE *cf = open_memstream(&c,&clen), *pf = open_memstream(&p,&plen);
        if (!cf || !pf) ok = ce_error(&error,0,"cannot allocate protocol output");
        if (cf) { if (ok) header(&ps,cf); if (fclose(cf)) ok = ce_error(&error,0,"cannot finish protocol header"); }
        if (pf) { if (ok) plans(&ps,pf); if (fclose(pf)) ok = ce_error(&error,0,"cannot finish protocol plans"); }
        if (ok && verify) {
            ok = verify_file(&ps,out,c,clen,&error);
            if (ok && record) ok = verify_file(&ps,record,p,plen,&error);
        } else if (ok) {
            ok = write_file(out,plan ? p : c,plan ? plen : clen,&error);
            if (ok && record) ok = write_file(record,p,plen,&error);
            if (ok && dep) {
                FILE *f = fopen(dep,"wb");
                if (!f) ok = ce_error(&error,0,"cannot open dependency file");
                else {
                    make_path(f,ce_cstr(&ps.arena,out));
                    if (record) { fputc(' ',f); make_path(f,ce_cstr(&ps.arena,record)); }
                    fputc(':',f);
                    for (size_t i = 0; i < ps.catalog.len; ++i) {
                        fputc(' ',f); make_path(f,ce_module_path(ps.catalog.modules[i]));
                    }
                    fputc('\n',f); bool good = !ferror(f);
                    if (fclose(f) || !good) ok = ce_error(&error,0,"cannot write dependency file");
                }
            }
        }
    }
    if (!ok) fprintf(stderr,"%s: byte %zu: %s\n",argv[3],error.offset,error.message);
    free(c); free(p); ce_catalog_free(&ps.catalog); ce_grammar_free(&ps.syntax);
    ce_lazy_lexer_free(&ps.lexer); ce_arena_free(&ps.arena);
    return ok ? 0 : 1;
}

#include "layout.h"

#include <stdlib.h>
#include <string.h>

/* ---- Layouts ----------------------------------------------------------------
   A layout theory declares each layout as an equation:

     (FrameIdentity) == (Layout "cesta-layout/1" (Word 32) (Constants ...)
                           (Fields (Field handle "value") ...)
                           (Case (Prefix 1 0) (Place handle (Bits 0 20) ...) ...)
                           ... (Location "immutable"))

   Reading checks what the Lean admission decides again for the normalized
   form, and refuses what it cannot see: names, kinds, aliases, constraints
   and write disciplines. */

CeText ce_builtin_TheoryModuleSyntax(void);

typedef struct { CeText field; uint64_t shift, width, offset, lo, hi; } Place;
typedef struct { bool reserved; uint64_t prefix, depth; Place *places; size_t offset; } Case;
typedef struct { CeText name, kind; bool after_publication; } Field;
typedef struct { CeText name, field, condition; } Alias;
typedef struct { CeText field, reason; uint64_t at_most; } Requirement;
typedef struct { int child[2]; int leaf; } Node;
typedef struct { CeText name; uint64_t value; const CeAtom *expr; } Constant;
typedef struct {
    CeText name;
    uint64_t width;
    Field *fields; size_t nfields;
    Case *cases; size_t ncases;
    Alias *aliases; size_t naliases;
    Requirement *requires; size_t nrequires;
    CeText location;
    Node *trie; size_t ntrie;
    CeText from;    /* the earlier version its words migrate from */
    int key;        /* the field whose ranges choose the case, or -1 */
    Constant *constants; size_t nconstants;
} Layout;

struct CeLayouts {
    CeArena arena;
    CeGrammar syntax;
    CeLazyLexer lexer;
    CeCatalog catalog;
    Layout *items; size_t len;
};

static const char *const kinds[] = {"value","any-child","all-children","fact","cache","occurrence","reference"};
static const char *const disciplines[] = {"immutable","single-owner","construction","atomic"};

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
static bool name_of(const CeAtom *a, CeText *out, CeError *error) {
    if (a->kind != CE_ATOM_SYMBOL || !identifier(a->text))
        return ce_error(error,a->offset,"layout name requires a C identifier");
    *out = a->text; return true;
}
static bool text_of(const CeAtom *a, CeText *out, CeError *error) {
    if (a->kind != CE_ATOM_TEXT) return ce_error(error,a->offset,"layout word requires a string");
    *out = a->text; return true;
}
/* Accessor names a field name would collide with. */
static const char *const accessors[] = {"valid","admits","encode","pack","derive_init","derive_child","derive_finish",
                                        "value_copy","store","relocate"};
static bool one_of(CeText t, const char *const *names, size_t n) {
    for (size_t i = 0; i < n; ++i) if (ce_is(t,names[i])) return true;
    return false;
}
static uint64_t mask(uint64_t width) { return width >= 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1; }

/* Constant expressions: decimal integers, constants, (Hex "…"), (Pow2 e),
   (Sum e …), (Times e …), (Minus a b), all exact in 64 bits. */
static bool evaluate(const CeAtom *a, const Constant *constants, size_t n, uint64_t *out, CeError *error) {
    if (a->kind == CE_ATOM_SYMBOL && a->text.len && a->text.data[0] >= '0' && a->text.data[0] <= '9') {
        uint64_t v = 0;
        for (size_t i = 0; i < a->text.len; ++i) {
            unsigned d = a->text.data[i] - '0';
            if (d > 9 || __builtin_mul_overflow(v,10,&v) || __builtin_add_overflow(v,d,&v))
                return ce_error(error,a->offset,"layout integer does not fit 64 bits");
        }
        *out = v; return true;
    }
    if (a->kind == CE_ATOM_SYMBOL) {
        for (size_t i = 0; i < n; ++i) if (ce_equal(constants[i].name,a->text)) { *out = constants[i].value; return true; }
        return ce_error(error,a->offset,"unknown layout constant %.*s",(int)a->text.len,a->text.data);
    }
    if (form(a,"Hex") && a->len == 2 && a->items[1]->kind == CE_ATOM_TEXT && a->items[1]->text.len) {
        uint64_t v = 0; CeText t = a->items[1]->text;
        for (size_t i = 0; i < t.len; ++i) {
            unsigned c = t.data[i], d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                                        c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
            if (d > 15 || v >> 60) return ce_error(error,a->offset,"layout hexadecimal constant is malformed or too large");
            v = v << 4 | d;
        }
        *out = v; return true;
    }
    if (form(a,"Pow2") && a->len == 2) {
        uint64_t e;
        if (!evaluate(a->items[1],constants,n,&e,error)) return false;
        if (e > 63) return ce_error(error,a->offset,"layout power of two does not fit 64 bits");
        *out = UINT64_C(1) << e; return true;
    }
    if ((form(a,"Sum") || form(a,"Times")) && a->len >= 2) {
        bool sum = form(a,"Sum"); uint64_t v = sum ? 0 : 1;
        for (size_t i = 1; i < a->len; ++i) {
            uint64_t x;
            if (!evaluate(a->items[i],constants,n,&x,error)) return false;
            if (sum ? __builtin_add_overflow(v,x,&v) : __builtin_mul_overflow(v,x,&v))
                return ce_error(error,a->offset,"layout constant expression overflows 64 bits");
        }
        *out = v; return true;
    }
    if (form(a,"Minus") && a->len == 3) {
        uint64_t x, y;
        if (!evaluate(a->items[1],constants,n,&x,error) || !evaluate(a->items[2],constants,n,&y,error)) return false;
        if (y > x) return ce_error(error,a->offset,"layout constant expression is negative");
        *out = x - y; return true;
    }
    return ce_error(error,a->offset,"layout constant requires an integer, a constant, Hex, Pow2, Sum, Times or Minus");
}

static size_t field_index(const Layout *l, CeText name) {
    for (size_t i = 0; i < l->nfields; ++i) if (ce_equal(l->fields[i].name,name)) return i;
    return SIZE_MAX;
}

/* (Place FIELD (Bits SHIFT WIDTH) [(Offset E)] [(Range LO HI)]) */
static bool read_place(CeLayouts *ls, Layout *l, Case *c, const CeAtom *a, uint64_t room,
                       const Constant *k, size_t nk, CeError *error) {
    CeText name;
    if (!form(a,"Place") || a->len < 3 || a->len > 5 || !name_of(a->items[1],&name,error))
        return ce_error(error,a->offset,"layout case entry requires (Place FIELD (Bits SHIFT WIDTH) [(Offset E)] [(Range LO HI)])");
    size_t f = field_index(l,name);
    if (f == SIZE_MAX) return ce_error(error,a->offset,"layout case places undeclared field %.*s",(int)name.len,name.data);
    Place *p = &c->places[f];
    if (p->field.len) return ce_error(error,a->offset,"layout case places field %.*s twice",(int)name.len,name.data);
    const CeAtom *bits = a->items[2];
    if (!form(bits,"Bits") || bits->len != 3 || !evaluate(bits->items[1],k,nk,&p->shift,error) ||
        !evaluate(bits->items[2],k,nk,&p->width,error))
        return error->message[0] ? false : ce_error(error,bits->offset,"layout place requires (Bits SHIFT WIDTH)");
    if (p->width > room || p->shift > room - p->width)
        return ce_error(error,bits->offset,"field %.*s lies outside the %llu bits below the case prefix",
                        (int)name.len,name.data,(unsigned long long)room);
    bool ranged = false;
    for (size_t i = 3; i < a->len; ++i) {
        const CeAtom *x = a->items[i];
        if (form(x,"Offset") && x->len == 2) { if (!evaluate(x->items[1],k,nk,&p->offset,error)) return false; }
        else if (form(x,"Range") && x->len == 3) {
            if (!evaluate(x->items[1],k,nk,&p->lo,error) || !evaluate(x->items[2],k,nk,&p->hi,error)) return false;
            ranged = true;
        } else return ce_error(error,x->offset,"layout place accepts (Offset E) and (Range LO HI)");
    }
    if (__builtin_add_overflow(p->offset,mask(p->width),&(uint64_t){0}))
        return ce_error(error,a->offset,"field %.*s: offset plus slot range overflows 64 bits",(int)name.len,name.data);
    if (!ranged) { p->lo = p->offset; p->hi = p->offset + mask(p->width); }
    if (!c->reserved) {
        if (p->lo > p->hi) return ce_error(error,a->offset,"field %.*s admits no value",(int)name.len,name.data);
        if (p->lo < p->offset || p->hi - p->offset > mask(p->width))
            return ce_error(error,a->offset,"field %.*s: range [%llu, %llu] does not fit offset %llu and %llu bits",
                            (int)name.len,name.data,(unsigned long long)p->lo,(unsigned long long)p->hi,
                            (unsigned long long)p->offset,(unsigned long long)p->width);
    }
    p->field = ce_text(&ls->arena,name.data,name.len);
    return true;
}

/* Insert a prefix into the trie; a prefix of another prefix is refused. */
static bool insert(Layout *l, size_t index, CeError *error) {
    const Case *c = &l->cases[index];
    if (!l->ntrie) { l->trie = ce_resize(l->trie,1,sizeof(*l->trie)); l->trie[0] = (Node){{-1,-1},-1}; l->ntrie = 1; }
    int at = 0;
    for (uint64_t d = c->depth; d-- > 0;) {
        if (l->trie[at].leaf >= 0)
            return ce_error(error,c->offset,"case prefix extends the prefix of an earlier case");
        int bit = (int)(c->prefix >> d & 1);
        if (l->trie[at].child[bit] < 0) {
            l->trie = ce_resize(l->trie,l->ntrie+1,sizeof(*l->trie));
            l->trie[l->ntrie] = (Node){{-1,-1},-1};
            l->trie[at].child[bit] = (int)l->ntrie++;
        }
        at = l->trie[at].child[bit];
    }
    if (l->trie[at].leaf >= 0 || l->trie[at].child[0] >= 0 || l->trie[at].child[1] >= 0)
        return ce_error(error,c->offset,"case prefix is shared with or begins another case's prefix");
    l->trie[at].leaf = (int)index;
    return true;
}
static bool complete(const Layout *l, int at, uint64_t prefix, uint64_t depth, CeError *error, size_t offset) {
    const Node *n = &l->trie[at];
    if (n->leaf >= 0) return true;
    for (int b = 0; b < 2; ++b) {
        if (n->child[b] < 0)
            return ce_error(error,offset,"no case covers words whose top %llu bits are %llu; declare it Reserved",
                            (unsigned long long)depth+1,(unsigned long long)(prefix << 1 | (uint64_t)b));
        if (!complete(l,n->child[b],prefix << 1 | (uint64_t)b,depth+1,error,offset)) return false;
    }
    return true;
}

static bool disjoint(const Place *a, const Place *b) {
    return a->shift + a->width <= b->shift || b->shift + b->width <= a->shift;
}
static bool bounds_apart(const Case *a, const Case *b, size_t n) {
    if (a->reserved || b->reserved) return true;
    for (size_t i = 0; i < n; ++i) if (a->places[i].hi < b->places[i].lo || b->places[i].hi < a->places[i].lo) return true;
    return false;
}
static bool ranges_apart(const Case *a, const Case *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const Place *p = &a->places[i], *q = &b->places[i];
        if (p->offset + mask(p->width) < q->offset || q->offset + mask(q->width) < p->offset) return true;
    }
    return false;
}

static bool read_case(CeLayouts *ls, Layout *l, const CeAtom *a, const Constant *k, size_t nk, CeError *error) {
    Case c = {.reserved = form(a,"Reserved"), .offset = a->offset};
    c.places = ce_alloc(&ls->arena,l->nfields*sizeof(*c.places));
    memset(c.places,0,l->nfields*sizeof(*c.places));
    size_t i = 1;
    if (a->len > 1 && form(a->items[1],"Prefix")) {
        const CeAtom *p = a->items[1];
        if (p->len != 3 || !evaluate(p->items[1],k,nk,&c.depth,error) || !evaluate(p->items[2],k,nk,&c.prefix,error))
            return error->message[0] ? false : ce_error(error,p->offset,"case prefix requires (Prefix LENGTH VALUE)");
        if (c.depth > l->width || (c.depth < 64 && c.prefix >> c.depth))
            return ce_error(error,p->offset,"case prefix value does not fit its length within the word");
        i = 2;
    }
    for (; i < a->len; ++i)
        if (!read_place(ls,l,&c,a->items[i],l->width - c.depth,k,nk,error)) return false;
    for (size_t f = 0; f < l->nfields; ++f)
        if (!c.places[f].field.len)
            return ce_error(error,a->offset,"case does not place field %.*s",(int)l->fields[f].name.len,l->fields[f].name.data);
    uint64_t room = l->width - c.depth;
    for (size_t f = 0; f < l->nfields; ++f)
        for (size_t g = f + 1; g < l->nfields; ++g)
            if (!disjoint(&c.places[f],&c.places[g]))
                return ce_error(error,a->offset,"fields %.*s and %.*s overlap",(int)c.places[f].field.len,
                                c.places[f].field.data,(int)c.places[g].field.len,c.places[g].field.data);
    for (uint64_t bit = 0; bit < room; ++bit) {
        bool held = false;
        for (size_t f = 0; f < l->nfields && !held; ++f)
            held = c.places[f].shift <= bit && bit < c.places[f].shift + c.places[f].width;
        if (!held)
            return ce_error(error,a->offset,"bit %llu below the case prefix is in no field; declare a field for it",
                            (unsigned long long)bit);
    }
    l->cases = ce_resize(l->cases,l->ncases+1,sizeof(*l->cases)); l->cases[l->ncases++] = c;
    return true;
}

static bool check_key(const Layout *l, size_t offset, CeError *error);

/* A newer version must read the same fields and admit, case by case, every
   value the earlier version admits, so every old word has a new word. */
static bool check_migration(CeLayouts *ls, const Layout *l, size_t offset, CeError *error) {
    const Layout *old = NULL;
    for (size_t i = 0; i < ls->len; ++i) if (ce_equal(ls->items[i].name,l->from)) old = &ls->items[i];
    if (!old) return ce_error(error,offset,"earlier version %.*s is not declared before this layout",
                              (int)l->from.len,l->from.data);
    if (old->nfields != l->nfields) return ce_error(error,offset,"a newer version reads the same fields");
    for (size_t f = 0; f < l->nfields; ++f)
        if (!ce_equal(old->fields[f].name,l->fields[f].name))
            return ce_error(error,offset,"a newer version reads the same fields, in the same order");
    for (size_t c = 0; c < old->ncases; ++c) {
        const Case *a = &old->cases[c];
        if (a->reserved) continue;
        bool inside = false;
        for (size_t d = 0; d < l->ncases && !inside; ++d) {
            const Case *b = &l->cases[d];
            inside = !b->reserved;
            for (size_t f = 0; f < l->nfields && inside; ++f)
                inside = b->places[f].lo <= a->places[f].lo && a->places[f].hi <= b->places[f].hi;
        }
        if (!inside)
            return ce_error(error,a->offset,"no case of %.*s admits every value this case of %.*s admits",
                            (int)l->name.len,l->name.data,(int)old->name.len,old->name.data);
    }
    return true;
}

static bool read_layout(CeLayouts *ls, CeText name, const CeAtom *a, CeError *error) {
    if (!form(a,"Layout") || a->len < 2 || a->items[1]->kind != CE_ATOM_TEXT || !ce_is(a->items[1]->text,"cesta-layout/1"))
        return ce_error(error,a->offset,"layout equation requires (Layout \"cesta-layout/1\" ...)");
    Layout l = {.name = ce_text(&ls->arena,name.data,name.len), .key = -1};
    Constant *k = NULL; size_t nk = 0; bool word = false, fields = false; bool ok = true;
    CeText key = {0}; size_t key_offset = 0;
    for (size_t i = 2; i < a->len && ok; ++i) {
        const CeAtom *x = a->items[i];
        if (form(x,"Word") && x->len == 2) {
            ok = !word && evaluate(x->items[1],k,nk,&l.width,error);
            if (ok && (l.width < 1 || l.width > 64)) ok = ce_error(error,x->offset,"layout word has 1 to 64 bits");
            if (word) ok = ce_error(error,x->offset,"layout declares its word twice");
            word = true;
        } else if (form(x,"Constants")) {
            for (size_t j = 1; j < x->len && ok; ++j) {
                const CeAtom *c = x->items[j]; Constant v = {0};
                if (!form(c,"Const") || c->len != 3 || !name_of(c->items[1],&v.name,error)) {
                    ok = error->message[0] ? false : ce_error(error,c->offset,"layout constant requires (Const NAME E)"); break;
                }
                for (size_t m = 0; m < nk; ++m) if (ce_equal(k[m].name,v.name)) ok = ce_error(error,c->offset,"layout constant declared twice");
                v.expr = c->items[2];
                if (ok && (ok = evaluate(c->items[2],k,nk,&v.value,error))) { k = ce_resize(k,nk+1,sizeof(*k)); k[nk++] = v; }
            }
        } else if (form(x,"Fields")) {
            if (fields) { ok = ce_error(error,x->offset,"layout declares its fields twice"); break; }
            fields = true;
            for (size_t j = 1; j < x->len && ok; ++j) {
                const CeAtom *f = x->items[j]; Field v = {0};
                if (!form(f,"Field") || f->len < 3 || f->len > 4 || !name_of(f->items[1],&v.name,error) ||
                    !text_of(f->items[2],&v.kind,error)) {
                    ok = error->message[0] ? false : ce_error(error,f->offset,"layout field requires (Field NAME KIND [(Written WHEN)])");
                    break;
                }
                if (!one_of(v.kind,kinds,sizeof(kinds)/sizeof(*kinds)))
                    ok = ce_error(error,f->items[2]->offset,"unknown field kind %.*s",(int)v.kind.len,v.kind.data);
                if (ok && f->len == 4) {
                    CeText when;
                    if (!form(f->items[3],"Written") || f->items[3]->len != 2 || !text_of(f->items[3]->items[1],&when,error) ||
                        !(ce_is(when,"construction") || ce_is(when,"after-publication")))
                        ok = ce_error(error,f->items[3]->offset,"field write time is (Written \"construction\") or (Written \"after-publication\")");
                    else v.after_publication = ce_is(when,"after-publication");
                }
                if (ok && (one_of(v.name,accessors,sizeof(accessors)/sizeof(*accessors)) ||
                           (v.name.len > 4 && !memcmp(v.name.data,"set_",4))))
                    ok = ce_error(error,f->offset,"field name %.*s would name another accessor",(int)v.name.len,v.name.data);
                if (ok && field_index(&l,v.name) != SIZE_MAX) ok = ce_error(error,f->offset,"layout field declared twice");
                if (ok) { l.fields = ce_resize(l.fields,l.nfields+1,sizeof(*l.fields)); l.fields[l.nfields++] = v; }
            }
        } else if (form(x,"Case") || form(x,"Reserved")) {
            if (!word || !fields) ok = ce_error(error,x->offset,"layout cases follow the word and the fields");
            else ok = read_case(ls,&l,x,k,nk,error);
        } else if (form(x,"Alias") && x->len == 4) {
            Alias v;
            ok = name_of(x->items[1],&v.name,error) && name_of(x->items[2],&v.field,error) && text_of(x->items[3],&v.condition,error);
            if (ok && field_index(&l,v.field) == SIZE_MAX) ok = ce_error(error,x->offset,"alias names an undeclared field");
            if (ok && field_index(&l,v.name) != SIZE_MAX) ok = ce_error(error,x->offset,"alias reuses a field name");
            if (ok && !v.condition.len) ok = ce_error(error,x->offset,"alias requires the condition under which it splits");
            if (ok) { l.aliases = ce_resize(l.aliases,l.naliases+1,sizeof(*l.aliases)); l.aliases[l.naliases++] = v; }
        } else if (form(x,"Requires") && x->len == 3) {
            const CeAtom *r = x->items[1]; Requirement v;
            ok = form(r,"AtMost") && r->len == 3 && name_of(r->items[1],&v.field,error) &&
                evaluate(r->items[2],k,nk,&v.at_most,error) && text_of(x->items[2],&v.reason,error);
            if (!ok && !error->message[0]) ok = ce_error(error,x->offset,"layout requirement is (Requires (AtMost FIELD BITS) REASON)");
            if (ok && field_index(&l,v.field) == SIZE_MAX) ok = ce_error(error,x->offset,"requirement names an undeclared field");
            if (ok) { l.requires = ce_resize(l.requires,l.nrequires+1,sizeof(*l.requires)); l.requires[l.nrequires++] = v; }
        } else if (form(x,"Location") && x->len == 2) {
            ok = !l.location.len && text_of(x->items[1],&l.location,error);
            if (ok && !one_of(l.location,disciplines,sizeof(disciplines)/sizeof(*disciplines)))
                ok = ce_error(error,x->offset,"unknown write discipline %.*s",(int)l.location.len,l.location.data);
            if (!ok && !error->message[0]) ok = ce_error(error,x->offset,"layout declares its location twice");
        } else if (form(x,"From") && x->len == 2) {
            ok = !l.from.len && name_of(x->items[1],&l.from,error);
            if (!ok && !error->message[0]) ok = ce_error(error,x->offset,"layout declares two earlier versions");
        } else if (form(x,"Key") && x->len == 2) {
            ok = !key.len && name_of(x->items[1],&key,error);
            if (!ok && !error->message[0]) ok = ce_error(error,x->offset,"layout declares two keys");
            key_offset = x->offset;
        } else ok = ce_error(error,x->offset,"unknown layout section");
    }
    l.constants = k; l.nconstants = nk;
    if (ok && !l.ncases) ok = ce_error(error,a->offset,"layout declares no case");
    if (ok && !l.location.len) ok = ce_error(error,a->offset,"layout requires a (Location DISCIPLINE)");
    for (size_t i = 0; i < l.ncases && ok; ++i) ok = insert(&l,i,error);
    if (ok) ok = complete(&l,0,0,0,error,a->offset);
    for (size_t i = 0; i < l.ncases && ok; ++i)
        for (size_t j = i + 1; j < l.ncases && ok; ++j) {
            if (!bounds_apart(&l.cases[i],&l.cases[j],l.nfields))
                ok = ce_error(error,l.cases[j].offset,"this case and an earlier one admit a common value");
            else if (!ranges_apart(&l.cases[i],&l.cases[j],l.nfields))
                ok = ce_error(error,l.cases[j].offset,"this case and an earlier one read some words as the same value");
        }
    if (ok && key.len) {
        size_t j = field_index(&l,key);
        if (j == SIZE_MAX) ok = ce_error(error,key_offset,"key names an undeclared field");
        else { l.key = (int)j; ok = check_key(&l,key_offset,error); }
    }
    for (size_t i = 0; i < l.nrequires && ok; ++i) {
        size_t f = field_index(&l,l.requires[i].field);
        for (size_t c = 0; c < l.ncases && ok; ++c)
            if (l.cases[c].places[f].width > l.requires[i].at_most)
                ok = ce_error(error,l.cases[c].offset,"field %.*s is wider than its requirement allows: %.*s",
                              (int)l.requires[i].field.len,l.requires[i].field.data,
                              (int)l.requires[i].reason.len,l.requires[i].reason.data);
    }
    /* A prefix-coded word is written whole from a value, never derived from
       children's words, so its fields carry values, facts or references. */
    if (ok && (l.ncases > 1 || l.cases[0].depth))
        for (size_t f = 0; f < l.nfields && ok; ++f)
            if (!(ce_is(l.fields[f].kind,"value") || ce_is(l.fields[f].kind,"fact") || ce_is(l.fields[f].kind,"reference")))
                ok = ce_error(error,a->offset,"field %.*s: kind %.*s needs a word with a single case",
                              (int)l.fields[f].name.len,l.fields[f].name.data,(int)l.fields[f].kind.len,l.fields[f].kind.data);
    for (size_t f = 0; f < l.nfields && ok; ++f)
        if (l.fields[f].after_publication && !ce_is(l.location,"atomic"))
            ok = ce_error(error,a->offset,"field %.*s is written after publication in a word that is not atomic; "
                          "make the word atomic or give the field its own word",
                          (int)l.fields[f].name.len,l.fields[f].name.data);
    for (size_t i = 0; i < ls->len && ok; ++i)
        if (ce_equal(ls->items[i].name,l.name)) ok = ce_error(error,a->offset,"layout declared twice");
    if (ok && l.from.len) ok = check_migration(ls,&l,a->offset,error);
    if (!ok) {
        free(l.fields); free(l.cases); free(l.aliases); free(l.requires); free(l.trie); free(l.constants);
        return false;
    }
    ls->items = ce_resize(ls->items,ls->len+1,sizeof(*ls->items)); ls->items[ls->len++] = l;
    return true;
}

CeLayouts *ce_layouts_new(CeError *error) {
    CeLayouts *ls = calloc(1,sizeof(*ls));
    if (!ls) { ce_error(error,0,"cannot allocate layouts"); return NULL; }
    ls->catalog.syntax = &ls->syntax;
    if (!ce_grammar_read(&ls->syntax,ce_builtin_TheoryModuleSyntax(),error) || !ce_grammar_finish(&ls->syntax,error)) {
        ce_layouts_free(ls); return NULL;
    }
    return ls;
}
static bool export_names(CeLayouts *ls, CeError *error);
void ce_layouts_free(CeLayouts *ls) {
    if (!ls) return;
    for (size_t i = 0; i < ls->len; ++i) {
        Layout *l = &ls->items[i];
        free(l->fields); free(l->cases); free(l->aliases); free(l->requires); free(l->trie); free(l->constants);
    }
    free(ls->items); ce_catalog_free(&ls->catalog); ce_grammar_free(&ls->syntax);
    ce_lazy_lexer_free(&ls->lexer); ce_arena_free(&ls->arena); free(ls);
}
bool ce_layouts_load(CeLayouts *ls, const char *path, const char *theory, CeError *error) {
    CeModule *module; CePresentation presentation = {0};
    if (!ce_module_load(&ls->catalog,path,&module,error) || !ce_module_check(&ls->catalog,module,error) ||
        !ce_module_theory(&ls->catalog,module,theory,&presentation,error)) return false;
    if (presentation.len) return ce_error(error,0,"a layout theory declares no Terms");
    for (size_t i = 0; i < presentation.nlaws; ++i) {
        const CeTree *law = presentation.laws[i].tree;
        if (!ce_tree_label(&ls->syntax,law,"EquationImpl"))
            return ce_error(error,law->offset,"a layout theory's laws are equations naming layouts");
        CeAtom *lhs = ce_law_atom(&ls->syntax,&ls->lexer,&ls->arena,law->children[0],error);
        if (!lhs) return false;
        if (lhs->kind != CE_ATOM_LIST || lhs->len != 1 || lhs->items[0]->kind != CE_ATOM_SYMBOL ||
            !identifier(lhs->items[0]->text))
            return ce_error(error,law->offset,"a layout equation's left side is (NAME)");
        CeAtom *rhs = ce_law_atom(&ls->syntax,&ls->lexer,&ls->arena,law->children[1],error);
        if (!rhs || !read_layout(ls,lhs->items[0]->text,rhs,error)) return false;
    }
    if (!presentation.nlaws) return ce_error(error,0,"theory %s declares no layout",theory);
    return export_names(ls,error);
}
size_t ce_layouts_count(const CeLayouts *ls) { return ls->len; }

static void write_trie(const Layout *l, int at, FILE *file) {
    const Node *n = &l->trie[at];
    if (n->leaf < 0) {
        fputs("(Node ",file); write_trie(l,n->child[0],file); fputc(' ',file); write_trie(l,n->child[1],file); fputc(')',file);
        return;
    }
    const Case *c = &l->cases[n->leaf];
    fprintf(file,"(Leaf \"%s\"",c->reserved ? "reserved" : "admits");
    for (size_t f = 0; f < l->nfields; ++f) {
        const Place *p = &c->places[f];
        fprintf(file," (Place %llu %llu %llu %llu %llu)",(unsigned long long)p->shift,(unsigned long long)p->width,
                (unsigned long long)p->offset,(unsigned long long)p->lo,(unsigned long long)p->hi);
    }
    fputc(')',file);
}
void ce_layout_write(const CeLayouts *ls, size_t index, FILE *file) {
    const Layout *l = &ls->items[index];
    fprintf(file,"(cesta-layout/1 %.*s %llu (Fields",(int)l->name.len,l->name.data,(unsigned long long)l->width);
    for (size_t f = 0; f < l->nfields; ++f) {
        fprintf(file," (%.*s ",(int)l->fields[f].name.len,l->fields[f].name.data); ce_quote(file,l->fields[f].kind);
        fputs(l->fields[f].after_publication ? " \"after-publication\")" : " \"construction\")",file);
    }
    fputs(") ",file); write_trie(l,0,file);
    fputs(" (Location ",file); ce_quote(file,l->location); fputs(") (Aliases",file);
    for (size_t i = 0; i < l->naliases; ++i) {
        fprintf(file," (%.*s %.*s ",(int)l->aliases[i].name.len,l->aliases[i].name.data,(int)l->aliases[i].field.len,
                l->aliases[i].field.data);
        ce_quote(file,l->aliases[i].condition); fputc(')',file);
    }
    fputs(") (Requires",file);
    for (size_t i = 0; i < l->nrequires; ++i) {
        fprintf(file," (%.*s %llu ",(int)l->requires[i].field.len,l->requires[i].field.data,
                (unsigned long long)l->requires[i].at_most);
        ce_quote(file,l->requires[i].reason); fputc(')',file);
    }
    fputc(')',file);
    if (l->from.len) fprintf(file," (From %.*s)",(int)l->from.len,l->from.data);
    if (l->key >= 0) fprintf(file," (Key %.*s)",(int)l->fields[l->key].name.len,l->fields[l->key].name.data);
    if (l->nconstants) {
        fputs(" (Constants",file);
        for (size_t i = 0; i < l->nconstants; ++i) {
            fprintf(file," (Const %.*s %llu ",(int)l->constants[i].name.len,l->constants[i].name.data,
                    (unsigned long long)l->constants[i].value);
            ce_atom_write(l->constants[i].expr,file); fputc(')',file);
        }
        fputc(')',file);
    }
    fputs(")\n",file);
}


/* ---- Accessors ---------------------------------------------------------------
   Each accessor is one C expression over uint64_t, built as an atom and both
   recorded (for the Lean admission, which compares it with
   LayoutLowering.lowerLayout) and printed as C from that same atom. */

static CeAtom *sym(CeLayouts *ls, const char *s) { return ce_atom_new(&ls->arena,CE_ATOM_SYMBOL,ce_cstr(&ls->arena,s),0); }
static CeAtom *num(CeLayouts *ls, uint64_t v) {
    char buffer[32]; snprintf(buffer,sizeof(buffer),"%llu",(unsigned long long)v); return sym(ls,buffer);
}
static CeAtom *op(CeLayouts *ls, const char *head, size_t n, CeAtom *a, CeAtom *b, CeAtom *c) {
    CeAtom *x = ce_atom_new(&ls->arena,CE_ATOM_LIST,(CeText){0},n+1);
    x->items[0] = sym(ls,head);
    if (n > 0) x->items[1] = a;
    if (n > 1) x->items[2] = b;
    if (n > 2) x->items[3] = c;
    return x;
}
static CeAtom *var(CeLayouts *ls, uint64_t i) { return op(ls,"var",1,num(ls,i),NULL,NULL); }
static CeAtom *lit(CeLayouts *ls, uint64_t v) { return op(ls,"lit",1,num(ls,v),NULL,NULL); }
static CeAtom *bin(CeLayouts *ls, const char *head, CeAtom *a, CeAtom *b) { return op(ls,head,2,a,b,NULL); }
static CeAtom *shift(CeLayouts *ls, const char *head, CeAtom *a, uint64_t n) { return op(ls,head,2,a,num(ls,n),NULL); }

static CeAtom *read_expr(CeLayouts *ls, const Place *p) {
    if (!p->width) return lit(ls,p->offset);
    return bin(ls,"add",bin(ls,"and",shift(ls,"shr",var(ls,0),p->shift),lit(ls,mask(p->width))),lit(ls,p->offset));
}
static CeAtom *conjunction(CeLayouts *ls, CeAtom **items, size_t n) {
    CeAtom *out = lit(ls,1);
    for (size_t i = n; i-- > 0;) out = bin(ls,"and",items[i],out);
    return out;
}
/* A bound that always holds is written as 1: compilers reject comparing an
   unsigned value with 0 or with its type's maximum as vacuous. */
static CeAtom *at_least(CeLayouts *ls, uint64_t lo, CeAtom *x) { return lo ? bin(ls,"le",lit(ls,lo),x) : lit(ls,1); }
static CeAtom *at_most(CeLayouts *ls, CeAtom *x, uint64_t hi) { return hi != UINT64_MAX ? bin(ls,"le",x,lit(ls,hi)) : lit(ls,1); }
static CeAtom *bit_clear(CeLayouts *ls, uint64_t width, uint64_t depth) {
    return bin(ls,"eq",bin(ls,"and",shift(ls,"shr",var(ls,0),width - 1 - depth),lit(ls,1)),lit(ls,0));
}
/* The expression of a case, chosen through the prefix bits. */
typedef CeAtom *(*LeafExpr)(CeLayouts *ls, const Layout *l, const Case *c, size_t field);
static CeAtom *walk(CeLayouts *ls, const Layout *l, int at, uint64_t depth, LeafExpr leaf, size_t field) {
    const Node *n = &l->trie[at];
    if (n->leaf >= 0) return leaf(ls,l,&l->cases[n->leaf],field);
    return op(ls,"ite",3,bit_clear(ls,l->width,depth),walk(ls,l,n->child[0],depth+1,leaf,field),
              walk(ls,l,n->child[1],depth+1,leaf,field));
}
static CeAtom *part_leaf(CeLayouts *ls, const Layout *l, const Case *c, size_t field) {
    (void)l; return read_expr(ls,&c->places[field]);
}
static CeAtom *valid_leaf(CeLayouts *ls, const Layout *l, const Case *c, size_t field) {
    (void)field;
    if (c->reserved) return lit(ls,0);
    CeAtom **items = ce_alloc(&ls->arena,(l->nfields+1)*sizeof(*items));
    for (size_t f = 0; f < l->nfields; ++f) {
        const Place *p = &c->places[f];
        items[f] = bin(ls,"and",at_least(ls,p->lo,read_expr(ls,p)),at_most(ls,read_expr(ls,p),p->hi));
    }
    return conjunction(ls,items,l->nfields);
}
static CeAtom *admits_case(CeLayouts *ls, const Layout *l, const Case *c) {
    if (c->reserved) return lit(ls,0);
    CeAtom **items = ce_alloc(&ls->arena,(l->nfields+1)*sizeof(*items));
    for (size_t f = 0; f < l->nfields; ++f) {
        const Place *p = &c->places[f];
        items[f] = bin(ls,"and",at_least(ls,p->lo,var(ls,f)),at_most(ls,var(ls,f),p->hi));
    }
    return conjunction(ls,items,l->nfields);
}
static CeAtom *write_place(CeLayouts *ls, const Place *p, uint64_t j) {
    if (!p->width) return lit(ls,0);
    return shift(ls,"shl",bin(ls,"and",bin(ls,"sub",var(ls,j),lit(ls,p->offset)),lit(ls,mask(p->width))),p->shift);
}
/* The leaves in trie order, each with its prefix and depth. */
typedef struct { const Case *c; uint64_t prefix, depth; } Leaf;
static void leaves(const Layout *l, int at, uint64_t prefix, uint64_t depth, Leaf *out, size_t *n) {
    const Node *node = &l->trie[at];
    if (node->leaf >= 0) { out[(*n)++] = (Leaf){&l->cases[node->leaf],prefix,depth}; return; }
    leaves(l,node->child[0],prefix << 1,depth+1,out,n);
    leaves(l,node->child[1],prefix << 1 | 1,depth+1,out,n);
}
/* Field `key` chooses the case: its ranges ascend through the admitting
   cases in prefix order, so one comparison per case finds a value's case. */
static bool check_key(const Layout *l, size_t offset, CeError *error) {
    size_t n = 0; bool ok = true;
    Leaf *found = malloc((l->ncases+1)*sizeof(*found));
    if (!found) return ce_error(error,offset,"cannot allocate layout cases");
    leaves(l,0,0,0,found,&n);
    for (size_t a = 0; a < n && ok; ++a)
        for (size_t b = a + 1; b < n && ok; ++b)
            if (!found[a].c->reserved && !found[b].c->reserved &&
                !(found[a].c->places[l->key].hi < found[b].c->places[l->key].lo))
                ok = ce_error(error,offset,"key %.*s: its ranges do not ascend through the admitting cases in prefix order",
                              (int)l->fields[l->key].name.len,l->fields[l->key].name.data);
    free(found);
    return ok;
}
static CeAtom *word_of(CeLayouts *ls, const Layout *l, const Leaf *leaf) {
    CeAtom *pack = lit(ls,0);
    for (size_t f = l->nfields; f-- > 0;) pack = bin(ls,"or",write_place(ls,&leaf->c->places[f],f),pack);
    uint64_t prefix = leaf->depth ? leaf->prefix << (l->width - leaf->depth) : 0;
    return bin(ls,"or",lit(ls,prefix),pack);
}
static CeAtom *encode_expr(CeLayouts *ls, const Layout *l) {
    size_t nleaves = 0;
    Leaf *found = ce_alloc(&ls->arena,(l->ncases+1)*sizeof(*found));
    leaves(l,0,0,0,found,&nleaves);
    CeAtom *encode = lit(ls,0);
    for (size_t i = nleaves; i-- > 0;)
        encode = op(ls,"ite",3,admits_case(ls,l,found[i].c),word_of(ls,l,&found[i]),encode);
    return encode;
}
/* Replace each parameter j below n by readings[j]. */
static CeAtom *substitute(CeLayouts *ls, const CeAtom *a, CeAtom **readings, size_t n) {
    if (ce_is(a->items[0]->text,"var")) {
        size_t j = (size_t)strtoull((const char *)a->items[1]->text.data,NULL,10);
        return j < n ? readings[j] : var(ls,j + 1);
    }
    if (ce_is(a->items[0]->text,"lit")) return (CeAtom *)a;
    CeAtom *x = ce_atom_new(&ls->arena,CE_ATOM_LIST,(CeText){0},a->len);
    x->items[0] = a->items[0];
    for (size_t i = 1; i < a->len; ++i)
        x->items[i] = a->items[i]->kind == CE_ATOM_LIST ? substitute(ls,a->items[i],readings,n) : a->items[i];
    return x;
}
/* The new version's word for an old word: the old checked reading guarding
   the new encoder, with the old readings in place of its parameters. */
static CeAtom *migration(CeLayouts *ls, const Layout *old, const Layout *l) {
    CeAtom **readings = ce_alloc(&ls->arena,(old->nfields+1)*sizeof(*readings));
    for (size_t f = 0; f < old->nfields; ++f) readings[f] = walk(ls,old,0,0,part_leaf,f);
    return op(ls,"ite",3,walk(ls,old,0,0,valid_leaf,0),substitute(ls,encode_expr(ls,l),readings,old->nfields),lit(ls,0));
}
static const Layout *earlier(const CeLayouts *ls, const Layout *l) {
    for (size_t i = 0; i < ls->len; ++i) if (l->from.len && ce_equal(ls->items[i].name,l->from)) return &ls->items[i];
    return NULL;
}
static uint64_t slot_mask(const Place *p) { return p->width ? mask(p->width) << p->shift : 0; }

/* An accessor: its parameters are the named ones, every field (`fields`) or
   the key field alone (`keyed`). */
typedef struct {
    CeText name; const char *result; const char *params[3]; size_t nparams; bool fields; CeAtom *body; bool keyed;
} Function;

static CeText joined(CeLayouts *ls, CeText a, const char *mid, CeText b) {
    size_t m = strlen(mid); unsigned char *s = ce_alloc(&ls->arena,a.len+m+b.len+1);
    if (a.len) memcpy(s,a.data,a.len);
    memcpy(s+a.len,mid,m);
    if (b.len) memcpy(s+a.len+m,b.data,b.len);
    s[a.len+m+b.len] = 0;
    return (CeText){s,a.len+m+b.len};
}
static size_t lower(CeLayouts *ls, const Layout *l, Function *out) {
    size_t n = 0, nleaves = 0;
    Leaf *ls_leaves = ce_alloc(&ls->arena,(l->ncases+1)*sizeof(*ls_leaves));
    leaves(l,0,0,0,ls_leaves,&nleaves);
    for (size_t f = 0; f < l->nfields; ++f)
        out[n++] = (Function){joined(ls,l->name,"_",l->fields[f].name),"value",{"word"},1,false,walk(ls,l,0,0,part_leaf,f),false};
    out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"valid")),"bool",{"word"},1,false,walk(ls,l,0,0,valid_leaf,0),false};
    CeAtom *any = lit(ls,0), *encode = encode_expr(ls,l);
    for (size_t i = nleaves; i-- > 0;) any = bin(ls,"or",admits_case(ls,l,ls_leaves[i].c),any);
    out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"admits")),"bool",{0},0,true,any,false};
    out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"encode")),"word",{0},0,true,encode,false};
    if (l->ncases == 1) {
        const Case *c = &l->cases[0];
        for (size_t f = 0; f < l->nfields; ++f) {
            const Place *p = &c->places[f];
            CeAtom *body = !p->width ? var(ls,0) :
                bin(ls,"or",bin(ls,"and",var(ls,0),op(ls,"not",1,lit(ls,slot_mask(p)),NULL,NULL)),write_place(ls,p,1));
            out[n++] = (Function){joined(ls,l->name,"_set_",l->fields[f].name),"word",{"word","value"},2,false,body,false};
        }
        bool derived = false;
        uint64_t any_mask = 0, all_mask = 0, own_mask = 0, every = 0, kept = 0;
        for (size_t f = 0; f < l->nfields; ++f) {
            CeText k = l->fields[f].kind; uint64_t m = slot_mask(&c->places[f]);
            derived |= ce_is(k,"any-child") || ce_is(k,"all-children") || ce_is(k,"cache") || ce_is(k,"occurrence");
            if (ce_is(k,"any-child")) any_mask |= m;
            if (ce_is(k,"all-children")) all_mask |= m;
            if (ce_is(k,"value") || ce_is(k,"fact") || ce_is(k,"reference")) own_mask |= m;
            every |= m;
            if (!ce_is(k,"occurrence")) kept |= m;
        }
        if (derived) {
            out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"derive_init")),"word",{0},0,false,lit(ls,all_mask),false};
            out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"derive_child")),"word",{"acc","child"},2,false,
                bin(ls,"or",bin(ls,"and",bin(ls,"or",var(ls,0),var(ls,1)),lit(ls,any_mask)),
                    bin(ls,"and",bin(ls,"and",var(ls,0),var(ls,1)),lit(ls,all_mask))),false};
            out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"derive_finish")),"word",{"acc","own"},2,false,
                bin(ls,"or",bin(ls,"and",var(ls,0),lit(ls,any_mask | all_mask)),bin(ls,"and",var(ls,1),lit(ls,own_mask))),false};
            out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"value_copy")),"word",{"word"},1,false,
                bin(ls,"and",var(ls,0),lit(ls,every)),false};
            out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"store")),"word",{"word"},1,false,
                bin(ls,"and",var(ls,0),lit(ls,kept)),false};
            out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"relocate")),"word",{"word"},1,false,
                bin(ls,"and",var(ls,0),lit(ls,every)),false};
        }
    }
    return n;
}
static size_t lowered_bound(const Layout *l) { return l->nfields * 2 + 10; }

/* ---- Views -------------------------------------------------------------------
   For a word with several cases, each field's bounds in the word's case. For a
   keyed layout, each field's bounds in the case that holds a key, and whether
   some case holds it. For a keyed layout or one with a single admitting case,
   the unchecked encoder: one comparison of the key per case, correct for
   admitted values only. LayoutViews.lowerViews states the same list. */
static CeAtom *lo_leaf(CeLayouts *ls, const Layout *l, const Case *c, size_t field) {
    (void)l; return lit(ls,c->places[field].lo);
}
static CeAtom *hi_leaf(CeLayouts *ls, const Layout *l, const Case *c, size_t field) {
    (void)l; return lit(ls,c->places[field].hi);
}
static CeAtom *one_leaf(CeLayouts *ls, const Layout *l, const Case *c, size_t field) {
    (void)l; (void)c; (void)field; return lit(ls,1);
}
/* The first admitting case whose key range holds the key (parameter 0). */
static CeAtom *by_key(CeLayouts *ls, const Layout *l, LeafExpr leaf, size_t field) {
    size_t n = 0;
    Leaf *found = ce_alloc(&ls->arena,(l->ncases+1)*sizeof(*found));
    leaves(l,0,0,0,found,&n);
    CeAtom *rest = lit(ls,0);
    for (size_t i = n; i-- > 0;) {
        const Case *c = found[i].c;
        if (c->reserved) continue;
        const Place *p = &c->places[l->key];
        rest = op(ls,"ite",3,bin(ls,"and",at_least(ls,p->lo,var(ls,0)),at_most(ls,var(ls,0),p->hi)),
                  leaf(ls,l,c,field),rest);
    }
    return rest;
}
/* A field shifted into its slot without truncation, and a case's word made of
   such fields: exact for admitted values, which fit their slots. */
static CeAtom *place_value(CeLayouts *ls, const Place *p, uint64_t j) {
    if (!p->width) return lit(ls,0);
    return shift(ls,"shl",p->offset ? bin(ls,"sub",var(ls,j),lit(ls,p->offset)) : var(ls,j),p->shift);
}
static CeAtom *packed_word(CeLayouts *ls, const Layout *l, const Leaf *leaf) {
    CeAtom *pack = lit(ls,0);
    for (size_t f = l->nfields; f-- > 0;) pack = bin(ls,"or",place_value(ls,&leaf->c->places[f],f),pack);
    uint64_t prefix = leaf->depth ? leaf->prefix << (l->width - leaf->depth) : 0;
    return bin(ls,"or",lit(ls,prefix),pack);
}
static CeAtom *pack_chain(CeLayouts *ls, const Layout *l, uint64_t key) {
    size_t n = 0, m = 0;
    Leaf *found = ce_alloc(&ls->arena,(l->ncases+1)*sizeof(*found));
    leaves(l,0,0,0,found,&n);
    for (size_t i = 0; i < n; ++i) if (!found[i].c->reserved) found[m++] = found[i];
    if (!m) return lit(ls,0);
    CeAtom *rest = packed_word(ls,l,&found[m-1]);
    for (size_t i = m - 1; i-- > 0;)
        rest = op(ls,"ite",3,at_most(ls,var(ls,key),found[i].c->places[key].hi),packed_word(ls,l,&found[i]),rest);
    return rest;
}
static size_t lower_views(CeLayouts *ls, const Layout *l, Function *out) {
    size_t n = 0, admitting = 0;
    if (l->trie[0].leaf < 0)
        for (size_t f = 0; f < l->nfields; ++f) {
            CeText field = joined(ls,l->name,"_",l->fields[f].name);
            out[n++] = (Function){joined(ls,field,"_lo",(CeText){0}),"value",{"word"},1,false,walk(ls,l,0,0,lo_leaf,f),false};
            out[n++] = (Function){joined(ls,field,"_hi",(CeText){0}),"value",{"word"},1,false,walk(ls,l,0,0,hi_leaf,f),false};
        }
    if (l->key >= 0) {
        CeText key = l->fields[l->key].name;
        for (size_t f = 0; f < l->nfields; ++f) {
            CeText field = joined(ls,l->name,"_",l->fields[f].name);
            out[n++] = (Function){joined(ls,field,"_lo_for_",key),"value",{0},0,false,by_key(ls,l,lo_leaf,f),true};
            out[n++] = (Function){joined(ls,field,"_hi_for_",key),"value",{0},0,false,by_key(ls,l,hi_leaf,f),true};
        }
        out[n++] = (Function){joined(ls,l->name,"_admits_",key),"bool",{0},0,false,by_key(ls,l,one_leaf,0),true};
    }
    for (size_t c = 0; c < l->ncases; ++c) admitting += !l->cases[c].reserved;
    if (l->key >= 0 || admitting == 1)
        out[n++] = (Function){joined(ls,l->name,"_",ce_cstr(&ls->arena,"pack")),"word",{0},0,true,
                              pack_chain(ls,l,l->key >= 0 ? (uint64_t)l->key : 0),false};
    return n;
}
static size_t views_bound(const Layout *l) { return l->nfields * 4 + 2; }

static bool add_export_name(CeText **names, size_t *len, CeText name, CeError *error) {
    for (size_t i = 0; i < *len; ++i)
        if (ce_equal((*names)[i],name))
            return ce_error(error,0,"layouts export the same C name %.*s",(int)name.len,name.data);
    *names = ce_resize(*names,*len+1,sizeof(**names));
    (*names)[(*len)++] = name;
    return true;
}
static bool export_names(CeLayouts *ls, CeError *error) {
    CeText *names = NULL; size_t len = 0; bool ok = true;
    for (size_t i = 0; i < ls->len && ok; ++i) {
        const Layout *l = &ls->items[i];
        Function *fs = ce_alloc(&ls->arena,(lowered_bound(l)+views_bound(l))*sizeof(*fs));
        size_t n = lower(ls,l,fs);
        n += lower_views(ls,l,fs+n);
        for (size_t c = 0; c < l->nconstants && ok; ++c)
            ok = add_export_name(&names,&len,joined(ls,l->name,"_",l->constants[c].name),error);
        for (size_t j = 0; j < n && ok; ++j) {
            ok = add_export_name(&names,&len,fs[j].name,error);
            if (ok) {
                CeText body = joined(ls,ce_cstr(&ls->arena,"CESTA_LAYOUT_"),"",fs[j].name);
                body = joined(ls,body,"_BODY",(CeText){0});
                ok = add_export_name(&names,&len,body,error);
            }
        }
        if (ce_is(l->location,"atomic")) {
            if (ok) ok = add_export_name(&names,&len,joined(ls,l->name,"_",ce_cstr(&ls->arena,"atomic_load")),error);
            if (ok) ok = add_export_name(&names,&len,joined(ls,l->name,"_",ce_cstr(&ls->arena,"atomic_store")),error);
            for (size_t f = 0; l->ncases == 1 && f < l->nfields && ok; ++f)
                ok = add_export_name(&names,&len,joined(ls,l->name,"_atomic_set_",l->fields[f].name),error);
        }
        if (ok && l->from.len) ok = add_export_name(&names,&len,joined(ls,l->name,"_from_",l->from),error);
    }
    free(names);
    return ok;
}

static bool uses_param(const CeAtom *a, size_t index) {
    if (a->kind != CE_ATOM_LIST) return false;
    if (ce_is(a->items[0]->text,"var"))
        return (size_t)strtoull((const char *)a->items[1]->text.data,NULL,10) == index;
    for (size_t i = 1; i < a->len; ++i) if (uses_param(a->items[i],index)) return true;
    return false;
}

static void write_function_atom(const Layout *l, const Function *f, FILE *file) {
    fputs("(Function ",file); ce_quote(file,f->name); fputs(" (Params",file);
    if (f->fields)
        for (size_t i = 0; i < l->nfields; ++i) { fputc(' ',file); ce_quote(file,l->fields[i].name); }
    else if (f->keyed) { fputc(' ',file); ce_quote(file,l->fields[l->key].name); }
    else for (size_t i = 0; i < f->nparams; ++i) fprintf(file," \"%s\"",f->params[i]);
    fprintf(file,") \"%s\" ",f->result); ce_atom_write(f->body,file); fputc(')',file);
}
void ce_layout_functions(const CeLayouts *ls, size_t index, FILE *file) {
    const Layout *l = &ls->items[index];
    Function *fs = ce_alloc(&((CeLayouts *)ls)->arena,lowered_bound(l)*sizeof(*fs));
    size_t n = lower((CeLayouts *)ls,l,fs);
    fprintf(file,"(cesta-layout-functions/1 %.*s",(int)l->name.len,l->name.data);
    for (size_t i = 0; i < n; ++i) { fputc(' ',file); write_function_atom(l,&fs[i],file); }
    fputs(")\n",file);
    Function *vs = ce_alloc(&((CeLayouts *)ls)->arena,views_bound(l)*sizeof(*vs));
    size_t nv = lower_views((CeLayouts *)ls,l,vs);
    fprintf(file,"(cesta-layout-views/1 %.*s (Constants",(int)l->name.len,l->name.data);
    for (size_t i = 0; i < l->nconstants; ++i) {
        fputs(" (",file); ce_quote(file,joined((CeLayouts *)ls,l->name,"_",l->constants[i].name));
        fprintf(file," %llu)",(unsigned long long)l->constants[i].value);
    }
    fputc(')',file);
    for (size_t i = 0; i < nv; ++i) { fputc(' ',file); write_function_atom(l,&vs[i],file); }
    fputs(")\n",file);
    const Layout *old = earlier(ls,l);
    if (old) {
        Function m = {joined((CeLayouts *)ls,l->name,"_from_",old->name),"word",{"old"},1,false,
                      migration((CeLayouts *)ls,old,l),false};
        fprintf(file,"(cesta-layout-migration/1 %.*s %.*s ",(int)old->name.len,old->name.data,(int)l->name.len,l->name.data);
        write_function_atom(l,&m,file); fputs(")\n",file);
    }
}

/* C for an expression atom: every operand is uint64_t. */
static void write_c(const CeAtom *a, const char *const *params, FILE *file) {
    CeText head = a->items[0]->text;
    if (ce_is(head,"var")) {
        size_t i = (size_t)strtoull((const char *)a->items[1]->text.data,NULL,10);
        fprintf(file,"((uint64_t)%s)",params[i]); return;
    }
    if (ce_is(head,"lit")) { fprintf(file,"UINT64_C(%.*s)",(int)a->items[1]->text.len,a->items[1]->text.data); return; }
    if (ce_is(head,"not")) { fputs("(~",file); write_c(a->items[1],params,file); fputc(')',file); return; }
    if (ce_is(head,"shl") || ce_is(head,"shr")) {
        fputc('(',file); write_c(a->items[1],params,file);
        fprintf(file," %s %.*s)",ce_is(head,"shl") ? "<<" : ">>",(int)a->items[2]->text.len,a->items[2]->text.data);
        return;
    }
    if (ce_is(head,"ite")) {
        fputc('(',file); write_c(a->items[1],params,file); fputs(" ? ",file); write_c(a->items[2],params,file);
        fputs(" : ",file); write_c(a->items[3],params,file); fputc(')',file); return;
    }
    const char *symbol = ce_is(head,"and") ? "&" : ce_is(head,"or") ? "|" : ce_is(head,"add") ? "+" :
                         ce_is(head,"sub") ? "-" : ce_is(head,"eq") ? "==" : "<=";
    bool compare = ce_is(head,"eq") || ce_is(head,"le");
    fputs(compare ? "((uint64_t)(" : "(",file); write_c(a->items[1],params,file);
    fprintf(file," %s ",symbol); write_c(a->items[2],params,file); fputs(compare ? "))" : ")",file);
}
static const char *word_type(uint64_t width) {
    return width == 8 ? "uint8_t" : width == 16 ? "uint16_t" : width == 32 ? "uint32_t" : width == 64 ? "uint64_t" : NULL;
}
static void fingerprint_number(uint64_t *hash, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        *hash = (*hash ^ (value & 255)) * UINT64_C(1099511628211);
        value >>= 8;
    }
}
static void fingerprint_text(uint64_t *hash, CeText text) {
    fingerprint_number(hash,text.len);
    for (size_t i = 0; i < text.len; ++i)
        *hash = (*hash ^ text.data[i]) * UINT64_C(1099511628211);
}
/* The declaration, rather than a module's reusable theory name, identifies
   the header guard. Unrelated modules may both declare a theory Layouts. */
static uint64_t fingerprint(const CeLayouts *ls) {
    uint64_t hash = UINT64_C(14695981039346656037);
    fingerprint_number(&hash,ls->len);
    for (size_t i = 0; i < ls->len; ++i) {
        const Layout *l = &ls->items[i];
        fingerprint_text(&hash,l->name); fingerprint_number(&hash,l->width);
        fingerprint_text(&hash,l->location); fingerprint_text(&hash,l->from);
        fingerprint_number(&hash,l->nfields);
        for (size_t f = 0; f < l->nfields; ++f) {
            fingerprint_text(&hash,l->fields[f].name); fingerprint_text(&hash,l->fields[f].kind);
            fingerprint_number(&hash,l->fields[f].after_publication);
        }
        fingerprint_number(&hash,l->ncases);
        for (size_t c = 0; c < l->ncases; ++c) {
            const Case *p = &l->cases[c];
            fingerprint_number(&hash,p->reserved); fingerprint_number(&hash,p->prefix);
            fingerprint_number(&hash,p->depth);
            for (size_t f = 0; f < l->nfields; ++f) {
                const Place *q = &p->places[f];
                fingerprint_number(&hash,q->shift); fingerprint_number(&hash,q->width);
                fingerprint_number(&hash,q->offset); fingerprint_number(&hash,q->lo);
                fingerprint_number(&hash,q->hi);
            }
        }
    }
    return hash;
}
bool ce_layout_header(const CeLayouts *ls, const char *guard, FILE *file, CeError *error) {
    for (size_t i = 0; i < ls->len; ++i)
        if (!word_type(ls->items[i].width))
            return ce_error(error,0,"layout %.*s has a %llu-bit word; C accessors need 8, 16, 32 or 64 bits",
                            (int)ls->items[i].name.len,ls->items[i].name.data,(unsigned long long)ls->items[i].width);
    bool atomic = false;
    for (size_t i = 0; i < ls->len; ++i) atomic |= ce_is(ls->items[i].location,"atomic");
    fprintf(file,"/* Generated by cesta layout header; edit the layout module instead. */\n"
                 "#ifndef %s\n#define %s\n\n%s#include <stdbool.h>\n#include <stdint.h>\n",guard,guard,
            atomic ? "#include <stdatomic.h>\n" : "");
    for (size_t i = 0; i < ls->len; ++i) {
        const Layout *l = &ls->items[i]; const char *type = word_type(l->width);
        Function *fs = ce_alloc(&((CeLayouts *)ls)->arena,(lowered_bound(l)+views_bound(l))*sizeof(*fs));
        size_t n = lower((CeLayouts *)ls,l,fs);
        n += lower_views((CeLayouts *)ls,l,fs+n);
        fprintf(file,"\n/* %.*s: %llu-bit word. */\n",(int)l->name.len,l->name.data,(unsigned long long)l->width);
        for (size_t c = 0; c < l->nconstants; ++c)
            fprintf(file,"#define %.*s_%.*s UINT64_C(%llu)\n",(int)l->name.len,l->name.data,
                    (int)l->constants[c].name.len,l->constants[c].name.data,(unsigned long long)l->constants[c].value);
        for (size_t k = 0; k < n; ++k) {
            const Function *f = &fs[k];
            size_t np = f->fields ? l->nfields : f->keyed ? 1 : f->nparams;
            const char **params = ce_alloc(&((CeLayouts *)ls)->arena,(np+1)*sizeof(*params));
            const char **wrapped = ce_alloc(&((CeLayouts *)ls)->arena,(np+1)*sizeof(*wrapped));
            const char *result = ce_is(ce_cstr(&((CeLayouts *)ls)->arena,f->result),"bool") ? "bool" :
                                 !strcmp(f->result,"value") ? "uint64_t" : type;
            /* C11 external-linkage inline definitions cannot refer to static
               functions. These implementation macros let an adapter reuse
               the same expression with its own already-evaluated parameters. */
            fprintf(file,"#define CESTA_LAYOUT_%.*s_BODY(",(int)f->name.len,f->name.data);
            for (size_t j = 0; j < np; ++j) {
                char ordinal[64];
                snprintf(ordinal,sizeof(ordinal),"cesta_layout_arg%zu",j);
                CeText text = joined((CeLayouts *)ls,ce_cstr(&((CeLayouts *)ls)->arena,"("),ordinal,
                                     ce_cstr(&((CeLayouts *)ls)->arena,")"));
                wrapped[j] = (const char *)text.data;
                fprintf(file,"%s%s",j ? ", " : "",ordinal);
            }
            fputs(") ",file); write_c(f->body,wrapped,file); fputc('\n',file);
            fprintf(file,"static inline %s %.*s(",result,(int)f->name.len,f->name.data);
            for (size_t j = 0; j < np; ++j) {
                /* Field names may also be C keywords, typedefs or stdint
                   macros. Prefix the C binder; expression variables retain
                   their indexed, logical field names in function records. */
                CeText logical = f->fields ? l->fields[j].name : f->keyed ? l->fields[l->key].name : (CeText){0};
                params[j] = f->fields || f->keyed ? (const char *)joined((CeLayouts *)ls,
                    ce_cstr(&((CeLayouts *)ls)->arena,"cesta_value_"),"",logical).data : f->params[j];
                bool word = !f->fields && !f->keyed && (!strcmp(params[j],"word") || !strcmp(params[j],"acc") ||
                                           !strcmp(params[j],"child") || !strcmp(params[j],"own"));
                fprintf(file,"%s%s %s",j ? ", " : "",word ? type : "uint64_t",params[j]);
            }
            if (!np) fputs("void",file);
            fputs(") {\n",file);
            for (size_t j = 0; j < np; ++j)
                if (!uses_param(f->body,j)) fprintf(file,"    (void)%s;\n",params[j]);
            fprintf(file,"    return (%s)CESTA_LAYOUT_%.*s_BODY(",result,(int)f->name.len,f->name.data);
            for (size_t j = 0; j < np; ++j) fprintf(file,"%s%s",j ? ", " : "",params[j]);
            fputc(')',file);
            fputs(";\n}\n",file);
        }
        if (ce_is(l->location,"atomic")) {
            /* An atomic word is only accessed atomically: loads acquire what
               stores release, and a field is written by compare-and-swap of
               the whole word through its proved setter. */
            fprintf(file,"static inline %s %.*s_atomic_load(_Atomic %s *word) {\n"
                         "    return atomic_load_explicit(word, memory_order_acquire);\n}\n"
                         "static inline void %.*s_atomic_store(_Atomic %s *word, %s value) {\n"
                         "    atomic_store_explicit(word, value, memory_order_release);\n}\n",
                    type,(int)l->name.len,l->name.data,type,(int)l->name.len,l->name.data,type,type);
            if (l->ncases == 1)
                for (size_t f = 0; f < l->nfields; ++f)
                    fprintf(file,"static inline void %.*s_atomic_set_%.*s(_Atomic %s *word, uint64_t value) {\n"
                                 "    %s old = atomic_load_explicit(word, memory_order_relaxed);\n"
                                 "    while (!atomic_compare_exchange_weak_explicit(word, &old, %.*s_set_%.*s(old, value),\n"
                                 "                                                  memory_order_release, memory_order_relaxed)) {}\n}\n",
                            (int)l->name.len,l->name.data,(int)l->fields[f].name.len,l->fields[f].name.data,type,type,
                            (int)l->name.len,l->name.data,(int)l->fields[f].name.len,l->fields[f].name.data);
        }
        const Layout *old = earlier(ls,l);
        if (old) {
            const char *params[1] = {"old"};
            fprintf(file,"/* A %.*s word as a %.*s word; 0 for a word %.*s does not decode. */\n"
                         "static inline %s %.*s_from_%.*s(%s old) {\n    return (%s)",
                    (int)old->name.len,old->name.data,(int)l->name.len,l->name.data,(int)old->name.len,old->name.data,
                    type,(int)l->name.len,l->name.data,(int)old->name.len,old->name.data,word_type(old->width),type);
            write_c(migration((CeLayouts *)ls,old,l),params,file);
            fputs(";\n}\n",file);
        }
    }
    fprintf(file,"\n#endif\n");
    return true;
}

int ce_layout_command(int argc, char **argv) {
    const char *mode = argc > 2 ? argv[2] : "";
    bool header = !strcmp(mode,"header");
    if (!((argc == 5 && (!strcmp(mode,"check") || !strcmp(mode,"functions"))) ||
          (header && argc == 7 && !strcmp(argv[5],"-o")))) {
        fputs("usage: cesta layout check|functions MODULE THEORY\n"
              "       cesta layout header MODULE THEORY -o OUTPUT.h\n",stderr);
        return 1;
    }
    CeError error = {{0},0}; CeLayouts *ls = ce_layouts_new(&error); int result = 1;
    if (!ls || !ce_layouts_load(ls,argv[3],argv[4],&error)) {
        fprintf(stderr,"%s: byte %zu: %s\n",argv[3],error.offset,error.message);
        ce_layouts_free(ls); return 1;
    }
    if (header) {
        char guard[64];
        snprintf(guard,sizeof(guard),"CESTA_LAYOUT_%016llX_H",(unsigned long long)fingerprint(ls));
        FILE *out = fopen(argv[6],"w");
        if (!out) fprintf(stderr,"%s: cannot write\n",argv[6]);
        else {
            if (ce_layout_header(ls,guard,out,&error)) result = 0;
            else fprintf(stderr,"%s: %s\n",argv[3],error.message);
            if (fclose(out)) result = 1;
        }
    } else {
        for (size_t i = 0; i < ls->len; ++i)
            (!strcmp(mode,"check") ? ce_layout_write : ce_layout_functions)(ls,i,stdout);
        result = 0;
    }
    ce_layouts_free(ls); return result;
}

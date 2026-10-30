#include "cesta.h"

#include <stdlib.h>
#include <string.h>

CeAtom *ce_atom_new(CeArena *arena, unsigned kind, CeText text, size_t len) {
    CeAtom *atom = ce_alloc(arena,sizeof(*atom));
    atom->kind = kind; atom->text = text; atom->len = len;
    atom->items = ce_alloc(arena,len*sizeof(*atom->items));
    return atom;
}

static bool white(unsigned c) { return c == ' ' || (c >= 9 && c <= 13); }
static void skip(CeText source, size_t *pos) {
    for (;;) {
        while (*pos < source.len && white(source.data[*pos])) ++*pos;
        if (*pos == source.len || source.data[*pos] != ';') return;
        while (*pos < source.len && source.data[*pos] != '\n') ++*pos;
    }
}

static bool symbol_utf8(CeText text) {
    for (size_t i = 0; i < text.len;) {
        unsigned c = text.data[i++];
        if (c < 32 || c == 127) return false;
        if (c < 128) continue;
        unsigned remaining, value, minimum;
        if (c >= 0xc2 && c <= 0xdf) { remaining = 1; value = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { remaining = 2; value = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { remaining = 3; value = c & 7; minimum = 0x10000; }
        else return false;
        if (remaining > text.len-i) return false;
        while (remaining--) {
            c = text.data[i++];
            if (c < 0x80 || c > 0xbf) return false;
            value = (value << 6) | (c & 63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

static bool read_one(CeArena *arena, CeText source, size_t *pos, CeAtom **out,
                      CeError *error, size_t depth) {
    skip(source,pos);
    size_t begin = *pos;
    if (depth > 4096) return ce_error(error,begin,"atom nesting exceeds reader stack depth");
    if (begin == source.len) return ce_error(error,begin,"expected presentation atom");
    unsigned c = source.data[*pos];
    if (c == ')') return ce_error(error,begin,"unexpected closing parenthesis");
    CeAtom *atom;
    if (c == '(') {
        ++*pos;
        CeAtom **items = NULL; size_t len = 0, capacity = 0;
        for (;;) {
            skip(source,pos);
            if (*pos == source.len) { free(items); return ce_error(error,begin,"unterminated atom list"); }
            if (source.data[*pos] == ')') { ++*pos; break; }
            CeAtom *child;
            if (!read_one(arena,source,pos,&child,error,depth+1)) { free(items); return false; }
            if (len == capacity) { capacity = capacity ? capacity*2 : 8; items = ce_resize(items,capacity,sizeof(*items)); }
            items[len++] = child;
        }
        atom = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},len);
        if (len) memcpy(atom->items,items,len*sizeof(*items));
        free(items);
    } else if (c == '"') {
        CeText value;
        if (!ce_quoted_value(arena,source,pos,&value,error)) return false;
        atom = ce_atom_new(arena,CE_ATOM_TEXT,value,0);
    } else {
        while (*pos < source.len) {
            c = source.data[*pos];
            if (white(c) || c == '(' || c == ')' || c == ';' || c == '"') break;
            ++*pos;
        }
        CeText value = {source.data+begin,*pos-begin};
        if (!symbol_utf8(value)) return ce_error(error,begin,"invalid UTF-8 or control byte in symbol");
        if (value.len && value.data[0] == '$') return ce_error(error,begin,"unfilled presentation hole");
        atom = ce_atom_new(arena,CE_ATOM_SYMBOL,ce_text(arena,value.data,value.len),0);
    }
    atom->offset = begin; *out = atom; return true;
}

bool ce_atom_read(CeArena *arena, CeText source, CeAtom **out, CeError *error) {
    size_t pos = 0;
    if (!read_one(arena,source,&pos,out,error,0)) return false;
    skip(source,&pos);
    return pos == source.len || ce_error(error,pos,"extra input after presentation atom");
}

void ce_atom_write(const CeAtom *atom, FILE *file) {
    if (atom->kind == CE_ATOM_TEXT) ce_quote(file,atom->text);
    else if (atom->kind == CE_ATOM_SYMBOL) fwrite(atom->text.data,1,atom->text.len,file);
    else {
        fputc('(',file);
        for (size_t i = 0; i < atom->len; ++i) {
            if (i) fputc(' ',file);
            ce_atom_write(atom->items[i],file);
        }
        fputc(')',file);
    }
}

bool ce_atom_equal(const CeAtom *first, const CeAtom *second) {
    if (first->kind != second->kind || first->len != second->len || !ce_equal(first->text,second->text)) return false;
    for (size_t i = 0; i < first->len; ++i) if (!ce_atom_equal(first->items[i],second->items[i])) return false;
    return true;
}

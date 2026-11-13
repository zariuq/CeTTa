#include "cesta.h"

#include <string.h>

/* This byte transport quotes atom kinds as data, including symbol heads.
   A host reader can therefore apply its ordinary token registrations without
   coercing source names, numeric spellings or presentation operators. */
static bool head(const CeAtom *atom, const char *name) {
    return atom->kind == CE_ATOM_LIST && atom->len && atom->items[0]->kind == CE_ATOM_SYMBOL &&
        ce_is(atom->items[0]->text,name);
}
bool ce_wire_envelope(const CeAtom *atom) { return head(atom,"cesta-wire/1"); }

static CeAtom *form(CeArena *arena, const char *name, size_t arguments, size_t offset) {
    CeAtom *atom = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},arguments+1);
    atom->offset = offset;
    atom->items[0] = ce_atom_new(arena,CE_ATOM_SYMBOL,ce_cstr(arena,name),0); return atom;
}
static bool pack(CeArena *arena, const CeAtom *atom, CeAtom **out, CeError *error, size_t depth) {
    if (depth > 4096) return ce_error(error,atom->offset,"wire nesting exceeds decoder stack depth");
    CeAtom *result;
    if (atom->kind == CE_ATOM_LIST) {
        result = form(arena,"cesta:e",atom->len,atom->offset);
        for (size_t i = 0; i < atom->len; ++i)
            if (!pack(arena,atom->items[i],&result->items[i+1],error,depth+1)) return false;
    } else {
        if (atom->text.len > (SIZE_MAX-1)/2) return ce_error(error,atom->offset,"wire byte length overflows");
        result = form(arena,atom->kind == CE_ATOM_SYMBOL ? "cesta:s" : "cesta:t",1,atom->offset);
        static const unsigned char digits[] = "0123456789abcdef";
        unsigned char *bytes = ce_alloc(arena,atom->text.len*2+1);
        for (size_t i = 0; i < atom->text.len; ++i) {
            bytes[2*i] = digits[atom->text.data[i] >> 4];
            bytes[2*i+1] = digits[atom->text.data[i] & 15];
        }
        result->items[1] = ce_atom_new(arena,CE_ATOM_TEXT,(CeText){bytes,atom->text.len*2},0);
    }
    *out = result; return true;
}
bool ce_wire_pack(CeArena *arena, const CeAtom *atom, CeAtom **out, CeError *error) {
    CeAtom *envelope = form(arena,"cesta-wire/1",1,atom->offset);
    if (!pack(arena,atom,&envelope->items[1],error,0)) return false;
    *out = envelope; return true;
}
static int nibble(unsigned char byte) {
    return byte >= '0' && byte <= '9' ? byte-'0' : byte >= 'a' && byte <= 'f' ? byte-'a'+10 : -1;
}
static bool unpack(CeArena *arena, const CeAtom *wire, CeAtom **out, CeError *error, size_t depth) {
    if (depth > 4096) return ce_error(error,wire->offset,"wire nesting exceeds decoder stack depth");
    if (head(wire,"cesta:e")) {
        CeAtom *result = ce_atom_new(arena,CE_ATOM_LIST,(CeText){NULL,0},wire->len-1);
        result->offset = wire->offset;
        for (size_t i = 1; i < wire->len; ++i)
            if (!unpack(arena,wire->items[i],&result->items[i-1],error,depth+1)) return false;
        *out = result; return true;
    }
    bool symbol = head(wire,"cesta:s");
    if (!symbol && !head(wire,"cesta:t")) return ce_error(error,wire->offset,"unknown wire atom tag");
    if (wire->len != 2 || wire->items[1]->kind != CE_ATOM_TEXT)
        return ce_error(error,wire->offset,"wire byte atom requires one hexadecimal string");
    CeText hexadecimal = wire->items[1]->text;
    if (hexadecimal.len % 2) return ce_error(error,wire->offset,"wire hexadecimal string has odd length");
    CeText bytes = {ce_alloc(arena,hexadecimal.len/2+1),hexadecimal.len/2};
    for (size_t i = 0; i < bytes.len; ++i) {
        int high = nibble(hexadecimal.data[2*i]), low = nibble(hexadecimal.data[2*i+1]);
        if (high < 0 || low < 0) return ce_error(error,wire->offset,"wire byte string requires lowercase hexadecimal digits");
        bytes.data[i] = (unsigned char)((high << 4)|low);
    }
    CeAtom *result;
    if (symbol) {
        CeError invalid = {{0},0};
        if (!ce_atom_read(arena,bytes,&result,&invalid) || result->kind != CE_ATOM_SYMBOL || !ce_equal(result->text,bytes))
            return ce_error(error,wire->offset,"wire symbol is not an admitted ungrounded atom symbol");
    } else result = ce_atom_new(arena,CE_ATOM_TEXT,bytes,0);
    result->offset = wire->offset; *out = result; return true;
}
bool ce_wire_unpack(CeArena *arena, const CeAtom *wire, CeAtom **out, CeError *error) {
    if (!ce_wire_envelope(wire) || wire->len != 2)
        return ce_error(error,wire->offset,"wire document requires cesta-wire/1 and one quoted atom");
    return unpack(arena,wire->items[1],out,error,0);
}
int ce_wire_command(int argc, char **argv) {
    CeArena arena = {0}; CeText source; CeAtom *atom, *result; CeError error = {{0},0};
    bool ok = argc == 3;
    if (ok) ok = ce_read_file(&arena,argv[2],&source,&error) && ce_atom_read(&arena,source,&atom,&error);
    if (ok) ok = !strcmp(argv[1],"pack") ? ce_wire_pack(&arena,atom,&result,&error) : ce_wire_unpack(&arena,atom,&result,&error);
    if (ok) { ce_atom_write(result,stdout); fputc('\n',stdout); ok = !ferror(stdout); }
    if (!ok) fprintf(stderr,"cesta %s: byte %zu: %s\n",argv[1],error.offset,
        error.message[0] ? error.message : "command takes one input file");
    ce_arena_free(&arena); return ok ? 0 : 1;
}

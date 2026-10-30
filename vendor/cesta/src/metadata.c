#include "cesta.h"

#include <stdlib.h>

/* Finite schema data retains physical categories, the logical sort quotient,
   every ordered RHS slot, and profile-event erasure separately. Flattened .cf
   inputs contain no module-origin evidence, so origin is explicitly null. */
static bool utf8(CeText text) {
    for (size_t i = 0; i < text.len;) {
        unsigned first = text.data[i++];
        if (first < 128) continue;
        unsigned n, value, minimum;
        if (first >= 194 && first <= 223) { n = 1; value = first&31; minimum = 128; }
        else if (first >= 224 && first <= 239) { n = 2; value = first&15; minimum = 2048; }
        else if (first >= 240 && first <= 244) { n = 3; value = first&7; minimum = 65536; }
        else return false;
        while (n--) {
            if (i == text.len || text.data[i] < 128 || text.data[i] > 191) return false;
            value = value*64+(text.data[i++]&63);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

static void string(FILE *file, CeText text) {
    fputc('"',file);
    for (size_t i = 0; i < text.len; ++i) {
        unsigned c = text.data[i];
        if (c == '"' || c == '\\') { fputc('\\',file); fputc((int)c,file); }
        else if (c < 32) fprintf(file,"\\u%04x",c);
        else fputc((int)c,file);
    }
    fputc('"',file);
}

static bool identifier_sort(const CeGrammar *g, int symbol) {
    return ce_is(g->symbols[symbol].name,"Ident") ||
        (g->c_scope && g->symbols[symbol].sort == g->symbols[g->name_token].sort);
}

static void sort_name(const CeGrammar *g, int symbol, FILE *file) {
    string(file,g->symbols[g->symbols[symbol].sort].name);
}

static int shape_symbol(const CeGrammar *g, int representative) {
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].sort == representative &&
            (g->symbols[i].kind == CE_TOKEN || g->symbols[i].list_element >= 0)) return (int)i;
    return representative;
}

bool ce_metadata_export(const CeGrammar *g, FILE *file, CeError *error) {
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (!utf8(g->symbols[i].name) || !utf8(g->symbols[i].surface))
            return ce_error(error,0,"metadata names and surfaces must be valid UTF-8 strings");
    for (size_t i = 0; i < g->nproductions; ++i) {
        const CeProduction *p = &g->productions[i];
        if (!utf8(p->label)) return ce_error(error,p->offset,"metadata constructor name is not valid UTF-8");
        if (p->internal) return ce_error(error,p->offset,"internal productions have no exported codec projection");
    }
    if (!utf8(g->name)) return ce_error(error,0,"metadata profile name is not valid UTF-8");
    fputs("{\"format\":\"cesta.sorted-metadata.v1\",\"profile\":",file); string(file,g->name);
    fputs(",\"sorts\":[",file);
    bool first = true;
    for (size_t i = 0; i < g->nsymbols; ++i) {
        const CeSymbol *s = &g->symbols[i];
        if (s->kind == CE_LITERAL || s->erased || s->sort != (int)i) continue;
        if (!first) fputc(',',file);
        first = false;
        fputs("{\"name\":",file); string(file,s->name); fputs(",\"shape\":{\"kind\":",file);
        int shape = shape_symbol(g,(int)i); const CeSymbol *physical = &g->symbols[shape];
        if (physical->kind == CE_TOKEN) {
            fputs("\"token\",\"representation\":",file);
            fputs(identifier_sort(g,shape) ? "\"identifier\"" : "\"literal\"",file);
            fputs(",\"head\":",file); string(file,physical->name);
        } else if (physical->list_element >= 0) {
            fputs("\"list\",\"element\":",file); sort_name(g,physical->list_element,file);
            fputs(",\"key\":",file); string(file,physical->name);
        } else fputs("\"constructors\"",file);
        fputs("}}",file);
    }
    fputs("],\"categories\":[",file); first = true;
    for (size_t i = 0; i < g->nsymbols; ++i) {
        const CeSymbol *s = &g->symbols[i];
        if (s->kind == CE_LITERAL) continue;
        if (!first) fputc(',',file);
        first = false;
        fputs("{\"name\":",file); string(file,s->name); fputs(",\"sort\":",file);
        if (s->erased) fputs("null",file); else sort_name(g,(int)i,file);
        fputs(",\"kind\":",file);
        fputs(s->erased ? "\"erased\"" : s->kind == CE_TOKEN ? "\"token\"" : "\"rule\"",file);
        fputc('}',file);
    }
    fputs("],\"productions\":[",file);
    for (size_t i = 0; i < g->nproductions; ++i) {
        const CeProduction *p = &g->productions[i]; const CeSymbol *lhs = &g->symbols[p->lhs];
        if (i) fputc(',',file);
        fprintf(file,"{\"occurrence\":%zu,\"origin\":null,\"resultCategory\":",i); string(file,lhs->name);
        fputs(",\"rhs\":[",file);
        int first_argument = -1;
        for (size_t j = 0; j < p->len; ++j) {
            const CeSymbol *s = &g->symbols[p->rhs[j]];
            if (j) fputc(',',file);
            if (s->kind == CE_LITERAL) {
                fputs("{\"kind\":\"fixed\",\"terminal\":",file); string(file,s->name);
                fputs(",\"surface\":",file);
                if (s->virtual_token && !s->surface.len) fputs("null",file);
                else string(file,s->surface.len ? s->surface : s->name);
            } else {
                fputs(s->erased ? "{\"kind\":\"erased\",\"category\":" : "{\"kind\":\"argument\",\"category\":",file);
                string(file,s->name);
                if (!s->erased && first_argument < 0) first_argument = p->rhs[j];
            }
            fputc('}',file);
        }
        fputs("],\"projection\":{\"kind\":",file);
        if (lhs->erased) fputs("\"erased\"",file);
        else if (ce_is(p->label,"_")) fputs("\"transparent\"",file);
        else if (lhs->list_element >= 0) {
            if (ce_is(p->label,"[]")) fputs("\"listEmpty\"",file);
            else if (ce_is(p->label,"(:[])")) fputs("\"listSingleton\"",file);
            else {
                fputs("\"listCons\",\"left\":",file);
                fputs(first_argument == p->lhs ? "true" : "false",file);
            }
        } else { fputs("\"constructor\",\"head\":",file); string(file,p->label); }
        fputs("}}",file);
    }
    fputs("]}\n",file);
    if (ferror(file)) return ce_error(error,0,"cannot write finite grammar metadata");
    return true;
}

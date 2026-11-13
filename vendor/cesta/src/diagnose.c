#include "diagnose.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { KIND_NONE, KIND_ERROR, KIND_WARNING, KIND_NOTE };

static const char *const BOLD = "\033[1m", *const RESET = "\033[m";

static const char *kind_color(int kind) {
    return kind == KIND_ERROR ? "\033[1;31m" : kind == KIND_WARNING ? "\033[1;35m" :
           kind == KIND_NOTE ? "\033[1;36m" : "";
}

bool ce_route_color(int stream, size_t n, char *const *args) {
    for (size_t i = 1; i < n; ++i) {
        if (!strcmp(args[i],"-fdiagnostics-color=always") || !strcmp(args[i],"-fdiagnostics-color")) return true;
        if (!strcmp(args[i],"-fdiagnostics-color=never") || !strcmp(args[i],"-fno-diagnostics-color")) return false;
    }
    const char *none = getenv("NO_COLOR");
    return isatty(stream) && !(none && *none);
}

static bool starts(CeText line, size_t at, const char *prefix, size_t length) {
    return at+length <= line.len && !memcmp(line.data+at,prefix,length);
}

static bool number(CeText line, size_t *at, size_t *value) {
    size_t begin = *at; *value = 0;
    while (*at < line.len && line.data[*at] >= '0' && line.data[*at] <= '9') {
        *value = *value*10 + (size_t)(line.data[*at]-'0'); ++*at;
    }
    return *at > begin;
}

/* The tokens printed on a generated line, as [first, last). */
static void line_tokens(const CeNativeOutput *map, size_t line, size_t *first, size_t *last) {
    size_t low = 0, high = map->ntokens;
    while (low < high) {
        size_t middle = (low+high)/2;
        if (map->tokens[middle].line < line) low = middle+1; else high = middle;
    }
    *first = low;
    while (low < map->ntokens && map->tokens[low].line == line) ++low;
    *last = low;
}

/* The authored offset behind a generated position: the token covering the
   column, else the last token before it, else the line's first token.
   `exact` reports a covering token. */
static bool token_offset(const CeNativeOutput *map, size_t line, size_t column, size_t *offset, bool *exact) {
    size_t first, last;
    line_tokens(map,line,&first,&last);
    if (first == last) return false;
    size_t chosen = first; *exact = false;
    for (size_t i = first; i < last; ++i) {
        const CeMapToken *t = &map->tokens[i];
        if (t->column > column) break;
        chosen = i;
        if (column < t->column+(t->length ? t->length : 1)) { *exact = true; break; }
    }
    *offset = map->tokens[chosen].offset; return true;
}

/* Generated lines a #line directive made stand for authored line `presumed`.
   When several do, prefer a token covering the column whose authored line is
   the one the compiler named, then any covering token, then any token. */
static bool presumed_offset(const CeNativeOutput *map, CeText source, size_t presumed, size_t column,
                            size_t *offset) {
    int best = -1;
    for (size_t i = 0; i < map->ndirectives; ++i) {
        const CeMapDirective *d = &map->directives[i];
        if (presumed < d->presumed) continue;
        size_t line = d->line+(presumed-d->presumed);
        if (i+1 < map->ndirectives && line+1 >= map->directives[i+1].line) continue;
        size_t candidate; bool exact;
        if (!token_offset(map,line,column,&candidate,&exact)) continue;
        int rank = exact ? (ce_source_line(source,candidate,NULL) == presumed ? 2 : 1) : 0;
        if (rank > best) { best = rank; *offset = candidate; }
        if (rank == 2) break;
    }
    return best >= 0;
}

static CeText source_line_text(CeText source, size_t line) {
    size_t start = 0, current = 1;
    for (size_t i = 0; i < source.len && current < line; ++i)
        if (source.data[i] == '\n') { ++current; start = i+1; }
    size_t end = start;
    while (end < source.len && source.data[end] != '\n') ++end;
    return (CeText){source.data+start,end-start};
}

static void excerpt(FILE *out, CeText source, size_t line, size_t column, const CeRouteStyle *style) {
    CeText text = source_line_text(source,line);
    fprintf(out,"%5zu | %.*s\n      | ",line,(int)text.len,text.data);
    for (size_t i = 0; i+1 < column && i < text.len; ++i) fputc(text.data[i] == '\t' ? '\t' : ' ',out);
    fprintf(out,"%s^%s\n",style->color ? "\033[1;32m" : "",style->color ? RESET : "");
}

static int message_kind(CeText rest) {
    if (starts(rest,0,"fatal error:",12) || starts(rest,0,"error:",6)) return KIND_ERROR;
    if (starts(rest,0,"warning:",8)) return KIND_WARNING;
    if (starts(rest,0,"note:",5)) return KIND_NOTE;
    return KIND_NONE;
}

static void located(FILE *out, const char *path, size_t line, size_t column, CeText rest,
                    const CeRouteStyle *style) {
    int kind = message_kind(rest);
    if (style->color) fputs(BOLD,out);
    fprintf(out,"%s:%zu:%zu:",path,line,column);
    if (style->color) fputs(RESET,out);
    if (kind != KIND_NONE && style->color) {
        size_t colon = 0;
        while (colon < rest.len && rest.data[colon] != ':') ++colon;
        fprintf(out," %s%.*s%s%.*s\n",kind_color(kind),(int)colon+1,rest.data,RESET,
                (int)(rest.len-colon-1),rest.data+colon+1);
    } else fprintf(out," %.*s\n",(int)rest.len,rest.data);
}

static bool route_unit(CeText line, const CeRouteUnit *unit, const CeRouteStyle *style, FILE *out);

static void route_line(CeText line, const CeRouteUnit *units, size_t nunits, const CeRouteStyle *style, FILE *out) {
    for (size_t i = 0; i < nunits; ++i) if (route_unit(line,&units[i],style,out)) return;
    fwrite(line.data,1,line.len,out); fputc('\n',out);
}

/* Rewrites a line naming this unit; false when the line does not name it. */
static bool route_unit(CeText line, const CeRouteUnit *unit, const CeRouteStyle *style, FILE *out) {
    size_t identity_length = strlen(unit->identity), generated_length = strlen(unit->generated);
    bool authored = starts(line,0,unit->identity,identity_length) && starts(line,identity_length,":",1);
    bool generated = !authored && starts(line,0,unit->generated,generated_length) &&
                     starts(line,generated_length,":",1);
    if (authored || generated) {
        size_t at = (authored ? identity_length : generated_length)+1, position = 0, column = 0;
        if (number(line,&at,&position)) {
            if (starts(line,at,":",1)) { ++at; if (!number(line,&at,&column)) column = 0; }
            if (starts(line,at,":",1)) ++at;
            if (starts(line,at," ",1)) ++at;
            CeText rest = {line.data+at,line.len-at};
            size_t offset = 0; bool exact = false, mapped;
            mapped = authored ? presumed_offset(unit->map,unit->source,position,column ? column : 1,&offset)
                              : token_offset(unit->map,position,column ? column : 1,&offset,&exact);
            size_t source_line = position, source_column = 0;
            if (mapped) source_line = ce_source_line(unit->source,offset,&source_column);
            if (!mapped && generated) return false;
            located(out,unit->identity,source_line,source_column ? source_column : 1,rest,style);
            if (message_kind(rest) != KIND_NONE && mapped)
                excerpt(out,unit->source,source_line,source_column,style);
            return true;
        }
        if (generated) {
            fprintf(out,"%s%.*s\n",unit->identity,(int)(line.len-generated_length),line.data+generated_length);
            return true;
        }
    }
    /* Include chains name the including position: "from PATH:LINE[,:]". */
    static const char *const includes[] = {"In file included from ","                 from "};
    for (size_t k = 0; k < 2; ++k) {
        size_t lead = strlen(includes[k]);
        if (starts(line,0,includes[k],lead) && starts(line,lead,unit->generated,generated_length) &&
            starts(line,lead+generated_length,":",1)) {
            size_t at = lead+generated_length+1, position, offset; bool exact;
            if (number(line,&at,&position) && token_offset(unit->map,position,1,&offset,&exact)) {
                size_t source_line = ce_source_line(unit->source,offset,NULL);
                fprintf(out,"%s%s:%zu%.*s\n",includes[k],unit->identity,source_line,
                        (int)(line.len-at),line.data+at);
                return true;
            }
        }
    }
    return false;
}

void ce_route_diagnostics(CeText text, const CeRouteUnit *units, size_t nunits,
                          const CeRouteStyle *style, FILE *out) {
    size_t start = 0;
    for (size_t i = 0; i <= text.len; ++i) {
        if (i < text.len && text.data[i] != '\n') continue;
        if (i > start || i < text.len) route_line((CeText){text.data+start,i-start},units,nunits,style,out);
        start = i+1;
    }
    fflush(out);
}

void ce_report_error(FILE *out, const char *path, CeText source, size_t offset,
                     const char *message, const CeRouteStyle *style) {
    size_t column, line = ce_source_line(source,offset,&column);
    size_t length = strlen(message)+8;
    char *rest = malloc(length);
    if (!rest) { fprintf(out,"%s:%zu:%zu: error: %s\n",path,line,column,message); return; }
    snprintf(rest,length,"error: %s",message);
    located(out,path,line,column,(CeText){(unsigned char *)rest,strlen(rest)},style);
    free(rest);
    if (offset <= source.len) excerpt(out,source,line,column,style);
    fflush(out);
}

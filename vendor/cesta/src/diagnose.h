#ifndef CESTA_DIAGNOSE_H
#define CESTA_DIAGNOSE_H

#include "native.h"

/* Diagnostics in authored terms. A compiled unit names its authored source in
   #line directives; its source map relates generated positions to authored
   offsets. Routing rewrites the compiler's positions in that unit to authored
   lines and columns and shows the authored line under each diagnostic. */
typedef struct {
    const char *identity;            /* authored path, as named in #line */
    const char *generated;           /* path of the C the compiler read */
    CeText source;                   /* authored text */
    const CeNativeOutput *map;
} CeRouteUnit;

typedef struct { bool color; } CeRouteStyle;

/* Whether diagnostics written to `stream` should be coloured, given the
   compiler arguments (an explicit -fdiagnostics-color= wins). */
bool ce_route_color(int stream, size_t n, char *const *args);

/* Lines naming one of the units are rewritten; others pass unchanged. */
void ce_route_diagnostics(CeText text, const CeRouteUnit *units, size_t nunits,
                          const CeRouteStyle *style, FILE *out);

/* Cesta's own error at a source offset: path:line:column: error: message,
   then the authored line with a caret. */
void ce_report_error(FILE *out, const char *path, CeText source, size_t offset,
                     const char *message, const CeRouteStyle *style);

#endif

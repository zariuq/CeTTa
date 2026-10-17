#ifndef CETTA_REGEX_TEXT_V1_H
#define CETTA_REGEX_TEXT_V1_H

#include "src/atom.h"

/* Extended-mode lexical preparation; grammar recognition remains the authored
 * parser's responsibility. Returns (re:source-text cleaned mapping), or an
 * explicit source/UTF-8/allocation outcome. No implicit work/depth limit. */
Atom *cetta_re_v1_prepare_text(Arena *arena, Atom *text);

/* CST positions are half-open scalar offsets. Relocate them and parser error
 * scalar/byte offsets to the original text. A packed forest is not relocated:
 * its original coordinates remain explicit in re:lexed-forest-v1 provenance.
 * Empty CST spans use the next retained scalar (or original EOF); nonempty
 * spans use the first/last retained scalar edges. */
Atom *cetta_re_v1_locate_source(Arena *arena, Atom *observation, Atom *mapping);

#endif

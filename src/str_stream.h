#ifndef CETTA_STR_STREAM_H
#define CETTA_STR_STREAM_H
#include "str_natives.h"

/* Pure transducers over explicit values. No file access, mutable owners,
 * locale changes or grammar preparation occurs in these operations. */
Atom *cetta_str_utf8_new(Arena *, Atom *const *, CettaStrRefusal *);
Atom *cetta_str_utf8_feed(Arena *, Atom *const *, CettaStrRefusal *);
Atom *cetta_str_line_segments(Arena *, Atom *const *, CettaStrRefusal *);
Atom *cetta_str_line_new(Arena *, Atom *const *, CettaStrRefusal *);
Atom *cetta_str_collect_lines(Arena *, Atom *const *, CettaStrRefusal *);
Atom *cetta_str_feed_lines(Arena *, Atom *const *, CettaStrRefusal *);
Atom *cetta_str_advance_lines(Arena *, Atom *const *, CettaStrRefusal *);
Atom *cetta_str_find_ascii_uncased(Arena *, Atom *const *, CettaStrRefusal *);
#endif

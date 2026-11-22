/* rc_rep.h - the representations rulec may give a sort.
 *
 * One list for the compiler, the generated code and the hosts that read a module's values.  The
 * generated table rc_sort_rep names each sort's representation by these constants (never by number),
 * and the second column is the suffix of the representation's views: RC_TAG_<view>, RC_KID_<view>,
 * RC_NUL_<view>, RC_MK<n>_<view> (rc_rt.h and the components; a representation with parameters of
 * its own per sort, word, binary and slices, names its views by the sort instead).
 */
#ifndef RC_REP_H
#define RC_REP_H

#define RC_REP_LIST(X) \
    X(SYMBOLIC, symbolic) X(RECORDS, records) X(NAT, nat) X(INTERN, intern) X(BYTES, bytes) X(TABLE, table) \
    X(UNARY, unary) X(WORD, word) X(BINARY, binary) X(SLICES, slices) X(PROMISE, promise)

#define RC_REP_ID(name, view) RC_REP_##name,
enum { RC_REP_LIST(RC_REP_ID) RC_REP_COUNT };
#undef RC_REP_ID

#endif

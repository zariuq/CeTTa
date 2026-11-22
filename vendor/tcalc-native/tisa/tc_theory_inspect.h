/*
 * tc_theory_inspect.h — Read-only reflection interface over instantiated tcalc
 * tables (Sorts, Constructors, Rules, Hole Contexts, and Compiled Flat layouts).
 *
 * Used by `--export-theory-json` and the t-ISA rule specializer so that every
 * manifest and compiled leaf is derived directly from tcalc's live tables
 * without any hand-transcribed duplicate rule table.
 */
#pragma once

#include "../tcalc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t i, j, i0, j0;
} TcInspectGuard;

typedef struct {
    uint16_t sort;
    uint8_t  bound;
    uint64_t avoid;
} TcInspectHole;

typedef struct {
    int      con_id;
    uint8_t  sort;
    uint8_t  arity;
    uint8_t  ndum;
    uint8_t  isdef;
    uint32_t abound;
    uint64_t retdeps;
    uint8_t  asort[TC_ARGS];
} TcInspectCon;

typedef struct {
    int            rule_id;
    int            nvars;
    uint8_t        nkeep;
    uint8_t        mspace;
    uint8_t        fibre;
    bool           pure;
    bool           flat;
    bool           mflat;
    uint64_t       implied;
    uint64_t       mimplied;
    uint64_t       bounds;
    Rule           decl_rule;
    TcInspectHole  holes[TC_VARS];
} TcInspectRule;

typedef struct {
    uint32_t       fl_index;
    bool           ordinary;
    uint32_t       ktag[TC_HYPS];
    uint32_t       mhead;
    uint16_t       msort;
    uint16_t       mar;
    uint16_t       nguard;
    uint16_t       nconst;
    TcInspectGuard guard[32];
    uint8_t        mi[TC_ARGS];
    uint8_t        mj[TC_ARGS];
    Term           mc[32];
    uint32_t       packed_guard[32];
    uint32_t       packed_make[TC_ARGS];
} TcInspectFlat;

int     tc_inspect_sort_count(void);
uint8_t tc_inspect_sort_flags(int sort_id);
bool    tc_inspect_space_protected(uint8_t space);
int     tc_inspect_con_count(void);
bool    tc_inspect_con(int con_id, TcInspectCon *out);
bool    tc_inspect_rule(int rule_id, TcInspectRule *out, TcInspectFlat *out_flat);

#ifdef __cplusplus
}
#endif

#ifndef CETTA_PETTA_NUMERIC_H
#define CETTA_PETTA_NUMERIC_H

#include "atom.h"
#include "term_universe.h"

#include <stddef.h>
#include <stdio.h>

typedef enum {
    CETTA_PETTA_NUMBER_SOURCE_TOKEN = 0,
    CETTA_PETTA_NUMBER_ATOM_TEXT = 1,
} CettaPeTTaNumberGrammar;

/*
 * PeTTa has two deliberate numeric grammars inherited from SWI-Prolog:
 * source tokens use number//1, while host text such as argv uses atom_number/2.
 * These entry points share one locale-independent codec without conflating the
 * two language judgments.  NULL/NONE means that the bytes are not a number.
 */
Atom *cetta_petta_number_parse_atom(
    Arena *arena, const uint8_t *bytes, size_t length,
    CettaPeTTaNumberGrammar grammar);

AtomId cetta_petta_number_parse_id(
    TermUniverse *universe, const uint8_t *bytes, size_t length,
    CettaPeTTaNumberGrammar grammar);

int cetta_petta_number_format_float(
    char *buffer, size_t size, double value);

bool cetta_petta_number_format_fraction(
    char *buffer, size_t size, double value, size_t precision);

bool cetta_petta_number_print(const Atom *atom, FILE *output);

char *cetta_petta_number_to_string(Arena *arena, const Atom *atom);

#endif

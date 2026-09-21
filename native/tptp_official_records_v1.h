#ifndef CETTA_TPTP_OFFICIAL_RECORDS_V1_H
#define CETTA_TPTP_OFFICIAL_RECORDS_V1_H

#include "src/atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1 \
    "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6"

/* Project a ParserPack CST tuple (AuthoredParseAccepted trees) to compact
 * tptp-rec:* values. Does not build an EBNF derivation forest. A leftover
 * production becomes (TPTP:Unprojected name) rather than tptp-rec:unknown.
 * The official source alternative named "unknown" is tptp-rec:source-unknown. */
bool cetta_tptp_records_from_cst_v1(
    const Atom *trees,
    const char *source,
    Arena *arena,
    Atom **out,
    char *error,
    size_t error_size);

/* Same projection without GLL-layout span retouch. Used when combining
 * per-TPTP_input records, then retouching the assembled file once. */
bool cetta_tptp_records_from_cst_noreouch_v1(
    const Atom *trees,
    const char *source,
    Arena *arena,
    Atom **out,
    char *error,
    size_t error_size);

bool cetta_tptp_records_from_cst_noreouch_ascii_v1(
    const Atom *trees,
    const char *source,
    bool source_ascii,
    Arena *arena,
    Atom **out,
    char *error,
    size_t error_size);

bool cetta_tptp_file_from_inputs_v1(
    Arena *arena,
    const char *source,
    Atom **inputs,
    uint32_t n,
    Atom **out,
    char *error,
    size_t error_size);

bool cetta_tptp_file_sha256_hex_v1(
    const char *path, char out[65], char *error, size_t error_size);

bool cetta_tptp_write_atom_v1(
    const char *path, Atom *atom, Arena *arena, char *error, size_t error_size);

bool cetta_tptp_read_atom_v1(
    const char *path, Arena *arena, Atom **out, char *error, size_t error_size);

#endif

#ifndef CETTA_GSLT2PARSE_PARSER_PACK_ABI_STREAM_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_ABI_STREAM_V1_H

#include <stdbool.h>
#include <stddef.h>

#include "parser_pack_abi_v1.h"

typedef struct {
    Arena arena;
    Atom **productions;
    size_t production_len;
    size_t production_cap;
    Atom **classes;
    size_t class_len;
    size_t class_cap;
    PPABIV1DerivationInput *derivations;
    size_t derivation_len;
    size_t derivation_cap;
    Atom *start;
    char source_digest[65];
    char compiler_digest[65];
    char environment_digest[65];
    char expected_pack_digest[65];
    bool expected_closed;
} PPABIV1Wire;

void ppabi_v1_wire_init(PPABIV1Wire *wire);
void ppabi_v1_wire_free(PPABIV1Wire *wire);

bool ppabi_v1_wire_read(PPABIV1Wire *wire,
                        const char *path,
                        char *error_buf,
                        size_t error_buf_size);

bool ppabi_v1_wire_load_pack(const PPABIV1Wire *wire,
                             PPABIV1Pack *out,
                             char *error_buf,
                             size_t error_buf_size);

#endif

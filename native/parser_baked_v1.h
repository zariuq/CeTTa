#ifndef CETTA_PARSER_BAKED_V1_H
#define CETTA_PARSER_BAKED_V1_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *bytes;
    size_t length;
} CettaBakedParserV1;

/* These are the same portable artifacts accepted by the file loader. */
const CettaBakedParserV1 *cetta_parser_baked_v1_find(const char *name);

#endif
